#ifndef __COMMON_SETUP__
#define __COMMON_SETUP__

#include <pthread.h>
#include <stdlib.h>

#include "common.h"

extern pthread_t _server;

void start_log_server(){
     if (log_server_init() != 0){
          printf("FAILURE: log_server_init\n");
          exit(1);
     }

     if (pthread_create(&_server, NULL, log_server_run, NULL) != 0){
          printf("FAILURE: pthread_create...log_server_run...\n");
          exit(1);
     }

     printf("Server opened\n");
     printf("Browse to http://127.0.0.1:%d/ to see the statistics\n",
             LOG_SERVER_PORT);
}

#endif
