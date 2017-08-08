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

/*
 * The backend logging mechanism is implemented using one long array in memory, which contains a circular array inside
 * The array is aligned in memory, for a better caching performance
 */

#include <string.h>

#include "logging_backend.h"


/*
 * Create a new logger and return it
 */
Logger* logger_init(void) {
	//TODO
	return 0;
}

/*
 * Write `length` data units from the buffer to the logger
 * If the logger can not fit all the data, block until it can fit them all
 */
void logger_write(Logger* logger, logger_data_t* buffer, int length) {
	// writing should be as fast as possible; thus, the lock is acquired once for the entire write

	// wait until the buffer is empty enough
	pthread_mutex_lock(&(logger->lock));
	while (logger->max_size - logger->size < length)
		pthread_cond_wait(&(logger->buffer_full));

	// first, copy data until the end of the buffer of the logger
	int first_part_length = length;
	logger_data_t* new_head = logger->head + length;
	if ((logger->head - logger->buffer) + length >= logger->max_size) {
		first_part_length = logger->max_size - (logger->head - logger->buffer);
		new_head = logger->buffer;
	}
	memcpy(logger->head, buffer, first_part_length * sizeof(logger_data_t));
	logger->head = new_head;

	// next, copy the rest of the data (if any left)
	memcpy(logger->head, buffer + first_part_length, (length - first_part_length) * sizeof(logger_data_t));

	// update counters and signal condition variables
	logger->size += length;
	if (logger->size > EMPTY_LOG_THREASHOLD)
		pthread_cond_signal(&(logger->buffer_empty));

	pthread_mutex_unlock(&(logger->lock));
}

/*
 * Read `length` data units from the logger to the buffer.
 * If the logger does not have `length` data units, block until it has enough data
 */
void logger_read(Logger* logger, logger_data_t* buffer, int length) {
	// TODO
}

/*
 * Free the logger
 */
void logger_free(Logger* logger) {
	//TODO
}
