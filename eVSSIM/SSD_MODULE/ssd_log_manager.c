// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"

int servSock;
int clientSock;
int clientSock2;

int g_server_create = 0;
int g_init_log_server = 0;

void INIT_LOG_MANAGER(void)
{
#ifdef MONITOR_ON
	if (g_init_log_server == 0){
		if (popen("./ssd_monitor", "r") == NULL){
			perror("popen");
			PERR("fwrite\n");
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
	PDBG_MNT("SERVER THREAD CREATED!!!\n");

	unsigned int len;
	//unsigned int len2;
	struct sockaddr_in serverAddr;
	struct sockaddr_in clientAddr;
	//struct sockaddr_in clientAddr2;

	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		RDBG_MNT(, "Server Socket Creation error!!!\n");

	int flags = fcntl(servSock, F_GETFL, 0);
	fcntl(servSock, F_SETFL, flags | O_APPEND);

	int option = 1;
	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
	memset(&serverAddr, 0x00, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(9999);

	if (bind(servSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
		RDBG_MNT(, "Server Socket Bind Error!!!\n");

	if (listen(servSock, 100) < 0)
		RDBG_MNT(, "Server Socket Listen Error!!!\n");

	PDBG_MNT("Wait for client....[%d]\n", servSock);

	clientSock = accept(servSock, (struct sockaddr*) &clientAddr, &len);
	PDBG_MNT("Connected![%d]\n", clientSock);
	PDBG_MNT("Error No. [%d]\n", errno);

	g_server_create = 1;
}

void THREAD_CLIENT(void *arg)
{
	int a = *(int*)arg;
	PDBG_MNT("ClientSock[%d]\n", a);
	send(a, "test\n", 5, 0);
}
