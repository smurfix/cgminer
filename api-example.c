/*
 * Copyright 2011 Kano
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "compat.h"
#include "miner.h"

#if defined(unix)
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>

	#define SOCKETTYPE int
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define CLOSESOCKET close

	#define SOCKETINIT {}

	#define SOCKERRMSG strerror(errno)
#endif

#ifdef WIN32
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define CLOSESOCKET closesocket

	static char WSAbuf[1024];

	struct WSAERRORS {
		int id;
		char *code;
	} WSAErrors[] = {
		{ 0,			"No error" },
		{ WSAEINTR,		"Interrupted system call" },
		{ WSAEBADF,		"Bad file number" },
		{ WSAEACCES,		"Permission denied" },
		{ WSAEFAULT,		"Bad address" },
		{ WSAEINVAL,		"Invalid argument" },
		{ WSAEMFILE,		"Too many open sockets" },
		{ WSAEWOULDBLOCK,	"Operation would block" },
		{ WSAEINPROGRESS,	"Operation now in progress" },
		{ WSAEALREADY,		"Operation already in progress" },
		{ WSAENOTSOCK,		"Socket operation on non-socket" },
		{ WSAEDESTADDRREQ,	"Destination address required" },
		{ WSAEMSGSIZE,		"Message too long" },
		{ WSAEPROTOTYPE,	"Protocol wrong type for socket" },
		{ WSAENOPROTOOPT,	"Bad protocol option" },
		{ WSAEPROTONOSUPPORT,	"Protocol not supported" },
		{ WSAESOCKTNOSUPPORT,	"Socket type not supported" },
		{ WSAEOPNOTSUPP,	"Operation not supported on socket" },
		{ WSAEPFNOSUPPORT,	"Protocol family not supported" },
		{ WSAEAFNOSUPPORT,	"Address family not supported" },
		{ WSAEADDRINUSE,	"Address already in use" },
		{ WSAEADDRNOTAVAIL,	"Can't assign requested address" },
		{ WSAENETDOWN,		"Network is down" },
		{ WSAENETUNREACH,	"Network is unreachable" },
		{ WSAENETRESET,		"Net connection reset" },
		{ WSAECONNABORTED,	"Software caused connection abort" },
		{ WSAECONNRESET,	"Connection reset by peer" },
		{ WSAENOBUFS,		"No buffer space available" },
		{ WSAEISCONN,		"Socket is already connected" },
		{ WSAENOTCONN,		"Socket is not connected" },
		{ WSAESHUTDOWN,		"Can't send after socket shutdown" },
		{ WSAETOOMANYREFS,	"Too many references, can't splice" },
		{ WSAETIMEDOUT,		"Connection timed out" },
		{ WSAECONNREFUSED,	"Connection refused" },
		{ WSAELOOP,		"Too many levels of symbolic links" },
		{ WSAENAMETOOLONG,	"File name too long" },
		{ WSAEHOSTDOWN,		"Host is down" },
		{ WSAEHOSTUNREACH,	"No route to host" },
		{ WSAENOTEMPTY,		"Directory not empty" },
		{ WSAEPROCLIM,		"Too many processes" },
		{ WSAEUSERS,		"Too many users" },
		{ WSAEDQUOT,		"Disc quota exceeded" },
		{ WSAESTALE,		"Stale NFS file handle" },
		{ WSAEREMOTE,		"Too many levels of remote in path" },
		{ WSASYSNOTREADY,	"Network system is unavailable" },
		{ WSAVERNOTSUPPORTED,	"Winsock version out of range" },
		{ WSANOTINITIALISED,	"WSAStartup not yet called" },
		{ WSAEDISCON,		"Graceful shutdown in progress" },
		{ WSAHOST_NOT_FOUND,	"Host not found" },
		{ WSANO_DATA,		"No host data of that type was found" },
		{ -1,			"Unknown error code" }
	};

	static char *WSAErrorMsg()
	{
		char *msg;
		int i;
		int id = WSAGetLastError();

		/* Assume none of them are actually -1 */
		for (i = 0; WSAErrors[i].id != -1; i++)
			if (WSAErrors[i].id == id)
				break;

		sprintf(WSAbuf, "Socket Error: (%d) %s", id, WSAErrors[i].code);

		return &(WSAbuf[0]);
	}

	#define SOCKERRMSG WSAErrorMsg()

	static WSADATA WSA_Data;

	#define SOCKETINIT	int wsa; \
				if (wsa = WSAStartup(0x0202, &WSA_Data)) { \
					printf("Socket startup failed: %d\n", wsa); \
					return 1; \
				}

	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif
#endif

#define RECVSIZE 65500

static const char SEPARATOR = '|';
static const char COMMA = ',';
static const char EQ = '=';

void display(char *buf)
{
	char *nextobj, *item, *nextitem, *eq;
	int itemcount;

	while (buf != NULL) {
		nextobj = strchr(buf, SEPARATOR);
		if (nextobj != NULL)
			*(nextobj++) = '\0';

		if (*buf) {
			item = buf;
			itemcount = 0;
			while (item != NULL) {
				nextitem = strchr(item, COMMA);
				if (nextitem != NULL)
					*(nextitem++) = '\0';

				if (*item) {
					eq = strchr(item, EQ);
					if (eq != NULL)
						*(eq++) = '\0';

					if (itemcount == 0)
						printf("[%s%s] =>\n(\n", item, (eq != NULL && isdigit(*eq)) ? eq : "");

					if (eq != NULL)
						printf("   [%s] => %s\n", item, eq);
					else
						printf("   [%d] => %s\n", itemcount, item);
				}

				item = nextitem;
				itemcount++;
			}
			if (itemcount > 0)
				puts(")");
		}

		buf = nextobj;
	}
}

int callapi(const char *command, const char *host, const char *port)
{
	char buf[RECVSIZE+1];
	struct hostent *ip;
	struct sockaddr_in serv;
	SOCKETTYPE sock;
	int ret = 0;
	int n, p;
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *ai;
	char *errstr = NULL;

	SOCKETINIT;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	
	ret = getaddrinfo(host, port, &hints, &res);
	if (ret) {
		printf("Resolving %s:%s: failed: %s\n", host, port, gai_strerror(ret));
		return 1;
	}

	/* Loop through the possible addresses. The first one is preferred,
	 * whatever that means, so errstr gets (a copy of) the first error we
	 * encounter. */
	for(ai = res; ai; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock == INVSOCK)
			continue;

		if (SOCKETFAIL(connect(sock,ai->ai_addr, ai->ai_addrlen))) {
			if (errstr == NULL)
				errstr = strdup(SOCKERRMSG);
			CLOSESOCKET(sock);
			sock = INVSOCK;
			continue;
		}

		break;
	}

	if (res)
		freeaddrinfo(res);

	if (sock == INVSOCK) {
		if (errstr == NULL)
			errstr = strdup(SOCKERRMSG);
		printf("Could not connect to %s:%s: %s\n", host,port, errstr);
		free(errstr);
		return 1;
	}
	if (errstr)
		free(errstr);

	n = send(sock, command, strlen(command), 0);
	if (SOCKETFAIL(n)) {
		printf("Send failed: %s\n", SOCKERRMSG);
		ret = 1;
	}
	else {
		p = 0;
		buf[0] = '\0';
		while (p < RECVSIZE) {
			n = recv(sock, &buf[p], RECVSIZE - p , 0);

			if (SOCKETFAIL(n)) {
				printf("Recv failed: %s\n", SOCKERRMSG);
				ret = 1;
				break;
			}

			if (n == 0)
				break;

			p += n;
			buf[p] = '\0';
		}

		printf("Reply was '%s'\n", buf);

		display(buf);
	}

	CLOSESOCKET(sock);

	return ret;
}

static char *trim(char *str)
{
	char *ptr;

	while (isspace(*str))
		str++;

	ptr = strchr(str, '\0');
	while (ptr-- > str) {
		if (isspace(*ptr))
			*ptr = '\0';
	}

	return str;
}

int main(int argc, char *argv[])
{
	const char *command = "summary";
	const char *host = "127.0.0.1";
	const char *port = "4028";
	char *ptr;

	if (argc > 1)
		if (strcmp(argv[1], "-?") == 0
		||  strcmp(argv[1], "-h") == 0
		||  strcmp(argv[1], "--help") == 0) {
			fprintf(stderr, "usAge: %s [command [ip/host [port]]]\n", argv[0]);
			return 1;
		}

	if (argc > 1) {
		ptr = trim(argv[1]);
		if (strlen(ptr) > 0)
			command = ptr;
	}

	if (argc > 2) {
		ptr = trim(argv[2]);
		if (strlen(ptr) > 0)
			host = ptr;
	}

	if (argc > 3) {
		ptr = trim(argv[3]);
		if (strlen(ptr) > 0)
			port = ptr;
	}

	return callapi(command, host, port);
}
