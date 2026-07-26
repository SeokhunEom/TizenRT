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

#include <tinyara/config.h>

#include <errno.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <debug.h>
#include <tinyara/arch.h>
#include <tinyara/irq.h>
#include <tinyara/net/if/ethernet.h>
#include <tinyara/netmgr/netdev_mgr.h>
#include <tinyara/wqueue.h>

#include <arch/chip/chip.h>
#include <arch/chip/irq.h>

#define LAN9118_RX_DATA                 0x000
#define LAN9118_TX_DATA                 0x020
#define LAN9118_RX_STATUS               0x040
#define LAN9118_ID_REV                  0x050
#define LAN9118_IRQ_CFG                 0x054
#define LAN9118_INT_STS                 0x058
#define LAN9118_INT_EN                  0x05c
#define LAN9118_FIFO_INT                0x068
#define LAN9118_RX_CFG                  0x06c
#define LAN9118_TX_CFG                  0x070
#define LAN9118_HW_CFG                  0x074
#define LAN9118_RX_FIFO_INF             0x07c
#define LAN9118_TX_FIFO_INF             0x080
#define LAN9118_GPIO_CFG                0x088
#define LAN9118_MAC_CSR_CMD             0x0a4
#define LAN9118_MAC_CSR_DATA            0x0a8
#define LAN9118_AFC_CFG                 0x0ac

#define LAN9118_HW_CFG_SRST             (1u << 0)
#define LAN9118_HW_CFG_TX_FIF_SZ_5K     (5u << 16)
#define LAN9118_INT_RSFL                (1u << 3)
#define LAN9118_IRQ_CFG_ENABLE          0x22000111
#define LAN9118_TX_CFG_ON               (1u << 1)
#define LAN9118_RX_FIFO_PENDING_MASK    (0xffu << 16)
#define LAN9118_RX_FIFO_PENDING_SHIFT   16
#define LAN9118_TX_FIFO_FREE_MASK       0xffffu

#define LAN9118_MAC_CSR_BUSY            (1u << 31)
#define LAN9118_MAC_CSR_READ            (1u << 30)
#define LAN9118_MAC_CR                  1
#define LAN9118_MAC_ADDRH               2
#define LAN9118_MAC_ADDRL               3
#define LAN9118_MAC_CR_RXEN             (1u << 2)
#define LAN9118_MAC_CR_TXEN             (1u << 3)
#define LAN9118_MAC_CR_MCPAS            (1u << 19)

#define LAN9118_TX_CMD_A_FIRST          (1u << 13)
#define LAN9118_TX_CMD_A_LAST           (1u << 12)
#define LAN9118_RX_STATUS_ERROR         (1u << 15)
#define LAN9118_RX_STATUS_LEN_MASK      (0x3fffu << 16)
#define LAN9118_RX_STATUS_LEN_SHIFT     16

#define LAN9118_CHIP_ID_9118            0x0118
#define LAN9118_CHIP_ID_9220            0x9220
#define LAN9118_MTU                     1500
#define LAN9118_ETH_HEADER_LEN          14
#define LAN9118_FCS_LEN                 4
#define LAN9118_MAX_FRAME               (LAN9118_MTU + LAN9118_ETH_HEADER_LEN)
#define LAN9118_RX_BUFFER_SIZE          ((LAN9118_MAX_FRAME + LAN9118_FCS_LEN + 3) & ~3)
#define LAN9118_TIMEOUT_US              100000

struct lan9118_driver_s {
	struct netdev *netdev;
	struct work_s rxwork;
	sem_t txsem;
	bool initialized;
	volatile bool enabled;
	uint8_t rxbuf[LAN9118_RX_BUFFER_SIZE];
};

static struct lan9118_driver_s g_lan9118;

static const uint8_t g_lan9118_mac[6] = {
	0x52, 0x54, 0x00, 0x12, 0x34, 0x56
};

static inline uint32_t lan9118_getreg(uint32_t offset)
{
	return *(volatile uint32_t *)(MPS2_LAN9118_BASE + offset);
}

static inline void lan9118_putreg(uint32_t value, uint32_t offset)
{
	*(volatile uint32_t *)(MPS2_LAN9118_BASE + offset) = value;
}

static int lan9118_wait_clear(uint32_t offset, uint32_t mask)
{
	unsigned int elapsed;

	for (elapsed = 0; elapsed < LAN9118_TIMEOUT_US; elapsed++) {
		if ((lan9118_getreg(offset) & mask) == 0) {
			return OK;
		}
		up_udelay(1);
	}

	return -ETIMEDOUT;
}

static int lan9118_mac_write(uint8_t reg, uint32_t value)
{
	if (lan9118_wait_clear(LAN9118_MAC_CSR_CMD,
						   LAN9118_MAC_CSR_BUSY) < 0) {
		return -ETIMEDOUT;
	}

	lan9118_putreg(value, LAN9118_MAC_CSR_DATA);
	lan9118_putreg(LAN9118_MAC_CSR_BUSY | reg, LAN9118_MAC_CSR_CMD);
	return lan9118_wait_clear(LAN9118_MAC_CSR_CMD, LAN9118_MAC_CSR_BUSY);
}

static int lan9118_reset(void)
{
	uint32_t chip_id;

	chip_id = lan9118_getreg(LAN9118_ID_REV) >> 16;
	if (chip_id != LAN9118_CHIP_ID_9118 &&
		chip_id != LAN9118_CHIP_ID_9220) {
		lldbg("LAN9118: unsupported chip id 0x%04lx\n",
			  (unsigned long)chip_id);
		return -ENODEV;
	}

	lan9118_putreg(LAN9118_HW_CFG_SRST, LAN9118_HW_CFG);
	if (lan9118_wait_clear(LAN9118_HW_CFG, LAN9118_HW_CFG_SRST) < 0) {
		lldbg("LAN9118: reset timeout\n");
		return -ETIMEDOUT;
	}

	lan9118_putreg(LAN9118_HW_CFG_TX_FIF_SZ_5K, LAN9118_HW_CFG);
	lan9118_putreg(0x006e3740, LAN9118_AFC_CFG);
	lan9118_putreg(0x70070000, LAN9118_GPIO_CFG);
	lan9118_putreg(0, LAN9118_INT_EN);
	lan9118_putreg(UINT32_MAX, LAN9118_INT_STS);
	lan9118_putreg(0, LAN9118_RX_CFG);
	lan9118_putreg(0, LAN9118_FIFO_INT);
	lan9118_putreg(LAN9118_TX_CFG_ON, LAN9118_TX_CFG);

	if (lan9118_mac_write(LAN9118_MAC_ADDRL,
						  ((uint32_t)g_lan9118_mac[3] << 24) |
						  ((uint32_t)g_lan9118_mac[2] << 16) |
						  ((uint32_t)g_lan9118_mac[1] << 8) |
						  g_lan9118_mac[0]) < 0 ||
		lan9118_mac_write(LAN9118_MAC_ADDRH,
						  ((uint32_t)g_lan9118_mac[5] << 8) |
						  g_lan9118_mac[4]) < 0 ||
		lan9118_mac_write(LAN9118_MAC_CR,
						  LAN9118_MAC_CR_RXEN |
						  LAN9118_MAC_CR_TXEN |
						  LAN9118_MAC_CR_MCPAS) < 0) {
		lldbg("LAN9118: MAC setup timeout\n");
		return -ETIMEDOUT;
	}

	lan9118_putreg(LAN9118_IRQ_CFG_ENABLE, LAN9118_IRQ_CFG);
	return OK;
}

static void lan9118_drain_rx(FAR void *arg)
{
	struct lan9118_driver_s *priv = arg;

	while (priv->enabled &&
		   ((lan9118_getreg(LAN9118_RX_FIFO_INF) &
			 LAN9118_RX_FIFO_PENDING_MASK) >>
			LAN9118_RX_FIFO_PENDING_SHIFT) != 0) {
		uint32_t status = lan9118_getreg(LAN9118_RX_STATUS);
		uint32_t frame_len = (status & LAN9118_RX_STATUS_LEN_MASK) >>
							 LAN9118_RX_STATUS_LEN_SHIFT;
		uint32_t words = (frame_len + 3) / 4;
		uint32_t i;

		for (i = 0; i < words; i++) {
			uint32_t word = lan9118_getreg(LAN9118_RX_DATA);
			if (i * 4 < sizeof(priv->rxbuf)) {
				memcpy(priv->rxbuf + i * 4, &word,
					   sizeof(word));
			}
		}

		if ((status & LAN9118_RX_STATUS_ERROR) == 0 &&
			frame_len >= LAN9118_ETH_HEADER_LEN + LAN9118_FCS_LEN &&
			frame_len <= sizeof(priv->rxbuf)) {
			(void)netdev_input(priv->netdev, priv->rxbuf,
							   frame_len - LAN9118_FCS_LEN);
		}
	}

	if (priv->enabled) {
		lan9118_putreg(LAN9118_INT_RSFL, LAN9118_INT_EN);
	}
}

static int lan9118_interrupt(int irq, FAR void *context, FAR void *arg)
{
	struct lan9118_driver_s *priv = arg;

	(void)irq;
	(void)context;
	lan9118_putreg(0, LAN9118_INT_EN);
	lan9118_putreg(LAN9118_INT_RSFL, LAN9118_INT_STS);

	if (!priv->enabled) {
		return OK;
	}

	if (work_available(&priv->rxwork)) {
		if (work_queue(HPWORK, &priv->rxwork, lan9118_drain_rx,
					   priv, 0) < 0 && priv->enabled) {
			lan9118_putreg(LAN9118_INT_RSFL, LAN9118_INT_EN);
		}
	}

	return OK;
}

static int lan9118_linkoutput(struct netdev *dev, FAR void *data,
							  uint16_t len)
{
	struct lan9118_driver_s *priv = dev->priv;
	const uint8_t *frame = data;
	uint32_t aligned_len;
	uint32_t i;
	int ret = OK;

	if (!priv || !priv->enabled || !frame ||
		len < LAN9118_ETH_HEADER_LEN || len > LAN9118_MAX_FRAME) {
		return -EINVAL;
	}

	while (sem_wait(&priv->txsem) < 0) {
		if (errno != EINTR) {
			return -errno;
		}
	}

	aligned_len = (len + 3) & ~3u;
	if ((lan9118_getreg(LAN9118_TX_FIFO_INF) &
		 LAN9118_TX_FIFO_FREE_MASK) < aligned_len + 8) {
		ret = -EBUSY;
		goto out;
	}

	lan9118_putreg(LAN9118_TX_CMD_A_FIRST | LAN9118_TX_CMD_A_LAST | len,
				   LAN9118_TX_DATA);
	lan9118_putreg(((uint32_t)len << 16) | len, LAN9118_TX_DATA);

	for (i = 0; i < aligned_len; i += 4) {
		uint32_t word = 0;
		uint32_t remaining = len - i;
		size_t copy_len = remaining < sizeof(word) ? remaining : sizeof(word);

		memcpy(&word, frame + i, copy_len);
		lan9118_putreg(word, LAN9118_TX_DATA);
	}

out:
	sem_post(&priv->txsem);
	return ret;
}

static int lan9118_multicast(struct netdev *dev,
							 FAR const struct in_addr *group,
							 netdev_mac_filter_action action)
{
	(void)dev;
	(void)group;
	(void)action;
	return OK;
}

static int lan9118_init(struct netdev *dev)
{
	struct lan9118_driver_s *priv = dev->priv;
	int ret;

	if (priv->initialized) {
		return OK;
	}

	ret = lan9118_reset();
	if (ret < 0) {
		return ret;
	}

	ret = irq_attach(MPS2_IRQ_LAN9118, lan9118_interrupt, priv);
	if (ret < 0) {
		return ret;
	}

	priv->initialized = true;
	return OK;
}

static int lan9118_disable(struct netdev *dev)
{
	struct lan9118_driver_s *priv = dev->priv;

	if (!priv || !priv->enabled) {
		return OK;
	}

	priv->enabled = false;
	lan9118_putreg(0, LAN9118_INT_EN);
	up_disable_irq(MPS2_IRQ_LAN9118);
	(void)work_cancel(HPWORK, &priv->rxwork);
	return OK;
}

static int lan9118_deinit(struct netdev *dev)
{
	struct lan9118_driver_s *priv = dev->priv;

	(void)lan9118_disable(dev);
	if (priv && priv->initialized) {
		irq_detach(MPS2_IRQ_LAN9118);
		priv->initialized = false;
	}

	return OK;
}

static int lan9118_enable(struct netdev *dev)
{
	struct lan9118_driver_s *priv = dev->priv;

	if (!priv || !priv->initialized) {
		return -ENODEV;
	}

	if (priv->enabled) {
		return OK;
	}

	priv->enabled = true;
	lan9118_putreg(LAN9118_INT_RSFL, LAN9118_INT_STS);
	lan9118_putreg(LAN9118_INT_RSFL, LAN9118_INT_EN);
	up_enable_irq(MPS2_IRQ_LAN9118);
	return OK;
}

static struct nic_io_ops g_lan9118_io_ops = {
	.linkoutput = lan9118_linkoutput,
	.igmp_mac_filter = lan9118_multicast,
};

static struct ethernet_ops g_lan9118_eth_ops = {
	.init = lan9118_init,
	.deinit = lan9118_deinit,
	.enable = lan9118_enable,
	.disable = lan9118_disable,
};

void up_netinitialize(void)
{
	struct netdev_config config;

	memset(&g_lan9118, 0, sizeof(g_lan9118));
	sem_init(&g_lan9118.txsem, 0, 1);

	memset(&config, 0, sizeof(config));
	config.ops = &g_lan9118_io_ops;
	config.flag = NM_FLAG_BROADCAST | NM_FLAG_ETHARP |
				  NM_FLAG_ETHERNET | NM_FLAG_IGMP;
	config.mtu = LAN9118_MTU;
	config.hwaddr_len = sizeof(g_lan9118_mac);
	memcpy(config.hwaddr, g_lan9118_mac, sizeof(g_lan9118_mac));
	config.is_default = 1;
	config.t_ops.eth = &g_lan9118_eth_ops;
	config.type = NM_ETHERNET;
	config.priv = &g_lan9118;

	g_lan9118.netdev = netdev_register(&config);
	if (!g_lan9118.netdev) {
		lldbg("LAN9118: netdev registration failed\n");
	}
}
