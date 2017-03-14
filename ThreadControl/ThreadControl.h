/*
 * ThreadControl.h
 *
 *  Created on: Mar 14, 2017
 *      Author: root
 */

#ifndef THREADCONTROL_H_
#define THREADCONTROL_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <pthread.h>

typedef int (*thread_execution_function)(void* pHandle );

typedef enum { UPDATE_THREAD_INFO_TYPE_ENABLE , UPDATE_THREAD_INFO_TYPE_EXIT , UPDATE_THREAD_INFO_TYPE_LIB_PATH , UPDATE_THREAD_INFO_TYPE_ENTRY_POINT , UPDATE_THREAD_INFO_TYPE_SENTINEL } teUpdateThreadInfoType;

int		ThreadControl_Init( unsigned int iNumberOfThreads , unsigned int* pvStackSizesList );
int		ThreadControl_Update( unsigned int number , teUpdateThreadInfoType type , ... );
void	ThreadControl_Close();

#endif /* THREADCONTROL_H_ */
