// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _LOG_MANAGER_H_
#define _LOG_MANAGER_H_

#include "logging_backend.h"


void INIT_LOG_MANAGER(void);
void TERM_LOG_MANAGER(void);
void WRITE_LOG(const char *fmt, ...);

void THREAD_SERVER(void);
void THREAD_CLIENT(void *arg);

/**
 * Returns the appropriate logger of the flash
 * @param flash_number the number of the flash
 * @return the appropriate logger of the flash, or NULL if an error occurred
 *         (or if the logger is not set up)
 */
Logger* GET_LOGGER(unsigned int flash_number);

#endif
