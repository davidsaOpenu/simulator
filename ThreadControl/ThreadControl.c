/*
 ============================================================================
 Name        : ThreadControl.c
 Author      : Alexy T.
 Version     :
 Copyright   : 
 Description : Thread Control
 ============================================================================
 */

#include "ThreadControl.h"
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>

typedef struct THREAD_INFO_ITEM
{
	unsigned int				number,				/// user defined serial number of thread
						enabled,			/// Is enabled to run
						exit;				/// force stop run thread

	int					error;				/// error from thread

	unsigned int				stack_size;

	char*					library_path;			/// the share library

	char*					entry_point;			/// the entry point of share library

	void*					lib_handle;			/// share library handle

	struct sockaddr_in			socket_info;			/// thread income message socket info

	int					fd_socket;			/// income socket file descriptor

	pthread_t				thread_id;			/// the OS ID number of thread

	pthread_mutex_t				lock;				/// access protection

	pthread_attr_t 				attr;				/// tread attributes

	struct THREAD_INFO_ITEM*		next;				/// next item of info

} tsThreadInfoItem;

typedef enum { GET_THREAD_INFO_TYPE_SN , GET_THREAD_INFO_TYPE_ID , GET_THREAD_INFO_TYPE_SENTINEL } teGetThreadInfoType;

tsThreadInfoItem*	gThreadsInfoList;

int			CreateThreadInfoList( tsThreadInfoItem** list , unsigned int iNumberOfThreads , unsigned int* pvStackSizesList );
tsThreadInfoItem*	GetThreadInfoItem( tsThreadInfoItem *list , teGetThreadInfoType type , ... );
void			FreeThreadInfoList( tsThreadInfoItem* list );


int main(void)
{
	return EXIT_SUCCESS;
}

/**
 * @brief thread_function
 *
 * This is a thread function, that will be executed by "ThreadControl_Init" function.
 * The thread function is a sample space for soft start of target thread function from
 * the share library.
 *
 * @see ThreadControl_Init
 *
 * @return Returns final status.
 */
static void*	thread_function(void* arg)
{
   tsThreadInfoItem*			current 	= arg;

   unsigned int					exit 		= 0;

   thread_execution_function	function 	= NULL;

   int							error		= 0;

   if ( current == NULL )
	   return NULL;

   while( exit == 0 )
   {
	   exit = current->exit;

	   if ( current->enabled == 0 )
		   continue;

	   if (( current->lib_handle == NULL ) && ( function == NULL ))
	   {
		   if ( current->entry_point && current->library_path )
		   {
			   if ( current->lib_handle == NULL )
				   current->lib_handle = dlopen( current->library_path , 0 );

			   if ( current->lib_handle )
			   {
				   function = (thread_execution_function*) dlsym( current->lib_handle , current->entry_point );

			   }
			   else
			   {

			   }
		   }
	   }
	   else
	   {
		   error = function(current);
	   }
   }

   return NULL;
}

/**
 * @brief ThreadControl_Init
 *
 * This method is Initiates all threads environment for all threads.
 * The function is not start the target thread, only prepare for running.
 *
 * @param[in] iNumberOfThreads (unsigned int) - The number of threads.
 * @param[in] pvStackSizesList (unsigned int*) - A list of thread stack memory size for each thread ( can be NULL ).
 *
 * @return status of initialization ( 0 - indicates a success ).
 *
 * @see ThreadControl_Update , ThreadControl_Close
 */
int	ThreadControl_Init( unsigned int iNumberOfThreads , unsigned int* pvStackSizesList )
{
	if ( CreateThreadInfoList( &gThreadsInfoList , iNumberOfThreads , pvStackSizesList ) < 0 )
		return -1;

	return 0;
}

/**
 * @brief ThreadControl_Update
 *
 * This method is updates thread configuration parameters.
 *
 * @param[in] number (unsigned int) - The number of threads.
 * @param[in] type (teUpdateThreadInfoType) - A list of thread stack memory size for each thread ( can be NULL ).
 *
 * Types of parameters:
 * 1. UPDATE_THREAD_INFO_TYPE_ENABLE 	  ( int ) - starts the target thread
 * 2. UPDATE_THREAD_INFO_TYPE_EXIT   	  ( int ) - kill the support thread ( not for external use )
 * 3. UPDATE_THREAD_INFO_TYPE_LIB_PATH 	  ( char* ) - The share library path name
 * 4. UPDATE_THREAD_INFO_TYPE_ENTRY_POINT ( char* ) - The thread function to run ( int (*func)(void* ) )
 *
 * @return status of initialization ( 0 - indicates a success ).
 *
 * @see ThreadControl_Update , ThreadControl_Close
 */
int	ThreadControl_Update( unsigned int number , teUpdateThreadInfoType type , ... )
{
	tsThreadInfoItem*	current = NULL;

	va_list				args;

	char*				text = NULL;

	current = GetThreadInfoItem( gThreadsInfoList , number );

	if ( current == NULL )
		return -1;

	va_start( args , type );

	switch( type )
	{
		case UPDATE_THREAD_INFO_TYPE_ENABLE:
			current->enabled = va_arg(args,int);
			break;
		case UPDATE_THREAD_INFO_TYPE_EXIT:
			current->exit = va_arg(args,int);
			break;
		case UPDATE_THREAD_INFO_TYPE_LIB_PATH:
			text = va_arg(args,char*);
			if ( text )
			{
				current->library_path = malloc((size_t)strlen(text)+1);
				strcpy(current->library_path,text);
			}
			break;
		case UPDATE_THREAD_INFO_TYPE_ENTRY_POINT:
			text = va_arg(args,char*);
			if ( text )
			{
				current->entry_point = malloc((size_t)strlen(text)+1);
				strcpy(current->entry_point,text);
			}
			break;
	}

	va_end(args);

	return 0;
}

/**
 * @brief ThreadControl_Close
 *
 * This method discard all allocated resources to running threads.
 *
 */
void	ThreadControl_Close()
{
	FreeThreadInfoList( gThreadsInfoList );
	gThreadsInfoList = NULL;

	return;
}

/**
 * @brief CreateThreadInfoList
 *
 * This method is Initiates all threads environment for all threads.
 * The function is not start the target thread, only prepare for running.
 *
 * @param[in] list ( tsThreadInfoItem** ) - The global memory for linked list
 * @param[in] iNumberOfThreads (unsigned int) - The number of threads.
 * @param[in] pvStackSizesList (unsigned int*) - A list of thread stack memory size for each thread ( can be NULL ).
 *
 * @return status of initialization ( 0 - indicates a success ).
 *
 * @see GetThreadInfoItem , FreeThreadInfoList
 */
int	CreateThreadInfoList( tsThreadInfoItem** list , unsigned int iNumberOfThreads , unsigned int* pvStackSizesList )
{
	tsThreadInfoItem*	current = NULL;

	void*				attr = NULL;

	unsigned int*		pvStackSizesListNext = NULL;

	if ( iNumberOfThreads == 0 )
		return 0;

	current = calloc(1,sizeof(gThreadsInfoList));

	if ( current == NULL )
		return -1;

	if ( pthread_attr_init(&current->attr) != 0 )
		return -1;

	if ( pthread_mutex_init(&current->lock, NULL) != 0 )
		return -1;

	if ( pvStackSizesList )
	{
		pvStackSizesListNext = pvStackSizesList + 1;

		if ( pthread_attr_setstacksize(&attr, *pvStackSizesList ) != 0 )
			return -1;
	}

	current->fd_socket = socket( AF_INET, SOCK_DGRAM, 0 );

	if ( current->fd_socket < 0 )
		return -1;

	current->socket_info.sin_family = AF_INET;
	current->socket_info.sin_port = 0;
	current->socket_info.sin_addr.s_addr = 0;

	if ( bind( current->fd_socket , &current->socket_info , sizeof(current->socket_info)) < 0 )
		return -1;

	if ( pthread_create( &current->thread_id , attr, thread_function , (void*) current ) != 0 )
		return -1;

	*list = current;

	return CreateThreadInfoList( &(current->next) , iNumberOfThreads-1 , pvStackSizesListNext );
}

/**
 * @brief GetThreadInfoItem
 *
 * This method retrieves thread info item from list.
 *
 * @param[in] list ( tsThreadInfoItem* ) - The global memory for linked list
 * @param[in] type (teGetThreadInfoType) - The type of key.
 * @param[in] key ( unsigned int ) - The key for search.
 *
 * Types of parameters:
 * 1. GET_THREAD_INFO_TYPE_SN  ( unsigned int ) - search by number
 * 2. GET_THREAD_INFO_TYPE_ID  ( unsigned int ) - search by thread ID
 *
 * @return (tsThreadInfoItem*) of initialization ( 0 - indicates a success ).
 *
 */
tsThreadInfoItem*	GetThreadInfoItem( tsThreadInfoItem *list , teGetThreadInfoType type , ... )
{
	tsThreadInfoItem	*current	= list;

	int					parameter	= 0;

	int					bFound		= 0;

	va_list				args;

	if ( list == NULL )
		return current;

	va_start( args , type );

	switch( type )
	{
		case GET_THREAD_INFO_TYPE_SN:
		case GET_THREAD_INFO_TYPE_ID:
			parameter= va_arg(args,int);
			break;
	}

	while(current)
	{
		switch( type )
		{
			case GET_THREAD_INFO_TYPE_SN:

				if ( current->number == parameter )
					bFound = 1;
				break;

			case GET_THREAD_INFO_TYPE_ID:

				if ( current->thread_id == parameter )
					bFound = 1;
				break;
		}

		if ( bFound )
			break;

		current = current->next;
	}

	va_end(args);

	if ( bFound == 0 )
		return NULL;

	return current;
}

/**
 * @brief FreeThreadInfoList
 *
 * This method discard all allocated resources to running threads.
 *
 */
void	FreeThreadInfoList( tsThreadInfoItem *list )
{
	tsThreadInfoItem*	current = list,
						*last = current;

	while(current)
	{
		current = current->next;

		current->exit = 1;

		pthread_attr_destroy(&current->attr);

		pthread_cancel(current->thread_id);

		pthread_mutex_destroy(&current->lock);

		if(last->entry_point)
			free(last->entry_point);

		free(last);
		last = current;
	}

	return;
}
