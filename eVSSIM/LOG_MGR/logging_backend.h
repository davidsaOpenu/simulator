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


#include <stdlib.h>

// TODO - update this explantion
/*
 * The backend logging mechanism is implemented using one long array in memory, which contains a
 * circular array inside; The array is aligned in memory, for a better caching performance
 * WARNING: the logging implemented here supports only one consumer and one producer!!
 */


/**
 * The alignment to use when allocating the logger's buffer
 */
#define LOGGER_BUFFER_ALIGNMENT 64

// TODO - understand if this is needed here or we can use the define that allready exists (1 << 24)
// 0.5 mega byte logging area pool's
#define LOGGER_BUFFER_POOL_SIZE (1 << 19)

/**
 * A byte typedef, for easier use
 */
typedef unsigned char Byte;

/**
 * The Logger Pool Node structure
 * Each node represents a place in the memmory pool
 * Where the logger may write/read from.
 */
typedef struct {
	/**
	 * The main buffer of the logger node
	 */
	Byte* buffer;
	/**
	 * The next place to insert a byte at the buffer
	 */
	Byte* head;
	/**
	 * The next place to read a byte from the buffer
	 */
	Byte* tail;
	/**
	 * The next pool node
	 */
	Logger_Pool_Node* next;
	/**
	 * The previous pool node
	 */
	Logger_Pool_Node* priv;
} Logger_Pool_Node;

/**
 * The Logger Pool structure
 * Each logger pool holds number_of_ree_pool_nodes
 * That may be allocated for the looger to use
 */
typedef struct {
	/**
	 * Head node of Logger pool
	 */
	Logger_Pool_Node* head_pool_node;
	/**
	 * Tail node of Logger pool
	 */
	Logger_Pool_Node* tail_pool_node;
	/**
	 * Next free node of Logger pool
	 */
	Logger_Pool_Node* free_pool_node;
	/**
	 * Number of total free pool nodes in the Logger pool
	 */
	unsigned int number_of_free_pool_nodes;
} Logger_Pool;

/**
 * Create a new logger
 * @param number_of_nodes the number of nodes to allocate at this logger pool
 *  Due to the way posix_memalign works, size must be a power of two multiple of sizeof(void *)
 * @return the newly created logger
 */
Logger_Pool* logger_init(unsigned int number_of_nodes);

/**
 * Write a byte array to the logger
 * Print a warning to stderr if the logger cannot fit all the data
 * @param logger_pool struct that holds the logger node to write the bytes to
 * @param buffer the buffer to read the bytes from
 * @param length the number of bytes to copy from the buffer to the logger
 * @return 0 if the logger can fit all the data, otherwise nonzero
 */
int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length);

/**
 * Read a byte array from the logger
 * @param logger the logger to read the data from
 * @param buffer the buffer to write the data to
 * @param length the maximum number of bytes to read
 * @return the number of bytes read
 */
int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length);

/**
 * Free the logger
 * @param logger the logger to free
 */
void logger_free(Logger* logger);

#endif
