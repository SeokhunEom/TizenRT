#include <stdlib.h>
#include <string.h>

#include <lwip/pbuf.h>

unsigned int g_task11_pbuf_alloc_calls;

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type)
{
	struct pbuf *p;

	(void)layer;
	g_task11_pbuf_alloc_calls++;
	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return NULL;
	}

	p->len = length;
	p->tot_len = length;
	p->type = type;
	p->ref = 1;
	if (type == PBUF_RAM || type == PBUF_POOL) {
		p->base = calloc(1, (size_t)length + PBUF_TRANSPORT_HLEN);
		if (p->base == NULL) {
			free(p);
			return NULL;
		}
		p->payload = p->base + PBUF_TRANSPORT_HLEN;
	}

	return p;
}

u8_t pbuf_free(struct pbuf *p)
{
	if (p->ref > 1) {
		p->ref--;
		return 0;
	}

	free(p->base);
	free(p);
	return 1;
}

void pbuf_ref(struct pbuf *p)
{
	p->ref++;
}

u8_t pbuf_clen(const struct pbuf *p)
{
	u8_t count = 0;

	while (p != NULL) {
		count++;
		p = p->next;
	}
	return count;
}

u16_t pbuf_copy_partial(const struct pbuf *p, void *dest, u16_t length, u16_t offset)
{
	u16_t copied = 0;
	uint8_t *out = dest;

	while (p != NULL && copied < length) {
		u16_t start = offset < p->len ? offset : p->len;
		u16_t available = p->len - start;
		u16_t take = available < length - copied ? available : length - copied;

		if (take > 0 && p->payload != NULL) {
			memcpy(out + copied, (const uint8_t *)p->payload + start, take);
		}
		copied += take;
		offset -= start;
		p = p->next;
	}
	return copied;
}

err_t pbuf_take(struct pbuf *p, const void *data, u16_t length)
{
	const uint8_t *in = data;
	u16_t copied = 0;
	struct pbuf *node = p;

	if (length > p->tot_len) {
		return ERR_MEM;
	}

	while (node != NULL && copied < length) {
		u16_t take = node->len < length - copied ? node->len : length - copied;

		if (take > 0 && node->payload != NULL) {
			memcpy(node->payload, in + copied, take);
		}
		copied += take;
		node = node->next;
	}
	return copied == length ? ERR_OK : ERR_MEM;
}

u16_t pbuf_memcmp(const struct pbuf *p, u16_t offset, const void *data, u16_t length)
{
	uint8_t *actual;
	u16_t copied;
	u16_t result;

	actual = malloc(length);
	if (actual == NULL) {
		return 1;
	}
	copied = pbuf_copy_partial(p, actual, length, offset);
	result = copied == length && memcmp(actual, data, length) == 0 ? 0 : 1;
	free(actual);
	return result;
}

u8_t pbuf_header(struct pbuf *p, int16_t header_size_increment)
{
	if (header_size_increment < 0) {
		u16_t amount = (u16_t)-header_size_increment;
		p->payload = (uint8_t *)p->payload + amount;
		p->len -= amount;
		p->tot_len -= amount;
	} else {
		u16_t amount = (u16_t)header_size_increment;
		p->payload = (uint8_t *)p->payload - amount;
		p->len += amount;
		p->tot_len += amount;
	}
	return 0;
}

void pbuf_chain(struct pbuf *head, struct pbuf *tail)
{
	head->next = tail;
	head->tot_len += tail->tot_len;
	tail->ref++;
}

struct pbuf *pbuf_dechain(struct pbuf *head)
{
	struct pbuf *tail = head->next;

	if (tail != NULL) {
		head->next = NULL;
		head->tot_len = head->len;
		tail->ref--;
	}
	return tail;
}
