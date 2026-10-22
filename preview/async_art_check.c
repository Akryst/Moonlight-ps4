#include "../src/ui/ui_art.c"
#include <assert.h>
#include <sys/time.h>

static const unsigned char png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,3,8,6,0,0,0,185,234,222,129,0,0,0,22,73,68,65,84,120,156,99,252,192,35,241,159,129,129,129,129,137,1,10,224,12,0,51,33,2,25,245,68,243,254,0,0,0,0,73,69,78,68,174,66,96,130};

static atomic_int requests,blocked,cancelled;
void http_set_timeout_ms(unsigned ms){(void)ms;}
void http_cancel_thread(pthread_t thread){(void)thread;atomic_store(&cancelled,1);}
int gs_appasset(gs_server_t *server,int id,unsigned char **bytes,size_t *size) {
    (void)server;(void)id;atomic_fetch_add(&requests,1);
    while(atomic_load(&blocked) && !atomic_load(&cancelled))usleep(1000);
    *bytes=malloc(sizeof(png));assert(*bytes);memcpy(*bytes,png,sizeof(png));*size=sizeof(png);return GS_OK;
}
static void wait_revision(unsigned previous) {
    for(int i=0;i<2000 && ui_art_revision()==previous;i++)usleep(1000);
    assert(ui_art_revision()!=previous);
}
int main(void) {
    char directory[]="/tmp/moonlight-art-XXXXXX";assert(mkdtemp(directory));
    gs_server_t server={0};snprintf(server.address,sizeof(server.address),"192.0.2.1");server.httpPort=47989;
    uint32_t pixels[64]={0};ui_surface_t surface={(uint8_t *)pixels,32,8,8};
    atomic_store(&blocked,1);ui_art_start(&server,directory);
    ui_art_draw(&server,7,&surface,0,0,8,8);
    for(int i=0;i<2000 && !atomic_load(&requests);i++)usleep(1000);
    assert(atomic_load(&requests)==1);
    /* Drawing continues while the network request is deliberately blocked. */
    for(int i=0;i<100;i++)ui_art_draw(&server,7,&surface,0,0,8,8);
    assert(!atomic_load(&cancelled));ui_art_clear();assert(atomic_load(&cancelled));
    atomic_store(&blocked,0);usleep(20000);atomic_store(&cancelled,0);
    unsigned revision=ui_art_revision();ui_art_start(&server,directory);
    ui_art_draw(&server,7,&surface,0,0,8,8);wait_revision(revision);ui_art_clear();
    int fetched=atomic_load(&requests);revision=ui_art_revision();
    ui_art_start(&server,directory);ui_art_draw(&server,7,&surface,0,0,8,8);wait_revision(revision);
    assert(atomic_load(&requests)==fetched);ui_art_clear();
    snprintf(server.address,sizeof(server.address),"192.0.2.2");revision=ui_art_revision();
    ui_art_start(&server,directory);ui_art_draw(&server,7,&surface,0,0,8,8);wait_revision(revision);
    assert(atomic_load(&requests)==fetched+1);ui_art_stop();
    char path[640];snprintf(path,sizeof(path),"%s/unrelated.keep",cache_dir);
    FILE *file=fopen(path,"wb");assert(file);fclose(file);
    for(int i=0;i<130;i++) {
        snprintf(path,sizeof(path),"%s/0000000000000000-%d.png",cache_dir,i);
        file=fopen(path,"wb");assert(file);fputs("test",file);fclose(file);
    }
    assert(cache_room(cache_dir,1024));int count=0;
    DIR *dir=opendir(cache_dir);assert(dir);struct dirent *entry;
    while((entry=readdir(dir))) {
        count+=cache_filename(entry->d_name);
        if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) {
            snprintf(path,sizeof(path),"%s/%s",cache_dir,entry->d_name);assert(!unlink(path));
        }
    }
    closedir(dir);assert(count<128);ui_art_clear();assert(!rmdir(cache_dir));assert(!rmdir(directory));
    puts("Async artwork: responsive drawing, cancellation, persistent cache, host isolation and eviction passed");
}
