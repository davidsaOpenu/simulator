/*
 * Copyright 2017 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LOGGING_BACKEND_H__
#define __LOGGING_BACKEND_H__

#include <pthread.h>


/*
 * The basic data unit used by the logger
 */
typedef unsigned char logger_data_t;


/*
 * The number of full data units above which the consumer will be woken up
 */
#define EMPTY_LOG_THREASHOLD 128
/*
 * The number of empty data units above which the producer will be woken up
 */
#define FULL_LOG_THREASHOLD 128

/*
 * The maximum size of the logger (about ~100Mb)
 */
#define LOGGER_MAX_SIZE (1 << 30)


/*
 * The logger structure
 */
typedef struct {
	// the main buffer of the logger
	logger_data_t buffer[LOGGER_MAX_SIZE] __attribute__ ((aligned (4))); // TODO align to cache line?

	// the next place to insert a data unit at
	logger_data_t* head;
	// the next place to read a data unit from
	logger_data_t* tail;

	// the lock of the logger
	pthread_mutex_t lock;
	// a condition variable for when the buffer is full
	pthread_cond_t buffer_full;
	// a condition variable for when the buffer is empty
	pthread_cond_t buffer_empty;
} Logger;

/*
 * Create a new logger and return it
 */
Logger* logger_init(void);

/*
 * Write `length` data units from the buffer to the logger
 * If the logger can not fit all the data, block until it can fit them all
 */
void logger_write(Logger* logger, logger_data_t* buffer, int length);

/*
 * Read `length` data units from the logger to the buffer.
 * If the logger does not have `length` data units, block until it has enough data
 */
void logger_read(Logger* logger, logger_data_t* buffer, int length);

/*
 * Free the logger
 */
void logger_free(Logger* logger);


#endif
