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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#ifdef CONFIG_SERIAL_TERMIOS
#include <termios.h>
#endif

#include <tinyara/irq.h>
#include <tinyara/serial/serial.h>
#include <arch/irq.h>

#include "up_arch.h"
#include "up_internal.h"
#include "chip.h"

#include "qemu_armv8m_internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qemu_armv8m_uart_s {
	uint32_t base;
	uint32_t baud;
	uint8_t parity;
	uint8_t bits;
	uint8_t stopbits2;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int qemu_uart_setup(FAR struct uart_dev_s *dev);
static void qemu_uart_shutdown(FAR struct uart_dev_s *dev);
static int qemu_uart_attach(FAR struct uart_dev_s *dev);
static void qemu_uart_detach(FAR struct uart_dev_s *dev);
static int qemu_uart_ioctl(FAR struct uart_dev_s *dev, int cmd, unsigned long arg);
static int qemu_uart_receive(FAR struct uart_dev_s *dev, FAR unsigned int *status);
static void qemu_uart_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool qemu_uart_rxavailable(FAR struct uart_dev_s *dev);
static void qemu_uart_send(FAR struct uart_dev_s *dev, int ch);
static void qemu_uart_txint(FAR struct uart_dev_s *dev, bool enable);
static bool qemu_uart_txready(FAR struct uart_dev_s *dev);
static bool qemu_uart_txempty(FAR struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops = {
	.setup = qemu_uart_setup,
	.shutdown = qemu_uart_shutdown,
	.attach = qemu_uart_attach,
	.detach = qemu_uart_detach,
	.ioctl = qemu_uart_ioctl,
	.receive = qemu_uart_receive,
	.rxint = qemu_uart_rxint,
	.rxavailable = qemu_uart_rxavailable,
	.send = qemu_uart_send,
	.txint = qemu_uart_txint,
	.txready = qemu_uart_txready,
	.txempty = qemu_uart_txempty,
};

static char g_uart0rxbuffer[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_UART0_TXBUFSIZE];

static struct qemu_armv8m_uart_s g_uart0priv = {
	.base = MPS2_UART0_BASE,
	.baud = CONFIG_UART0_BAUD,
	.parity = CONFIG_UART0_PARITY,
	.bits = CONFIG_UART0_BITS,
	.stopbits2 = CONFIG_UART0_2STOP,
};

static uart_dev_t g_uart0port = {
	.isconsole = false,
	.recv = {
		.size = CONFIG_UART0_RXBUFSIZE,
		.buffer = g_uart0rxbuffer,
	},
	.xmit = {
		.size = CONFIG_UART0_TXBUFSIZE,
		.buffer = g_uart0txbuffer,
	},
	.ops = &g_uart_ops,
	.priv = &g_uart0priv,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t qemu_uart_getreg(FAR struct qemu_armv8m_uart_s *priv,
					uint32_t offset)
{
	return getreg32(priv->base + offset);
}

static inline void qemu_uart_putreg(FAR struct qemu_armv8m_uart_s *priv,
				    uint32_t offset, uint32_t value)
{
	putreg32(value, priv->base + offset);
}

static uint32_t qemu_uart_ctrl(FAR struct qemu_armv8m_uart_s *priv)
{
	return qemu_uart_getreg(priv, MPS2_UART_CTRL_OFFSET);
}

static void qemu_uart_enable(FAR struct qemu_armv8m_uart_s *priv)
{
	uint32_t bauddiv = MPS2_AN505_SYSCLK_FREQUENCY / priv->baud;

	if (bauddiv == 0) {
		bauddiv = 1;
	}

	qemu_uart_putreg(priv, MPS2_UART_BAUDDIV_OFFSET, bauddiv);
	qemu_uart_putreg(priv, MPS2_UART_INTSTATUS_OFFSET, MPS2_UART_INT_ALL);
	qemu_uart_putreg(priv, MPS2_UART_CTRL_OFFSET,
			 MPS2_UART_CTRL_TXEN | MPS2_UART_CTRL_RXEN);
}

static int qemu_uart_interrupt(int irq, FAR void *context, FAR void *arg)
{
	uint32_t status;

	status = getreg32(MPS2_UART0_INTSTATUS);
	putreg32(status | MPS2_UART_INT_ALL, MPS2_UART0_INTSTATUS);

	if (qemu_uart_rxavailable(&g_uart0port)) {
		uart_recvchars(&g_uart0port);
	}

	if (qemu_uart_txready(&g_uart0port)) {
		uart_xmitchars(&g_uart0port);
	}

	return OK;
}

static int qemu_uart_setup(FAR struct uart_dev_s *dev)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	qemu_uart_enable(priv);
	return OK;
}

static void qemu_uart_shutdown(FAR struct uart_dev_s *dev)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	qemu_uart_putreg(priv, MPS2_UART_CTRL_OFFSET, 0);
}

static int qemu_uart_attach(FAR struct uart_dev_s *dev)
{
	int ret;

	ret = irq_attach(MPS2_IRQ_UART0_RX, qemu_uart_interrupt, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = irq_attach(MPS2_IRQ_UART0_TX, qemu_uart_interrupt, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = irq_attach(MPS2_IRQ_UART0_COMBINED, qemu_uart_interrupt, NULL);
	if (ret < 0) {
		return ret;
	}

	up_enable_irq(MPS2_IRQ_UART0_RX);
	up_enable_irq(MPS2_IRQ_UART0_TX);
	up_enable_irq(MPS2_IRQ_UART0_COMBINED);
	return OK;
}

static void qemu_uart_detach(FAR struct uart_dev_s *dev)
{
	up_disable_irq(MPS2_IRQ_UART0_RX);
	up_disable_irq(MPS2_IRQ_UART0_TX);
	up_disable_irq(MPS2_IRQ_UART0_COMBINED);

	irq_detach(MPS2_IRQ_UART0_RX);
	irq_detach(MPS2_IRQ_UART0_TX);
	irq_detach(MPS2_IRQ_UART0_COMBINED);
}

static int qemu_uart_ioctl(FAR struct uart_dev_s *dev, int cmd, unsigned long arg)
{
#ifdef CONFIG_SERIAL_TERMIOS
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;
	FAR struct termios *termiosp = (FAR struct termios *)arg;
	int ret = OK;

	switch (cmd) {
	case TCGETS:
		if (termiosp == NULL) {
			return -EINVAL;
		}

		cfsetispeed(termiosp, priv->baud);
		termiosp->c_cflag = CS5 + (priv->bits - 5);

		if (priv->parity != 0) {
			termiosp->c_cflag |= PARENB;
			if (priv->parity == 1) {
				termiosp->c_cflag |= PARODD;
			}
		}

		if (priv->stopbits2) {
			termiosp->c_cflag |= CSTOPB;
		}
		break;

	case TCSETS:
		if (termiosp == NULL) {
			return -EINVAL;
		}

		priv->bits = 5 + (termiosp->c_cflag & CSIZE);
		if (priv->bits < 5 || priv->bits > 8) {
			return -EINVAL;
		}

		priv->parity = 0;
		if ((termiosp->c_cflag & PARENB) != 0) {
			priv->parity = (termiosp->c_cflag & PARODD) != 0 ? 1 : 2;
		}

		priv->stopbits2 = (termiosp->c_cflag & CSTOPB) != 0;
		priv->baud = cfgetispeed(termiosp);
		if (priv->baud == 0) {
			priv->baud = CONFIG_UART0_BAUD;
		}

		qemu_uart_enable(priv);
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
#else
	return -ENOTTY;
#endif
}

static int qemu_uart_receive(FAR struct uart_dev_s *dev, FAR unsigned int *status)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	if (status != NULL) {
		*status = qemu_uart_getreg(priv, MPS2_UART_INTSTATUS_OFFSET);
	}

	return qemu_uart_getreg(priv, MPS2_UART_DATA_OFFSET) & 0xff;
}

static void qemu_uart_rxint(FAR struct uart_dev_s *dev, bool enable)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;
	uint32_t regval;

	regval = qemu_uart_ctrl(priv);
	if (enable) {
		regval |= MPS2_UART_CTRL_RXINTEN | MPS2_UART_CTRL_RXOVRINTEN;
	} else {
		regval &= ~(MPS2_UART_CTRL_RXINTEN | MPS2_UART_CTRL_RXOVRINTEN);
	}

	qemu_uart_putreg(priv, MPS2_UART_CTRL_OFFSET, regval);
}

static bool qemu_uart_rxavailable(FAR struct uart_dev_s *dev)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	return (qemu_uart_getreg(priv, MPS2_UART_STATE_OFFSET) &
		MPS2_UART_STATE_RXFULL) != 0;
}

static void qemu_uart_send(FAR struct uart_dev_s *dev, int ch)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	while (!qemu_uart_txready(dev)) {
	}

	qemu_uart_putreg(priv, MPS2_UART_DATA_OFFSET, ch & 0xff);
}

static void qemu_uart_txint(FAR struct uart_dev_s *dev, bool enable)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;
	irqstate_t flags;
	uint32_t regval;

	flags = irqsave();
	regval = qemu_uart_ctrl(priv);
	if (enable) {
		regval |= MPS2_UART_CTRL_TXINTEN | MPS2_UART_CTRL_TXOVRINTEN;
	} else {
		regval &= ~(MPS2_UART_CTRL_TXINTEN | MPS2_UART_CTRL_TXOVRINTEN);
	}

	qemu_uart_putreg(priv, MPS2_UART_CTRL_OFFSET, regval);

	if (enable) {
		uart_xmitchars(dev);
	}

	irqrestore(flags);
}

static bool qemu_uart_txready(FAR struct uart_dev_s *dev)
{
	FAR struct qemu_armv8m_uart_s *priv = dev->priv;

	return (qemu_uart_getreg(priv, MPS2_UART_STATE_OFFSET) &
		MPS2_UART_STATE_TXFULL) == 0;
}

static bool qemu_uart_txempty(FAR struct uart_dev_s *dev)
{
	return qemu_uart_txready(dev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void qemu_armv8m_lowsetup(void)
{
	qemu_uart_enable(&g_uart0priv);
}

void qemu_armv8m_lowputc(char ch)
{
	while ((getreg32(MPS2_UART0_STATE) & MPS2_UART_STATE_TXFULL) != 0) {
	}

	putreg32(ch & 0xff, MPS2_UART0_DATA);
}

int qemu_armv8m_lowgetc(void)
{
	if ((getreg32(MPS2_UART0_STATE) & MPS2_UART_STATE_RXFULL) == 0) {
		return -1;
	}

	return getreg32(MPS2_UART0_DATA) & 0xff;
}

#ifdef USE_SERIALDRIVER
void up_earlyserialinit(void)
{
	qemu_uart_setup(&g_uart0port);
}

void up_serialinit(void)
{
	g_uart0port.isconsole = true;
	qemu_uart_setup(&g_uart0port);

	uart_register("/dev/console", &g_uart0port);
	uart_register("/dev/ttyS0", &g_uart0port);
}

void *up_get_console_dev(void)
{
	return &g_uart0port;
}
#endif

void up_lowputc(char ch)
{
	qemu_armv8m_lowputc(ch);
}

int up_lowgetc(void)
{
	return qemu_armv8m_lowgetc();
}

int up_putc(int ch)
{
	if (ch == '\n') {
		qemu_armv8m_lowputc('\r');
	}

	qemu_armv8m_lowputc(ch);
	return ch;
}

int up_getc(void)
{
	return qemu_armv8m_lowgetc();
}
