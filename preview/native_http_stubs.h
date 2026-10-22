#pragma once
#include <pthread.h>

void http_set_timeout_ms(unsigned ms){(void)ms;}
void http_cancel_pending(void){}

void http_cancel_thread(pthread_t thread){(void)thread;}
