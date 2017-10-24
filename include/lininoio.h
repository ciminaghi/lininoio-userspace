#ifndef __LININOIO_H__
#define __LININOIO_H__

#include <stdint.h>

/*
 * Lininoio transport protocol
 * GPL v2 or later
 * Copyright Dog Hunter 2016
 * Author Davide Ciminaghi 2016
 */

/*
 * Ether type for lininoio over ethernet, we place this define here because
 * it is implementation independent, just like this file should be.
 */
#define LININOIO_ETH_TYPE 0x86b5

enum lininoio_packet_type {
	LININOIO_PACKET_AREQUEST = 1,
	LININOIO_PACKET_AREPLY = 2,
	LININOIO_PACKET_DATA = 3,
};

#define LININOIO_PROTO_MCUIO_V0		0x0001
#define LININOIO_PROTO_CONSOLE		0x0002
#define LININOIO_PROTO_RPMSG		0x0003

#define LININOIO_MAX_NCHANNELS		16
#define LININOIO_MAX_NCORES		8
#define LININOIO_N_PROTOS		(1 << 13)

/* LININOIO packets */

/* Generic packet */

struct lininoio_packet {
	uint8_t type;
	uint8_t rest_of_packet[0];
} __attribute__((packed));

/* Association request */

struct lininoio_arequest_packet {
	uint8_t type;
	uint8_t slave_name[16];
	uint8_t nchannels;
	uint16_t chan_descr[0];
} __attribute__((packed));

static inline uint8_t lininoio_cdescr_to_core_id(uint16_t chan_descr)
{
	return chan_descr >> 13;
}

static inline uint16_t lininoio_cdescr_to_proto_id(uint16_t chan_descr)
{
	return chan_descr & 0x1fff;
}

/* Association reply */

struct lininoio_association_data {
	uint16_t chan_dlen;
	uint8_t chan_data[0];
} __attribute__((packed));

static inline uint16_t lininoio_decode_cdlen(uint16_t cdlen, uint8_t *chan_id)
{
	if (chan_id)
		*chan_id = cdlen >> 12;
	return cdlen & 0xfff;
}

static inline uint16_t lininoio_encode_cdlen(uint16_t dlen, uint8_t chan_id)
{
	return (((uint16_t)chan_id) << 12) | (dlen & 0xfff);
}

struct lininoio_areply_packet {
	uint8_t type;
	uint8_t status;
	struct lininoio_association_data adata[0];
} __attribute__((packed));

/* Data */

struct lininoio_data_packet {
	uint8_t type;
	uint16_t cdlen;
	uint8_t data[0];
} __attribute__((packed));

#endif /* __LININOIO_H__ */
