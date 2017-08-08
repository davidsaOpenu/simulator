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
	//TODO
}

/*
 * Return the number of data units available for reading in the logger
 */
int logger_size(Logger* logger) {
	int size = logger->head - logger->tail;
	if (size < 0)
		size += logger->buffer_size;
	return size;
}

/*
 * Read `length` data units from the logger to the buffer.
 * If the logger does not have `length` data units, block until it is has enough data
 */
void logger_read(Logger* logger, logger_data_t* buffer, int length) {
	//TODO
}

/*
 * Free the logger
 */
void logger_free(Logger* logger) {
	//TODO
}
