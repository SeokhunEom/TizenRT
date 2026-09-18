/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/// @file tc_net_connect.c
/// @brief Test Case Example for connect() API
#include <tinyara/config.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netutils/netlib.h>
#include <sys/socket.h>

#include "tc_internal.h"

/**
 * @testcase		   :tc_net_connect_fd_n
 * @brief		   :
 * @scenario		   :
 * @apicovered	   :connect()
 * @precondition	   :
 * @postcondition	   :
 */
static void tc_net_connect_fd_n(struct sockaddr_in *sa)
{
	int ret = inet_pton(AF_INET, "192.168.1.3", &(sa->sin_addr));
	if (ret < 0) {
		printf("[ERR] %s\t%s:%d\n", __FUNCTION__, __FILE__, __LINE__);
		return;
	}

	ret = connect(0, (struct sockaddr *)sa, sizeof(struct sockaddr_in));

	TC_ASSERT_EQ("connect", ret, -1);
	TC_SUCCESS_RESULT();
}

/**
 * @testcase		   :tc_net_connect_broadcastaddr_n
 * @brief		   :
 * @scenario		   :
 * @apicovered	   :connect()
 * @precondition	   :
 * @postcondition	   :
 */
static void tc_net_connect_broadcastaddr_n(struct sockaddr_in *sa)
{
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		printf("socket creation error line:%d\n", __LINE__);
		return;
	}
	struct in_addr ad;

	ad.s_addr = htonl(INADDR_BROADCAST);
	sa->sin_addr = ad;
	/* Invalid TCP destinations must be rejected, not enter SYN retry. */
	TC_ASSERT_EQ_CLEANUP("fcntl", fcntl(fd, F_SETFL, O_NONBLOCK), 0, close(fd));
	int ret = connect(fd, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
	int saved_errno = errno;
	close(fd);

	TC_ASSERT_EQ("connect", ret, -1);
	TC_ASSERT_EQ("connect broadcast errno", saved_errno, EINVAL);
	TC_SUCCESS_RESULT();
}

/**
 * @testcase		   :tc_net_connect_loopbackaddr_n
 * @brief		   :
 * @scenario		   :
 * @apicovered	   :connect()
 * @precondition	   :
 * @postcondition	   :
 */
static void tc_net_connect_loopbackaddr_n(struct sockaddr_in *sa)
{
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		printf("socket creation error line:%d\n", __LINE__);
		return;
	}

	struct in_addr ad;
	ad.s_addr = htonl(INADDR_LOOPBACK);
	sa->sin_addr = ad;
	int ret = connect(fd, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
	close(fd);

	TC_ASSERT_EQ("connect", ret, -1);
	TC_SUCCESS_RESULT();
}

/**
 * @testcase		   :tc_net_connect_socklen_n
 * @brief		   :
 * @scenario		   :
 * @apicovered	   :connect()
 * @precondition	   :
 * @postcondition	   :
 */
static void tc_net_connect_socklen_n(struct sockaddr_in *sa)
{
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		printf("socket creation error line:%d\n", __LINE__);
		return;
	}
	struct in_addr ad;
	ad.s_addr = htonl(INADDR_LOOPBACK);
	sa->sin_addr = ad;
	int ret = connect(fd, (struct sockaddr *)sa, -1);
	close(fd);

	TC_ASSERT_EQ("connect", ret, -1);
	TC_SUCCESS_RESULT();
}

/* Nonblocking mode makes a missing validation fail without waiting for the
 * TCP SYN retry timer. Both IPv4 and IPv6 multicast must be rejected.
 */
static void tc_net_connect_multicastaddr_n(void)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(1100);
	inet_pton(AF_INET, "224.0.0.1", &sa.sin_addr);
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	TC_ASSERT_GEQ("socket", fd, 0);
	TC_ASSERT_EQ_CLEANUP("fcntl", fcntl(fd, F_SETFL, O_NONBLOCK), 0, close(fd));
	int ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	int saved_errno = errno;
	close(fd);
	TC_ASSERT_EQ("connect", ret, -1);
	TC_ASSERT_EQ("connect multicast errno", saved_errno, EINVAL);
	TC_SUCCESS_RESULT();
}

#ifdef CONFIG_NET_IPv6
static void tc_net_connect_ipv6_multicastaddr_n(void)
{
	struct sockaddr_in6 sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_port = htons(1100);
	inet_pton(AF_INET6, "ff02::1", &sa.sin6_addr);
	int fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	TC_ASSERT_GEQ("socket", fd, 0);
	TC_ASSERT_EQ_CLEANUP("fcntl", fcntl(fd, F_SETFL, O_NONBLOCK), 0, close(fd));
	int ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	int saved_errno = errno;
	close(fd);
	TC_ASSERT_EQ("connect", ret, -1);
	TC_ASSERT_EQ("connect IPv6 multicast errno", saved_errno, EINVAL);
	TC_SUCCESS_RESULT();
}
#endif

/****************************************************************************
 * Name: connect()
 ****************************************************************************/
int net_connect_main(void)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof sa);

	sa.sin_family = AF_INET;
	sa.sin_port = htons(1100);

	tc_net_connect_fd_n(&sa);
	tc_net_connect_broadcastaddr_n(&sa);
	tc_net_connect_loopbackaddr_n(&sa);
	tc_net_connect_socklen_n(&sa);
	tc_net_connect_multicastaddr_n();
#ifdef CONFIG_NET_IPv6
	tc_net_connect_ipv6_multicastaddr_n();
#endif
	
	return 0;
}
