/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/* A bounded TCP/UDP peer for loopback and external network runtime checks.
 * Both ends exchange the same six binary patterns. TCP handles short I/O;
 * UDP requires one complete datagram per pattern, including its source.
 */
#include <tinyara/config.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static const unsigned int g_peer_sizes[] = {1, 3, 64, 513, 1024, 1484};

static int peer_timeout(int fd)
{
	struct timeval timeout = {5, 0};
	return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ? -1 : 0;
}

static int peer_stream(int fd, uint8_t *buffer, size_t length, int sending)
{
	size_t done = 0;
	while (done < length) {
		ssize_t n = sending ? send(fd, buffer + done, length - done, 0) : recv(fd, buffer + done, length - done, 0);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			return -1;
		}
		done += n;
	}
	return 0;
}

int tc_network_peer_main(int argc, char *argv[])
{
	int fd = -1;
	int listener = -1;
	int ret = -1;
	uint8_t *buffer = NULL;
	struct sockaddr_in address;
	unsigned int bytes = 0;
	int tcp;
	int client;
	char *end;
	long port;

	if (argc != 5 || (strcmp(argv[1], "tcp") && strcmp(argv[1], "udp")) ||
		(strcmp(argv[2], "client") && strcmp(argv[2], "server"))) {
		printf("Usage: network_peer tcp|udp client|server IPv4-address port\n");
		return -1;
	}
	tcp = !strcmp(argv[1], "tcp");
	client = !strcmp(argv[2], "client");
	port = strtol(argv[4], &end, 10);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	if (!argv[4][0] || *end || port < 1 || port > 65535 || inet_pton(AF_INET, argv[3], &address.sin_addr) != 1) {
		goto out;
	}
	address.sin_port = htons(port);
	/* Heap storage keeps both TASH and pthread stack requirements bounded. */
	buffer = malloc(1485);
	if (!buffer) {
		goto out;
	}
	fd = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (fd < 0 || peer_timeout(fd) < 0) {
		goto out;
	}
	if (client) {
		if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
			goto out;
		}
	} else {
		int reuse = 1;
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
			bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
			goto out;
		}
		if (tcp && listen(fd, 1) < 0) {
			goto out;
		}
		printf("NETPEER READY %s server\n", argv[1]);
		if (tcp) {
			listener = fd;
			fd = accept(listener, NULL, NULL);
			if (fd < 0 || peer_timeout(fd) < 0) {
				goto out;
			}
		}
	}

	for (unsigned int round = 0; round < sizeof(g_peer_sizes) / sizeof(g_peer_sizes[0]); round++) {
		unsigned int length = g_peer_sizes[round];
		struct sockaddr_in source;
		socklen_t source_len = sizeof(source);
		if (client) {
			for (unsigned int i = 0; i < length; i++) {
				buffer[i] = (i * 37 + round * 13) & 255;
			}
			if (tcp ? peer_stream(fd, buffer, length, 1) < 0 : send(fd, buffer, length, 0) != length) {
				goto out;
			}
		}
		if (tcp) {
			if (peer_stream(fd, buffer, length, 0) < 0) {
				goto out;
			}
		} else {
			ssize_t n = recvfrom(fd, buffer, 1485, 0, (struct sockaddr *)&source, &source_len);
			if (n != length || source_len != sizeof(source) || source.sin_family != AF_INET ||
				(client && (source.sin_addr.s_addr != address.sin_addr.s_addr || source.sin_port != address.sin_port))) {
				goto out;
			}
		}
		for (unsigned int i = 0; i < length; i++) {
			if (buffer[i] != ((i * 37 + round * 13) & 255)) {
				errno = EIO;
				goto out;
			}
		}
		if (!client && (tcp ? peer_stream(fd, buffer, length, 1) < 0 :
			sendto(fd, buffer, length, 0, (struct sockaddr *)&source, source_len) != length)) {
			goto out;
		}
		bytes += length;
	}
	ret = 0;
out:
	if (ret == 0) {
		printf("NETPEER PASS %s %s rounds=6 bytes=%u\n", argv[1], argv[2], bytes);
	} else {
		printf("NETPEER FAIL %s %s errno=%d\n", argv[1], argv[2], errno);
	}
	if (fd >= 0) {
		close(fd);
	}
	if (listener >= 0) {
		close(listener);
	}
	free(buffer);
	return ret;
}
