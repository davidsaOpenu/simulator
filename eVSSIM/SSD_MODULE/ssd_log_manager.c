// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "ssd_log_manager.h"
#include "common.h"

#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>

int servSock;
int clientSock;
int clientSock2;

int g_server_create = 0;
int g_init_log_server = 0;
FILE *monitor = NULL;

void INIT_LOG_MANAGER(void)
{
#ifdef MONITOR_ON
	if(g_init_log_server == 0){
		if ((monitor = popen("./ssd_monitor", "r")) == NULL){
			perror("popen");
			printf("ERROR[%s] fwrite\n",__FUNCTION__);
		}
		THREAD_SERVER(NULL);

		g_init_log_server = 1;
	}
#endif
}
void TERM_LOG_MANAGER(void)
{
#ifdef MONITOR_ON
	close(servSock);
	close(clientSock);
	close(clientSock2);
	pclose(monitor);
#endif
}

void WRITE_LOG(const char *fmt, ...)
{
#ifdef MONITOR_ON
	if (g_server_create == 0)
		RERR(, "write log is failed\n");

	char szLog[1024];
	va_list argp;
	va_start(argp, fmt);
	vsprintf(szLog, fmt, argp);
	send(clientSock, szLog, strlen(szLog), 0);
	send(clientSock, "\n", 1, MSG_DONTWAIT);
	va_end(argp);
#endif
}

void THREAD_SERVER(void* arg)
{
	//int n;
	//char buffer[256];
#ifdef MNT_DEBUG
	printf("[%s] SERVER THREAD CREATED!!!\n",__FUNCTION__);
#endif
	unsigned int len;
	//unsigned int len2;
	struct sockaddr_in serverAddr;
	struct sockaddr_in clientAddr;
	//struct sockaddr_in clientAddr2;

	if((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
#ifdef MNT_DEBUG
		printf("[%s] Server Socket Creation error!!!\n",__FUNCTION__);
#endif
	}

	int flags = fcntl(servSock, F_GETFL, 0);

	fcntl(servSock, F_SETFL, flags | O_APPEND);

	int option = 1;
	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
	memset(&serverAddr, 0x00, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(9999);
	
	if(bind(servSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
#ifdef MNT_DEBUG
		printf("[%s] Server Socket Bind Error!!!\n",__FUNCTION__);
		return;
#endif
	}

	if(listen(servSock, 100)<0){
#ifdef MNT_DEBUG
		printf("[%s] Server Socket Listen Error!!!\n",__FUNCTION__);
#endif
	}
#ifdef MNT_DEBUG
	printf("[%s] Wait for client....[%d]\n", __FUNCTION__, servSock);
#endif
	clientSock = accept(servSock, (struct sockaddr*) &clientAddr, &len);
#ifdef MNT_DEBUG
	printf("[%s] Connected![%d]\n",__FUNCTION__, clientSock);
	printf("[%s] Error No. [%d]\n",__FUNCTION__, errno);
#endif	

	g_server_create = 1;
}

void THREAD_CLIENT(void* arg)
{
	int a = *(int*)arg;
	//	int a = (int)arg;
#ifdef MNT_DEBUG
	printf("[%s] ClientSock[%d]\n",__FUNCTION__, a);
#endif
	send(a, "test\n", 5, 0);
}

