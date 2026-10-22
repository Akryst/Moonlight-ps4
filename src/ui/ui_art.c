#include "ui_art.h"
#include "ui_theme.h"
#include "../gamestream/gs_errors.h"
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include "../gamestream/gs_http.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../vendor/stb_image.h"
#pragma GCC diagnostic pop
#include "ui_lake_data.h"
#include <string.h>
static unsigned char *landscape;
static int landscape_w, landscape_h;
void ui_landscape(ui_surface_t *surface,int x,int y,int w,int h,int radius) {
    if(!landscape) {
        int channels;
        landscape=stbi_load_from_memory(lake_png,sizeof(lake_png),&landscape_w,&landscape_h,&channels,4);
    }
    if(landscape)ui_image_round(surface,x,y,w,h,landscape,landscape_w,landscape_h,radius);
    else ui_round_rect(surface,x,y,w,h,radius,ui_theme_current()->panel);
}

/* Menu-scoped, bounded cache. No cover requests/decoding during streaming. */
#define ART_SLOTS 8
typedef struct { int id,w,h,hw,hh;unsigned stamp;unsigned char *rgba,*hero; } artwork_t;
static artwork_t slots[ART_SLOTS];
static unsigned s_stamp;
static unsigned char *controls[UI_CONTROLS];
static unsigned controls_tried;
void ui_control_image(ui_surface_t *s,int button,int x,int y,int size) {
    static const char *names[]={"cross","circle","square","triangle","options","dpad","l1","r1"};
    if(button<0 || button>=UI_CONTROLS || size<1)return;
    if(!(controls_tried&(1u<<button))) {
        controls_tried|=1u<<button;
        char path[192];
#ifdef __ORBIS__
        snprintf(path,sizeof(path),"/app0/assets/images/ps4-%s.png",names[button]);
#else
        snprintf(path,sizeof(path),"pkg/assets/ui/buttons/%s.png",names[button]);
#endif
        FILE *file=fopen(path,"rb");
        if(file) {
            unsigned char encoded[32768];
            size_t length=fread(encoded,1,sizeof(encoded),file);
            int oversized=fgetc(file)!=EOF;fclose(file);
            int w,h,c;
            if(!oversized && stbi_info_from_memory(encoded,(int)length,&w,&h,&c) && w==128 && h==128)
                controls[button]=stbi_load_from_memory(encoded,(int)length,&w,&h,&c,4);
        }
    }
    if(controls[button])ui_image_alpha(s,x,y,size,size,controls[button],128,128);
    else {
        /* Keep navigation usable if a resource is missing from an older package. */
        const char *labels[]={"X","O","[]","/\\","OPT","+","L1","R1"};
        ui_label(s,x,y,size/2,0,ui_theme_current()->text,labels[button]);
    }
}
static unsigned read_be32(const unsigned char *p) {
    return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3];
}
/* Optional embedded panorama in standard PNG; truncated/oversized chunks fail closed. */
static unsigned char *decode_hero(const unsigned char *png,size_t size,int *width,int *height) {
    if(size<8 || memcmp(png,"\x89PNG\r\n\x1a\n",8))return NULL;
    size_t pos=8;
    while(size-pos>=12) {
        unsigned len=read_be32(png+pos);
        if(len>size-pos-12)return NULL;
        if(!memcmp(png+pos+4,"mlBg",4)) {
            int w,h,c;
            if(len>INT_MAX||!stbi_info_from_memory(png+pos+8,(int)len,&w,&h,&c))return NULL;
            if(w<=0||h<=0||w>1920||h>1080||(size_t)w*h>2200000)return NULL;
            return stbi_load_from_memory(png+pos+8,(int)len,width,height,&c,4);
        }
        if(!memcmp(png+pos+4,"IEND",4))break;
        pos+=(size_t)len+12;
    }
    return NULL;
}
static pthread_mutex_t art_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t art_ready=PTHREAD_COND_INITIALIZER;
static pthread_t art_thread;
static int art_alive,art_stop,art_async,art_queue[8],art_count,art_active=-1;
static gs_server_t *art_server;
static char cache_dir[512],cache_prefix[32];
static atomic_uint art_revision;
static unsigned art_generation;

static void artwork_free(artwork_t *a) {
    stbi_image_free(a->rgba);stbi_image_free(a->hero);memset(a,0,sizeof(*a));
}
static artwork_t artwork_decode(int id,const unsigned char *encoded,size_t length) {
    artwork_t a={0};a.id=id;int w,h,c;
    if(encoded && length<=4*1024*1024 && stbi_info_from_memory(encoded,(int)length,&w,&h,&c) &&
       w>0 && h>0 && w<=1200 && h<=1800 && (size_t)w*h<=2200000) {
        a.rgba=stbi_load_from_memory(encoded,(int)length,&a.w,&a.h,&c,4);
        if(a.rgba)a.hero=decode_hero(encoded,length,&a.hw,&a.hh);
    }
    return a;
}
static void artwork_publish(artwork_t *a,unsigned generation) {
    pthread_mutex_lock(&art_mutex);
    if(generation!=art_generation){pthread_mutex_unlock(&art_mutex);artwork_free(a);return;}
    int slot=0;
    for(int i=0;i<ART_SLOTS;i++) {
        if(slots[i].stamp && slots[i].id==a->id){slot=i;break;}
        if(slots[i].stamp<slots[slot].stamp)slot=i;
    }
    artwork_free(&slots[slot]);a->stamp=++s_stamp;slots[slot]=*a;memset(a,0,sizeof(*a));
    atomic_fetch_add(&art_revision,1);
    pthread_mutex_unlock(&art_mutex);
}
static int cache_filename(const char *name) {
    size_t len=strlen(name);
    if(len<22 || len>40 || name[16]!='-' || strcmp(name+len-4,".png"))return 0;
    for(int i=0;i<16;i++)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return 0;
    for(size_t i=17;i<len-4;i++)if(name[i]<'0'||name[i]>'9')return 0;
    return 1;
}
static int cache_room(const char *directory,size_t incoming) {
    for(;;) {
        DIR *dir=opendir(directory);if(!dir)return 0;
        size_t bytes=0;int count=0;time_t oldest_time=0;char oldest[640]={0};struct dirent *entry;
        while((entry=readdir(dir))) {
            if(!cache_filename(entry->d_name))continue;
            char path[640];snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name);
            struct stat info;if(stat(path,&info) || !S_ISREG(info.st_mode))continue;
            bytes+=(size_t)info.st_size;count++;
            if(!oldest[0] || info.st_mtime<oldest_time){oldest_time=info.st_mtime;snprintf(oldest,sizeof(oldest),"%s",path);}
        }
        closedir(dir);
        if(bytes+incoming<=64*1024*1024 && count<128)return 1;
        if(!oldest[0] || unlink(oldest))return 0;
    }
}
static void cache_write(const char *path,const unsigned char *data,size_t size) {
    char directory[640];snprintf(directory,sizeof(directory),"%s",path);char *slash=strrchr(directory,'/');if(!slash)return;*slash=0;
    if(!cache_room(directory,size))return;
    char temporary[660];snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    FILE *file=fopen(temporary,"wb");if(!file)return;
    int good=fwrite(data,1,size,file)==size;
    if(fclose(file))good=0;
    if(!good || rename(temporary,path))unlink(temporary);
}
static void *artwork_worker(void *unused) {
    (void)unused;http_set_timeout_ms(1800);
    for(;;) {
        pthread_mutex_lock(&art_mutex);
        while(art_stop || !art_count)pthread_cond_wait(&art_ready,&art_mutex);
        int id=art_queue[0];memmove(art_queue,art_queue+1,(size_t)--art_count*sizeof(int));art_active=id;
        unsigned generation=art_generation;
        gs_server_t server=*art_server;
        char path[640];snprintf(path,sizeof(path),"%s/%s-%d.png",cache_dir,cache_prefix,id);
        pthread_mutex_unlock(&art_mutex);
        struct stat info;int fresh=0;
        if(!stat(path,&info) && info.st_size>0 && info.st_size<=4*1024*1024) {
            FILE *file=fopen(path,"rb");
            if(file) {
                unsigned char *bytes=malloc((size_t)info.st_size);
                if(bytes && fread(bytes,1,(size_t)info.st_size,file)==(size_t)info.st_size) {
                    artwork_t cached=artwork_decode(id,bytes,(size_t)info.st_size);
                    if(cached.rgba){artwork_publish(&cached,generation);fresh=time(NULL)-info.st_mtime<7*24*3600;}
                }
                free(bytes);fclose(file);
            }
        }
        pthread_mutex_lock(&art_mutex);int stop=art_stop || generation!=art_generation;pthread_mutex_unlock(&art_mutex);
        if(!fresh && !stop) {
            unsigned char *encoded=NULL;size_t length=0;
            int rc=gs_appasset(&server,id,&encoded,&length);
            pthread_mutex_lock(&art_mutex);stop=art_stop || generation!=art_generation;pthread_mutex_unlock(&art_mutex);
            if(!stop) {
                artwork_t image=artwork_decode(id,encoded,length);
                if(rc==GS_OK && image.rgba){cache_write(path,encoded,length);artwork_publish(&image,generation);}
                else {
                    artwork_free(&image);
                    /* Mark a miss once per menu, retaining any stale-but-valid image. */
                    pthread_mutex_lock(&art_mutex);int found=0;
                    for(int i=0;i<ART_SLOTS;i++)if(slots[i].stamp && slots[i].id==id)found=1;
                    pthread_mutex_unlock(&art_mutex);
                    if(!found){artwork_t missing={0};missing.id=id;artwork_publish(&missing,generation);}
                }
            }
            free(encoded);
        }
        pthread_mutex_lock(&art_mutex);art_active=-1;pthread_mutex_unlock(&art_mutex);
    }
    return NULL;
}
void ui_art_start(gs_server_t *server,const char *directory) {
    if(!server)return;
    pthread_mutex_lock(&art_mutex);
    art_async=1;art_stop=0;art_count=0;art_active=-1;art_server=server;
    snprintf(cache_dir,sizeof(cache_dir),"%s/artwork",directory);mkdir(cache_dir,0755);
    uint64_t hash=14695981039346656037ull;
    const char *keys[]={server->address,server->serverCertPem};
    for(int i=0;i<2;i++)for(const unsigned char *p=(const unsigned char *)keys[i];*p;p++)hash=(hash^*p)*1099511628211ull;
    hash=(hash^server->httpPort)*1099511628211ull;
    snprintf(cache_prefix,sizeof(cache_prefix),"%016llx",(unsigned long long)hash);
    if(!art_alive){art_alive=pthread_create(&art_thread,NULL,artwork_worker,NULL)==0;if(art_alive)pthread_detach(art_thread);}
    pthread_cond_signal(&art_ready);pthread_mutex_unlock(&art_mutex);
}
void ui_art_stop(void) {
    pthread_mutex_lock(&art_mutex);
    art_stop=1;art_generation++;art_count=0;art_active=-1;
    pthread_mutex_unlock(&art_mutex);
    if(art_alive)http_cancel_thread(art_thread);
}
unsigned ui_art_revision(void){return atomic_load(&art_revision);}
static void artwork_request(int id) {
    if(!art_alive || art_stop || art_active==id)return;
    for(int i=0;i<art_count;i++)if(art_queue[i]==id)return;
    if(art_count<8){art_queue[art_count++]=id;pthread_cond_signal(&art_ready);}
}
void ui_art_clear(void) {
    ui_art_stop();pthread_mutex_lock(&art_mutex);art_async=0;
    for(int i=0;i<UI_CONTROLS;i++){stbi_image_free(controls[i]);controls[i]=NULL;}
    controls_tried=0;
    stbi_image_free(landscape);landscape=NULL;
    for(int i=0;i<ART_SLOTS;i++)artwork_free(&slots[i]);
    s_stamp=0;pthread_mutex_unlock(&art_mutex);
}
void ui_art_draw(gs_server_t *server, int app_id, ui_surface_t *surface,
                 int x, int y, int width, int height) {
    ui_art_draw_round(server,app_id,surface,x,y,width,height,0);
}
void ui_art_draw_round(gs_server_t *server, int app_id, ui_surface_t *surface,
                      int x, int y, int width, int height, int radius) {
    pthread_mutex_lock(&art_mutex);
    int slot=-1;
    for(int i=0;i<ART_SLOTS;i++)if(slots[i].stamp && slots[i].id==app_id)slot=i;
    if(slot<0 && art_async)artwork_request(app_id);
    if(slot<0 && !art_async) {
        /* Synchronous fixture renderer; production starts the worker before drawing covers. */
        slot=0;for(int i=1;i<ART_SLOTS;i++)if(slots[i].stamp<slots[slot].stamp)slot=i;
        artwork_free(&slots[slot]);
        unsigned char *encoded=NULL;size_t length=0;
        if(server && gs_appasset(server,app_id,&encoded,&length)==GS_OK)slots[slot]=artwork_decode(app_id,encoded,length);
        free(encoded);slots[slot].id=app_id;
    }
    if(slot<0) {
        pthread_mutex_unlock(&art_mutex);
        ui_round_rect(surface,x,y,width,height,radius,ui_theme_current()->panel);return;
    }
    slots[slot].stamp = ++s_stamp;
    if(slots[slot].hero && width*10>height*11)
        ui_image_round(surface,x,y,width,height,slots[slot].hero,slots[slot].hw,slots[slot].hh,radius);
    else if (slots[slot].rgba)
        ui_image_round(surface, x, y, width, height, slots[slot].rgba, slots[slot].w, slots[slot].h,radius);
    else {
        const ui_theme_t *t = ui_theme_current();
        ui_round_rect(surface,x,y,width,height,radius,t->panel);
        ui_icon(surface,x+width/2-40,y+height/2-35,80,0,t->text);
    }
    pthread_mutex_unlock(&art_mutex);
}
