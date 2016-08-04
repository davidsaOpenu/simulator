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
int clientSock = 0;
int g_init_log_server = 0;
FILE *monitor = NULL;

void INIT_LOG_MANAGER(void)
{
#ifdef MONITOR_ON
	if(g_init_log_server == 0){
		if ((monitor = popen("./ssd_monitor", "r")) == NULL)
			PERR("popen failed: %s\n", strerror(errno));
		THREAD_SERVER();

		g_init_log_server = 1;
	}
#endif
}
void TERM_LOG_MANAGER(void)
{
#ifdef MONITOR_ON
	close(servSock);
	close(clientSock);
	pclose(monitor);
#endif
}

void WRITE_LOG(const char *fmt, ...)
{
#ifdef MONITOR_ON
	if (clientSock == 0)
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

void THREAD_SERVER(void)
{
#ifdef MONITOR_ON
    PDBG_MNT("Start\n");

    if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        RDBG_MNT(, "socket failed: %s\n", strerror(errno));
<<<<<<< HEAD

=======
	int flags = fcntl(servSock, F_GETFL, 0);

	fcntl(servSock, F_SETFL, flags | O_APPEND);
>>>>>>> afd5f7b... Object pass-through – simulator
    int option = 1;
    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        RDBG_MNT(, "setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0x00, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(9999);
  
    if (bind(servSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        RDBG_MNT(, "bind failed: %s\n", strerror(errno));

    if (listen(servSock, 100) < 0)
        RDBG_MNT(, "listen failed: %s\n", strerror(errno));

    PDBG_MNT("Wait for client....[%d]\n", servSock);

    if ((clientSock = accept(servSock, NULL, NULL)) < 0)
        RDBG_MNT(, "accept failed: %s\n", strerror(errno));
    PDBG_MNT("Connected![%d]\n", clientSock);
#endif
}

void THREAD_CLIENT(void *arg)
{
#ifdef MONITOR_ON
    int sock = *(int*)arg;
    PDBG_MNT("ClientSock[%d]\n", sock);
    send(sock, "test\n", 5, 0);
#endif
}

