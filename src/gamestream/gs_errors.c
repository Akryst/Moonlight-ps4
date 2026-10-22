#include "gs_errors.h"
#include <pthread.h>
#include <stdlib.h>
typedef struct { const char *message; char buffer[512]; } error_state_t;
static pthread_key_t key;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void init_key(void){if(pthread_key_create(&key,free))abort();}
static error_state_t *state(void){
    pthread_once(&once,init_key);
    error_state_t *s=pthread_getspecific(key);
    if(!s){s=calloc(1,sizeof(*s));if(!s || pthread_setspecific(key,s))abort();}
    return s;
}
const char **gs_error_slot(void){return &state()->message;}
char *gs_error_buffer(void){return state()->buffer;}

pthread_mutex_t identity_crypto_mutex=PTHREAD_MUTEX_INITIALIZER;
