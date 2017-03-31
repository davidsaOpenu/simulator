/*
 * ThreadControl.h
 *
 *  Created on: Mar 14, 2017
 *      Author: Alexey Trifonov.
 */

#ifndef THREAD_CONTROL_H_
#define THREAD_CONTROL_H_

typedef int (*thread_execution_function)(void* pHandle );

typedef enum { UPDATE_THREAD_INFO_TYPE_ENABLE , UPDATE_THREAD_INFO_TYPE_EXIT , UPDATE_THREAD_INFO_TYPE_LIB_PATH , UPDATE_THREAD_INFO_TYPE_ENTRY_POINT , UPDATE_THREAD_INFO_TYPE_CUSTOM_DATA , UPDATE_THREAD_INFO_TYPE_SENTINEL } teUpdateThreadInfoType;

typedef enum { ERROR_NONE , ERROR_INPUT_PARAM  = -1, ERROR_OS_RESOURCES  = -2, ERROR_OPEN_SOCKET  = -2} tsErrorList;

int        ThreadControl_Init( unsigned int iNumberOfThreads , unsigned int* pvStackSizesList );
int        ThreadControl_Update( unsigned int number , teUpdateThreadInfoType type , ... );
void       ThreadControl_Close();

int        SendMessage( void* pHandle , unsigned int uiThreadNumber , void *pData , unsigned int uiSize );
int        FetchMessage( void* pHandle , unsigned int uiReadSize , void *pData , unsigned int uiDataSize );

#endif /* THREAD_CONTROL_H_ */
