#ifndef __TASK11_FIXTURE_LWIP_PBUF_H
#define __TASK11_FIXTURE_LWIP_PBUF_H

#include <stddef.h>
#include <stdint.h>

#define TASK11_LWIP_PBUF_INCLUDED 1
#define PBUF_TRANSPORT_HLEN 4
#define ERR_OK 0
#define ERR_MEM (-1)

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef int err_t;

typedef enum {
	PBUF_TRANSPORT = 0,
	PBUF_IP,
	PBUF_LINK,
	PBUF_RAW_TX,
	PBUF_RAW
} pbuf_layer;

typedef enum {
	PBUF_RAM = 0,
	PBUF_ROM,
	PBUF_REF,
	PBUF_POOL
} pbuf_type;

struct pbuf {
	struct pbuf *next;
	void *payload;
	u16_t tot_len;
	u16_t len;
	pbuf_type type;
	u8_t ref;
	uint8_t *base;
};

extern unsigned int g_task11_pbuf_alloc_calls;

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type);
u8_t pbuf_free(struct pbuf *p);
void pbuf_ref(struct pbuf *p);
u8_t pbuf_clen(const struct pbuf *p);
err_t pbuf_take(struct pbuf *p, const void *data, u16_t length);
u16_t pbuf_copy_partial(const struct pbuf *p, void *dest, u16_t length, u16_t offset);
u16_t pbuf_memcmp(const struct pbuf *p, u16_t offset, const void *data, u16_t length);
u8_t pbuf_header(struct pbuf *p, int16_t header_size_increment);
void pbuf_chain(struct pbuf *head, struct pbuf *tail);
struct pbuf *pbuf_dechain(struct pbuf *head);

#endif
