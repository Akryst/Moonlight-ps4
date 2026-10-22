#include "../src/gamestream/gs_http.h"
#include "../src/gamestream/gs_errors.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>


void log_printf(const char *fmt,...){(void)fmt;}
static int listener,mode;
static atomic_bool waiting;
static unsigned long long milliseconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (unsigned long long)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;
}
static void *server(void *unused) {
    (void)unused;int fd=accept(listener,NULL,NULL);assert(fd>=0);
    struct timeval timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    char request[4096];assert(recv(fd,request,sizeof(request),0)>0);atomic_store(&waiting,true);
    const char *response=mode==3?"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\no":"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    if(mode==2)while(recv(fd,request,sizeof(request),0)>0){}
    else if(mode==1) {
        for(const char *p=response;*p;p++){if(send(fd,p,1,0)!=1)break;usleep(20000);}
    } else (void)send(fd,response,strlen(response),0);
    close(fd);return NULL;
}
static void *cancel(void *unused) {
    (void)unused;for(int i=0;i<1000 && !atomic_load(&waiting);i++)usleep(1000);
    assert(atomic_load(&waiting));http_cancel_pending();return NULL;
}

static pthread_barrier_t error_barrier;
static void *isolated_error(void *arg){
    const char *message=arg;gs_error=message;
    pthread_barrier_wait(&error_barrier);assert(!strcmp(gs_error,message));
    return NULL;
}
static void check_error_isolation(void){
    pthread_barrier_init(&error_barrier,NULL,2);pthread_t a,b;
    pthread_create(&a,NULL,isolated_error,"PC one refused");pthread_create(&b,NULL,isolated_error,"PC two timed out");
    pthread_join(a,NULL);pthread_join(b,NULL);pthread_barrier_destroy(&error_barrier);
}

static void *pool_server(void *arg){
    int socket_fd=*(int *)arg;int fd=accept(socket_fd,NULL,NULL);assert(fd>=0);
    struct timeval timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    for(int i=0;i<2;i++){
        char request[4096];assert(recv(fd,request,sizeof(request),0)>0);
        const char response[]="HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok";
        assert(send(fd,response,sizeof(response)-1,0)==sizeof(response)-1);
    }
    close(fd);return NULL;
}
static void check_keep_alive(void){
    int socket_fd=socket(AF_INET,SOCK_STREAM,0);assert(socket_fd>=0);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(socket_fd,(struct sockaddr *)&address,sizeof(address)));assert(!listen(socket_fd,1));
    socklen_t size=sizeof(address);assert(!getsockname(socket_fd,(struct sockaddr *)&address,&size));
    pthread_t thread;pthread_create(&thread,NULL,pool_server,&socket_fd);
    for(int i=0;i<2;i++){http_resp_t response={0};assert(http_get_timeout("127.0.0.1",ntohs(address.sin_port),0,"/serverinfo",&response,0,500)==GS_OK);assert(response.len==2);http_resp_free(&response);}
    pthread_join(thread,NULL);close(socket_fd);
}
int main(void) {
    check_error_isolation();
    signal(SIGPIPE,SIG_IGN);http_init(NULL,0);
    for(mode=0;mode<4;mode++) {
        listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
        struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)));assert(!listen(listener,1));
        socklen_t length=sizeof(address);assert(!getsockname(listener,(struct sockaddr *)&address,&length));
        atomic_store(&waiting,false);pthread_t worker,canceller;
        assert(!pthread_create(&worker,NULL,server,NULL));
        if(mode==2)assert(!pthread_create(&canceller,NULL,cancel,NULL));
        http_set_timeout_ms(mode==2?1000:150);http_resp_t response={0};
        unsigned long long start=milliseconds();int rc=http_get("127.0.0.1",ntohs(address.sin_port),0,"/test",&response);
        assert(milliseconds()-start<1000);
        if(mode==0)assert(rc==GS_OK && response.len==2 && !memcmp(response.body,"ok",2));
        else assert(rc!=GS_OK);
        http_resp_free(&response);pthread_join(worker,NULL);
        if(mode==2)pthread_join(canceller,NULL);close(listener);
    }
    check_keep_alive();
    puts("HTTP: IPv4 connection, total deadline, cancellation and truncated response checks passed");
}
