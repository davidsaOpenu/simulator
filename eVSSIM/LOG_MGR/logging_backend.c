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

/**
 * Return the number of empty bytes in logger_node
 * @param {Logger_Pool_Node*} logger node to chcek
 * @return the number of empty bytes in logger_node
 */
#define LOGGER_NODE_EMPTY_BYTES(logger_node) (LOGGER_BUFFER_POOL_SIZE - (logger_node->head - logger_node->buffer))

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
			cur->last_used_timestamp = time(NULL);
			continue;
		}

		priv = cur;
		priv->next = cur =  logger_pool_node;
		cur->priv = priv;
		cur->next = NULL;
		cur->head = cur-> tail = cur->buffer = buffer;
		cur->last_used_timestamp = time(NULL);

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
	logger_pool->number_of_allocated_nodes = logger_pool->number_of_free_pool_nodes = number_of_nodes;
	logger_pool->total_number_of_nodes = number_of_nodes;

	return logger_pool;
}

int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length) {
	Byte* mem_buf;
	int res=0, number_of_bytes_to_write=0;

	// check if there is a free node to write to
	// OR if there is not enough space in the node
	// for the amount of data that is going to be written
	if(logger_pool->number_of_free_pool_nodes == 0 || (LOGGER_NODE_EMPTY_BYTES(logger_pool->free_pool_node) < length))
	{
		Logger_Pool_Node* logger_pool_node = (Logger_Pool_Node*) malloc(sizeof(Logger_Pool_Node));
		if(logger_pool_node == NULL)
			return 1;

		if (posix_memalign((void**) &mem_buf, LOGGER_BUFFER_ALIGNMENT, LOGGER_BUFFER_POOL_SIZE))
			return 1;

		logger_pool_node->head = logger_pool_node->tail = logger_pool_node->buffer = mem_buf;
		logger_pool_node->next = NULL;
		logger_pool_node->priv = logger_pool->tail_pool_node;

		logger_pool->tail_pool_node->next = logger_pool_node;
		logger_pool->tail_pool_node = logger_pool_node;
		logger_pool->number_of_free_pool_nodes++;
		logger_pool->total_number_of_nodes++;

		// if we allocated new node because there is no free nodes
		// we need to point free_pool_node to logger_pool_node
		if(logger_pool->number_of_free_pool_nodes == 0)
			logger_pool->free_pool_node = logger_pool_node;
	}

	if(LOGGER_NODE_EMPTY_BYTES(logger_pool->free_pool_node) > length) // write all data to free_pool_node
	{
		memcpy((void*) logger_pool->free_pool_node->head, (void*) buffer, length);
		logger_pool->free_pool_node->head += length;
		logger_pool->free_pool_node->last_used_timestamp = time(NULL);
	}
	else
	{
		// write as much bytes as we can to free_pool_node
		number_of_bytes_to_write = LOGGER_NODE_EMPTY_BYTES(logger_pool->free_pool_node);
		memcpy((void*) logger_pool->free_pool_node->head, (void*) buffer, number_of_bytes_to_write);
		logger_pool->free_pool_node->head += number_of_bytes_to_write;
		logger_pool->free_pool_node->last_used_timestamp = time(NULL);

		// write the rest of the bytes to the next free_pool_node
		logger_pool->free_pool_node = tail_pool_node;
		memecpy((void*) logger_poll->free_pool_node->head, (void*)(buffer+number_of_bytes_to_write),
				(length-number_of_bytes_to_write));
		logger_pool->free_pool_node->head += (length-number_of_bytes_to_write);
		logger_pool->free_pool_node->last_used_timestamp = time(NULL);
	}

	return res;
}

int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length) {
	for(int i=length; i>0; i-=LOGGER_BUFFER_POOL_SIZE)
	{
		if(i >= LOGGER_BUFFER_POOL_SIZE)
			memcpy((void*) buffer, (void*) logger_pool->buffer, LOGGER_BUFFER_POOL_SIZE);
		else
			memcpy((void*) buffer, (void*) logger_pool->buffer, i);
	}
	return length;
}


void logger_free(Logger_Pool* logger_pool) {
	Logger_Pool_Node* logger_pool_node = logger_pool->head_pool_node;
	Logger_Pool_Node* temp;
	while(logger_pool_node != NULL)
	{
		// free the logger_pool buffer
		free(logger_pool_node->buffer);

		temp = logger_pool_node;
		logger_pool_node = logger_pool_node->next;

		// free the logger_pool_node
		free(temp);
	}

	// free Logger_Pool
	free(logger_pool);
}

void logger_pool_reduce_size(Logger_Pool* logger_pool) {
	while(logger_pool->total_number_of_nodes > logger_pool->number_of_allocated_nodes)
	{
		// lets find a node that wasn't wrriten for X time
		// TODO - how do we know if we can free this node ?
		// maybe some one will want to read it later ?
	}
}
