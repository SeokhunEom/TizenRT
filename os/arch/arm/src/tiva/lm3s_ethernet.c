/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
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
 * arch/arm/src/tiva/lm3s_ethernet.c
 *
 *   Copyright (C) 2009-2010, 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/* LM3S Ethernet FIFO driver for the TizenRT network manager. */

#include <tinyara/config.h>
#if defined(CONFIG_NET) && defined(CONFIG_TIVA_ETHERNET)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <debug.h>
#include <tinyara/arch.h>
#include <tinyara/irq.h>
#include <tinyara/wqueue.h>
#include <tinyara/net/ethernet.h>
#include <tinyara/net/if/ethernet.h>
#include <tinyara/netmgr/netdev_mgr.h>
#include <arch/board/board.h>
#include "chip.h"
#include "up_arch.h"
#include "tiva_gpio.h"
#include "tiva_ethernet.h"
#include "chip/tiva_pinmap.h"

#if defined(CONFIG_NET_NETMGR_ZEROCOPY)
#error The LM3S Ethernet driver requires copied network buffers
#endif
#if TIVA_NETHCONTROLLERS != 1
#error The LM3S Ethernet driver supports one controller
#endif

/* Half duplex can be forced if CONFIG_TIVA_ETHHDUPLEX is defined. */

#ifdef CONFIG_TIVA_ETHHDUPLEX
#define TIVA_DUPLEX_SETBITS 0
#define TIVA_DUPLEX_CLRBITS MAC_TCTL_DUPLEX
#else
#define TIVA_DUPLEX_SETBITS MAC_TCTL_DUPLEX
#define TIVA_DUPLEX_CLRBITS 0
#endif

/* Auto CRC generation can be suppressed if CONFIG_TIVA_ETHNOAUTOCRC is definde */

#ifdef CONFIG_TIVA_ETHNOAUTOCRC
#define TIVA_CRC_SETBITS 0
#define TIVA_CRC_CLRBITS MAC_TCTL_CRC
#else
#define TIVA_CRC_SETBITS MAC_TCTL_CRC
#define TIVA_CRC_CLRBITS 0
#endif

/* Tx padding can be suppressed if CONFIG_TIVA_ETHNOPAD is defined */

#ifdef CONFIG_TIVA_ETHNOPAD
#define TIVA_PADEN_SETBITS 0
#define TIVA_PADEN_CLRBITS MAC_TCTL_PADEN
#else
#define TIVA_PADEN_SETBITS MAC_TCTL_PADEN
#define TIVA_PADEN_CLRBITS 0
#endif

#define TIVA_TCTCL_SETBITS (TIVA_DUPLEX_SETBITS|TIVA_CRC_SETBITS|TIVA_PADEN_SETBITS)
#define TIVA_TCTCL_CLRBITS (TIVA_DUPLEX_CLRBITS|TIVA_CRC_CLRBITS|TIVA_PADEN_CLRBITS)

/* Multicast frames can be enabled by defining CONFIG_TIVA_MULTICAST */

#ifdef CONFIG_TIVA_MULTICAST
#define TIVA_AMUL_SETBITS MAC_RCTL_AMUL
#define TIVA_AMUL_CLRBITS 0
#else
#define TIVA_AMUL_SETBITS 0
#define TIVA_AMUL_CLRBITS MAC_RCTL_AMUL
#endif

/* Promiscuous mode can be enabled by defining CONFIG_TIVA_PROMISCUOUS */

#ifdef CONFIG_TIVA_PROMISCUOUS
#define TIVA_PRMS_SETBITS MAC_RCTL_PRMS
#define TIVA_PRMS_CLRBITS 0
#else
#define TIVA_PRMS_SETBITS 0
#define TIVA_PRMS_CLRBITS MAC_RCTL_PRMS
#endif

/* Bad CRC rejection can be enabled by define CONFIG_TIVA_BADCRC */

#ifdef CONFIG_TIVA_BADCRC
#define TIVA_BADCRC_SETBITS MAC_RCTL_BADCRC
#define TIVA_BADCRC_CLRBITS 0
#else
#define TIVA_BADCRC_SETBITS 0
#define TIVA_BADCRC_CLRBITS MAC_RCTL_BADCRC
#endif

#define TIVA_RCTCL_SETBITS (TIVA_AMUL_SETBITS|TIVA_PRMS_SETBITS|TIVA_BADCRC_SETBITS)
#define TIVA_RCTCL_CLRBITS (TIVA_AMUL_CLRBITS|TIVA_PRMS_CLRBITS|TIVA_BADCRC_CLRBITS)


#define TIVA_MAX_MDCCLK 2500000
#define TIVA_FRAME_SIZE (CONFIG_NET_ETH_MTU + ETH_HDRLEN)

struct tiva_driver_s {
	struct netdev *dev;
	bool up;
	sem_t txdone;
	struct work_s rxwork;
	uint8_t rxbuf[TIVA_FRAME_SIZE];
};

static struct tiva_driver_s g_lm3sdev;

static inline uint32_t tiva_ethin(struct tiva_driver_s *priv, int offset)
{
	return getreg32(TIVA_ETHCON_BASE + offset);
}

static inline void tiva_ethout(struct tiva_driver_s *priv, int offset, uint32_t value)
{
	putreg32(value, TIVA_ETHCON_BASE + offset);
}

static void tiva_ethreset(struct tiva_driver_s *priv)
{
	irqstate_t flags;
	uint32_t regval;

#if TIVA_NETHCONTROLLERS > 1
#error "If multiple interfaces are supported, this function would have to be redesigned"
#endif

	/* Make sure that clocking is enabled for the Ethernet (and PHY) peripherals */

	flags = irqsave();
	regval = getreg32(TIVA_SYSCON_RCGC2);
	regval |= (SYSCON_RCGC2_EMAC0 | SYSCON_RCGC2_EPHY0);
	putreg32(regval, TIVA_SYSCON_RCGC2);
	nllvdbg("RCGC2: %08x\n", regval);

	/* Then take the Ethernet controller out of the reset state */

	regval &= ~(SYSCON_SRCR2_EMAC0 | SYSCON_SRCR2_EPHY0);
	putreg32(regval, TIVA_SYSCON_SRCR2);
	nllvdbg("SRCR2: %08x\n", regval);

	/* Wait just a bit, again.  If we touch the ethernet too soon, we may busfault. */

	up_mdelay(2);

	/* Enable Port F for Ethernet LEDs: LED0=Bit 3; LED1=Bit 2 */

#ifdef CONFIG_TIVA_ETHLEDS
	/* Configure the pins for the peripheral function */

	tiva_configgpio(GPIO_ETHPHY_LED0 | GPIO_STRENGTH_2MA | GPIO_PADTYPE_STD);
	tiva_configgpio(GPIO_ETHPHY_LED1 | GPIO_STRENGTH_2MA | GPIO_PADTYPE_STD);
#endif

	/* Disable all Ethernet controller interrupts */

	regval = tiva_ethin(priv, TIVA_MAC_IM_OFFSET);
	regval &= ~MAC_IM_ALLINTS;
	tiva_ethout(priv, TIVA_MAC_IM_OFFSET, regval);

	/* Clear any pending interrupts (shouldn't be any) */

	regval = tiva_ethin(priv, TIVA_MAC_RIS_OFFSET);
	tiva_ethout(priv, TIVA_MAC_IACK_OFFSET, regval);
	irqrestore(flags);
}

/* RX runs on HPWORK. netdev_input copies the frame before returning, so
 * the driver can reuse rxbuf for the next FIFO entry.
 */
static void tiva_receive(void *arg)
{
	struct tiva_driver_s *priv = arg;
	while (priv->up && (tiva_ethin(priv, TIVA_MAC_NP_OFFSET) & MAC_NP_MASK)) {
		uint32_t word = tiva_ethin(priv, TIVA_MAC_DATA_OFFSET);
		unsigned int wirelen = word & 0xffff;
		/* The FIFO length includes its two-byte length and four-byte FCS. */
		bool valid = wirelen >= ETH_HDRLEN + 6 && wirelen <= sizeof(priv->rxbuf) + 6;
		unsigned int framelen = valid ? wirelen - 6 : 0;
		unsigned int offset = 2;
		if (valid) {
			priv->rxbuf[0] = word >> 16;
			priv->rxbuf[1] = word >> 24;
		}
		/* Drain the complete frame even when dropping an invalid length. */
		for (unsigned int consumed = 4; consumed < wirelen; consumed += 4) {
			word = tiva_ethin(priv, TIVA_MAC_DATA_OFFSET);
			for (unsigned int byte = 0; byte < 4; byte++, offset++) {
				if (offset < framelen) {
					priv->rxbuf[offset] = word >> (8 * byte);
				}
			}
		}
		if (valid && netdev_input(priv->dev, priv->rxbuf, framelen) < 0) {
			ndbg("LM3S: dropping received frame\n");
		}
	}

	irqstate_t flags = irqsave();
	if (priv->up) {
		uint32_t mask = tiva_ethin(priv, TIVA_MAC_IM_OFFSET);
		tiva_ethout(priv, TIVA_MAC_IM_OFFSET, mask | MAC_IM_RXINTM);
	}
	irqrestore(flags);
}

static int tiva_interrupt(int irq, void *context, void *arg)
{
	struct tiva_driver_s *priv = arg;
	/* Leave masked RX pending while HPWORK drains the FIFO. */
	uint32_t status = tiva_ethin(priv, TIVA_MAC_RIS_OFFSET) & tiva_ethin(priv, TIVA_MAC_IM_OFFSET);
	tiva_ethout(priv, TIVA_MAC_IACK_OFFSET, status);
	if (status & MAC_RIS_TXEMP) {
		int value;
		sem_getvalue(&priv->txdone, &value);
		if (value <= 0) {
			sem_post(&priv->txdone);
		}
	}
	if (status & MAC_RIS_RXINT) {
		uint32_t mask = tiva_ethin(priv, TIVA_MAC_IM_OFFSET);
		tiva_ethout(priv, TIVA_MAC_IM_OFFSET, mask & ~MAC_IM_RXINTM);
		/* A pending worker drains all frames before restoring RX interrupts. */
		int ret = work_queue(HPWORK, &priv->rxwork, tiva_receive, priv, 0);
		if (ret < 0 && ret != -EALREADY) {
			tiva_ethout(priv, TIVA_MAC_IM_OFFSET, mask);
		}
	}
	return OK;
}

static int tiva_transmit(struct netdev *dev, void *data, uint16_t len)
{
	struct tiva_driver_s *priv = dev->priv;
	const uint8_t *buf = data;
	struct timespec deadline;
	if (!data || len < ETH_HDRLEN || len > TIVA_FRAME_SIZE) {
		return -EINVAL;
	}
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec++;
	/* lwIP serializes linkoutput calls. Keep interrupts enabled while waiting
	 * for the previous hardware transmission to complete.
	 */
	while (priv->up && (tiva_ethin(priv, TIVA_MAC_TR_OFFSET) & MAC_TR_NEWTX)) {
		if (sem_timedwait(&priv->txdone, &deadline) < 0 && errno != EINTR) {
			return -errno;
		}
	}
	irqstate_t flags = irqsave();
	if (!priv->up) {
		irqrestore(flags);
		return -ENETDOWN;
	}
	uint32_t word = (len - ETH_HDRLEN) | ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 24);
	tiva_ethout(priv, TIVA_MAC_DATA_OFFSET, word);
	for (unsigned int offset = 2; offset < len; offset += 4) {
		word = 0;
		for (unsigned int byte = 0; byte < 4 && offset + byte < len; byte++) {
			word |= (uint32_t)buf[offset + byte] << (8 * byte);
		}
		tiva_ethout(priv, TIVA_MAC_DATA_OFFSET, word);
	}
	tiva_ethout(priv, TIVA_MAC_TR_OFFSET, MAC_TR_NEWTX);
	irqrestore(flags);
	return OK;
}

static int tiva_init(struct netdev *dev)
{
	return OK;
}

static int tiva_enable(struct netdev *dev)
{
	struct tiva_driver_s *priv = dev->priv;
	uint8_t mac[IFHWADDRLEN];
	if (netdev_get_hwaddr(dev, mac, NULL) < 0) {
		return -EINVAL;
	}
	irqstate_t flags = irqsave();
	tiva_ethreset(priv);
	tiva_ethout(priv, TIVA_MAC_MDV_OFFSET, SYSCLK_FREQUENCY / 2 / TIVA_MAX_MDCCLK);
	uint32_t value = tiva_ethin(priv, TIVA_MAC_TCTL_OFFSET);
	value = (value & ~TIVA_TCTCL_CLRBITS) | TIVA_TCTCL_SETBITS | MAC_TCTL_TXEN;
	tiva_ethout(priv, TIVA_MAC_TCTL_OFFSET, value);
	value = tiva_ethin(priv, TIVA_MAC_RCTL_OFFSET);
	value = (value & ~TIVA_RCTCL_CLRBITS) | TIVA_RCTCL_SETBITS | MAC_RCTL_RSTFIFO;
	tiva_ethout(priv, TIVA_MAC_RCTL_OFFSET, value);
	tiva_ethout(priv, TIVA_MAC_RCTL_OFFSET, (value & ~MAC_RCTL_RSTFIFO) | MAC_RCTL_RXEN);
	tiva_ethout(priv, TIVA_MAC_IA0_OFFSET, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
	tiva_ethout(priv, TIVA_MAC_IA1_OFFSET, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8));
	priv->up = true;
	tiva_ethout(priv, TIVA_MAC_IM_OFFSET, MAC_IM_RXINTM | MAC_IM_TXEMPM);
	up_enable_irq(TIVA_IRQ_ETHCON);
	irqrestore(flags);
	return OK;
}

static int tiva_disable(struct netdev *dev)
{
	struct tiva_driver_s *priv = dev->priv;
	irqstate_t flags = irqsave();
	priv->up = false;
	up_disable_irq(TIVA_IRQ_ETHCON);
	tiva_ethout(priv, TIVA_MAC_IM_OFFSET, 0);
	tiva_ethout(priv, TIVA_MAC_RCTL_OFFSET, MAC_RCTL_RSTFIFO);
	uint32_t value = tiva_ethin(priv, TIVA_MAC_TCTL_OFFSET);
	tiva_ethout(priv, TIVA_MAC_TCTL_OFFSET, value & ~MAC_TCTL_TXEN);
	tiva_ethout(priv, TIVA_MAC_IACK_OFFSET, tiva_ethin(priv, TIVA_MAC_RIS_OFFSET));
	work_cancel(HPWORK, &priv->rxwork);
	irqrestore(flags);
	return OK;
}

static int tiva_multicast(struct netdev *dev, const struct in_addr *group, netdev_mac_filter_action action)
{
	/* The LM3S MAC accepts all multicast frames when AMUL is set. */
	return OK;
}

void up_netinitialize(void)
{
	struct tiva_driver_s *priv = &g_lm3sdev;
	struct ether_addr mac;
	struct nic_io_ops io = {tiva_transmit, tiva_multicast};
	static struct ethernet_ops ops = {tiva_init, tiva_disable, tiva_enable, tiva_disable};
	struct netdev_config config;
	memset(&config, 0, sizeof(config));
	sem_init(&priv->txdone, 0, 0);
	sem_setprotocol(&priv->txdone, SEM_PRIO_NONE);
	tiva_ethreset(priv);
#ifdef CONFIG_TIVA_BOARDMAC
	tiva_ethernetmac(&mac);
#else
	uint32_t low = tiva_ethin(priv, TIVA_MAC_IA0_OFFSET);
	uint32_t high = tiva_ethin(priv, TIVA_MAC_IA1_OFFSET);
	for (int i = 0; i < 4; i++) {
		mac.ether_addr_octet[i] = low >> (8 * i);
	}
	mac.ether_addr_octet[4] = high;
	mac.ether_addr_octet[5] = high >> 8;
#endif
	config.type = NM_ETHERNET;
	config.ops = &io;
	config.t_ops.eth = &ops;
	config.priv = priv;
	config.flag = NM_FLAG_ETHARP | NM_FLAG_ETHERNET | NM_FLAG_BROADCAST;
#ifdef CONFIG_TIVA_MULTICAST
	config.flag |= NM_FLAG_IGMP;
#endif
	config.mtu = CONFIG_NET_ETH_MTU;
	config.hwaddr_len = IFHWADDRLEN;
	config.is_default = 1;
	if (irq_attach(TIVA_IRQ_ETHCON, tiva_interrupt, priv) < 0) {
		ndbg("LM3S: failed to attach Ethernet IRQ\n");
		sem_destroy(&priv->txdone);
		return;
	}
	priv->dev = netdev_register(&config);
	if (!priv->dev) {
		ndbg("LM3S: failed to register Ethernet device\n");
		irq_detach(TIVA_IRQ_ETHCON);
		sem_destroy(&priv->txdone);
		return;
	}
	netdev_set_hwaddr(priv->dev, mac.ether_addr_octet, IFHWADDRLEN);
}
#endif /* CONFIG_NET && CONFIG_TIVA_ETHERNET */
