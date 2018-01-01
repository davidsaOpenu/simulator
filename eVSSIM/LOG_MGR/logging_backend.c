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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging_backend.h"

//TODO - check if this is still needed
/**
 * Return the size of the logger (full slots only)
 * @param {Logger*} logger the logger to check
 * @return the size of the logger (in bytes)
 */
#define LOGGER_NODE_SIZE(logger) (((logger->head - logger->tail) + logger->buffer_size) % \
		logger->buffer_size)


Logger_Pool* logger_init(unsigned int number_of_nodes) {
	Byte* buffer;
	Logger_Pool_Node* head,tail,cur,priv;

	// try to allocate number_of_nodes logger pool nodes
	for(int i=0; i<number_of_nodes; i++) {
		Logger_Pool_Node* logger_pool_node = (Logger_Pool_Node*) malloc(sizeof(Logger_Pool_Node));
		if(logger_pool_node == NULL)
			return NULL;

		if (posix_memalign((void**) &buffer, LOGGER_BUFFER_ALIGNMENT, LOGGER_BUFFER_POOL_SIZE))
			return NULL;

		// if this is the first node allocation set correctly the Logger_Pool_Node structre
		// and save it as the head node
		if(i==0){
			head = tail = cur = priv = logger_pool_node;
			cur->priv = NULL;
			cur->next = NULL;
			cur->head = cur->tail = cur->buffer = buffer;
			continue;
		}

		priv = cur;
		priv->next = cur =  logger_pool_node;
		cur->priv = priv;
		cur->next = NULL;
		cur->head = cur-> tail = cur->buffer = buffer;

		// if this is the last node allocation save it as the tail node
		if(i == (number_of_areas-1))
			tail = cur;
	}

	// allocate Logger_Pool structre to hold and mange the pool of nodes
	// that were allocated above
	Logger_Pool* logger_pool = (Logger_Pool*) malloc(sizeof(Logger_Pool));
	if(logger_pool == NULL)
		return NULL;

	logger_pool->head_pool_node = head;
	logger_pool->tail_pool_node = tail;
	logger_pool->free_pool_node = head;
	logger_pool->number_of_free_pool_nodes = number_of_nodes;

	return logger_pool;
}

int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length) {
	Byte* mem_buf;
	int res=0, number_of_needed_pool_nodes=0;

	// calculate how many logger pool nodes we need to write this data
	number_of_needed_pool_nodes = (length / LOGGER_BUFFER_POOL_SIZE)+1;

	// TODO
	// check if there is a free node to write to
	// or if the amount of data that is going to be wrriten is bigger then
	// LOGGER_BUFFER_POOL_SIZE
	// we need to handle  a case where we start to write to one pool and then need to swap to
	// other pool
	if(logger_pool->number_of_free_pool_nodes < number_of_needed_pool_nodes) {
		// lets allocate number_of_needed_pool_nodes
		for(int i=0; i<number_of_needed_pool_nodes; i++)
		{
			Logger_Pool_Node* logger_pool_node = (Logger_Pool_Node*) malloc(sizeof(Logger_Pool_Node));
			if(logger_pool_node == NULL)
				return 1;

			if (posix_memalign((void**) &mem_buf, LOGGER_BUFFER_ALIGNMENT, LOGGER_BUFFER_POOL_SIZE))
				return 1;

			logger_pool_node->head = logger_pool_node->tail = logger_pool_node->buffer = mem_buf;
			logger_pool_node->next = NULL;
			logger_pool_node->priv = logger_pool->tail_pool_node;

			logger_pool->tail = logger_pool_node;
			logger_pool->number_of_free_pool_nodes++;
			if(i == 0)
				logger_pool->free_pool_node = logger_pool_node;
		}
	}

	if(!((logger_pool->free_pool_node->head - logger_pool->free_pool_node->buffer) + length >= LOGGER_BUFFER_POOL_SIZE))
	{
		memcpy((void*) logger_pool->free_pool_node->head, (void*) buffer, length);
		logger_pool->free_pool_node->head = logger_pool->free_pool_node->head + length;
	}
	// TODO - if the condition is not ture

	return res;
}

int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length) {
	// TODO
}


void logger_free(Logger* logger) {
	// TODO
}
