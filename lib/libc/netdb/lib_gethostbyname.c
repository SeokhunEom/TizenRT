/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
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
/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gethostbyname
 *
 * Description:
 *   Returns an entry containing addresses of address family AF_INET
 *   for the host with name name.
 *   Due to dns_gethostbyname limitations, only one address is returned.
 *
 * Input Parameters:
 *   name - the hostname to resolve
 *
 * Returned Value:
 *   an entry containing addresses of address family AF_INET
 *   for the host with name
 *
 ****************************************************************************/

/****************************************************************************
 * Name: gethostbyname
 ****************************************************************************/

#ifdef CONFIG_NET_LWIP_NETDB

#ifndef DNS_MAX_NAME_LENGTH
#define DNS_MAX_NAME_LENGTH 256
#endif

char g_name[DNS_MAX_NAME_LENGTH + 1];
struct in_addr g_hostent_addr;
char *g_aliases = NULL;
struct in_addr *g_phostent_addr[2] = {&g_hostent_addr, NULL};
struct hostent g_hent = {g_name, &g_aliases, 0, 0, (char **)&g_phostent_addr};

struct hostent *gethostbyname(const char *name)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct sockaddr_in *address;

	if (!name) {
		return NULL;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(name, NULL, &hints, &result) != 0 ||
		!result || !result->ai_addr) {
		if (result) {
			freeaddrinfo(result);
		}
		return NULL;
	}

	address = (struct sockaddr_in *)result->ai_addr;
	g_hostent_addr = address->sin_addr;
	strncpy(g_name, name, DNS_MAX_NAME_LENGTH);
	g_name[DNS_MAX_NAME_LENGTH] = '\0';
	g_hent.h_addrtype = AF_INET;
	g_hent.h_length = sizeof(struct in_addr);
	freeaddrinfo(result);
	return &g_hent;
}
#endif
