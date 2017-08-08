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
 * The logger structure
 */
typedef struct {
	// the main buffer of the logger
	logger_data_t* buffer;

	// the next place to insert a data unit at
	logger_data_t* head;
	// the next place to read a data unit from
	logger_data_t* tail;

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
 * Return the number of data units available for reading in the logger
 */
int logger_size(Logger* logger);

/*
 * Read `length` data units from the logger to the buffer.
 * If the logger does not have `length` data units, block until it is has enough data
 */
void logger_read(Logger* logger, logger_data_t* buffer, int length);

/*
 * Free the logger
 */
void logger_free(Logger* logger);


#endif
