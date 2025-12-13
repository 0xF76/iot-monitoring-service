#pragma once

#include <stddef.h>
#include <stdint.h>

#define TLV_TYPE_DISCOVER_REQUEST   0x01
#define TLV_TYPE_DISCOVER_RESPONSE  0x02
#define TLV_TYPE_LIST_REQUEST       0x10
#define TLV_TYPE_LIST_RESPONSE      0x11
#define TLV_TYPE_GET_REQUEST        0x13
#define TLV_TYPE_GET_RESPONSE       0x14
#define TLV_TYPE_SET_REQUEST        0x15
#define TLV_TYPE_SET_RESPONSE       0x16

typedef struct {
    uint16_t type;
    uint16_t length;
    // Followed by 'length' bytes of value
} __attribute__((packed)) tlv_header_t;


typedef struct {
    uint32_t device_id;
    float temperature;
    uint8_t battery;
    uint8_t status; // 0=OFFLINE, 1=ONLINE, 2=ERROR
} __attribute__((packed)) device_status_t;


int send_tlv(int fd, uint16_t type, const void *value, uint16_t length);
int recv_tlv(int fd, uint16_t *type, void *buf, uint16_t bufsize, uint16_t *out_length);

int tlv_encode_buf(uint8_t *out, size_t out_size, uint16_t type, const void *value, uint16_t len, size_t *out_len);
int tlv_decode_buf(const uint8_t *in, size_t in_size, uint16_t *out_type, const uint8_t **out_value, uint16_t *out_len);