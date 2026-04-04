/**
 * @file    protocol_frame.c
 * @brief   Generic frame parser and builder for both iPad and Screen protocols
 *
 *          iPad:   AA [CMD] [LEN] [DATA...] [CS] 55
 *                  CS = (0xAA + CMD + LEN + DATA_bytes) & 0xFF
 *
 *          Screen: AA 55 [CMD] [LEN] [DATA...] [CS] ED
 *                  CS = (CMD + LEN + DATA_bytes) & 0xFF
 *
 *          Frame-level errors (bad checksum, bad length, bad head/tail)
 *          => silent discard, no 0xFF response. (Frozen spec)
 */

#include "protocol_defs.h"
#include <string.h>

/* ========================================================================= */
/*  Frame Parser                                                             */
/* ========================================================================= */

void frame_parser_init(FrameParser_t *p, bool double_header)
{
    memset(p, 0, sizeof(*p));
    p->state = FRAME_PARSE_IDLE;
    p->has_double_header = double_header;
}

bool frame_parser_feed(FrameParser_t *p, uint8_t byte)
{
    switch (p->state) {

    case FRAME_PARSE_IDLE:
        if (byte == 0xAA) {
            p->checksum_calc = 0;
            p->data_idx = 0;
            if (p->has_double_header) {
                /* Screen protocol: expect 0x55 next */
                p->state = FRAME_PARSE_HEAD2;
                /* Screen checksum does NOT include header bytes */
            } else {
                /* iPad protocol: checksum includes 0xAA */
                p->checksum_calc = 0xAA;
                p->state = FRAME_PARSE_CMD;
            }
        }
        /* else: discard silently */
        break;

    case FRAME_PARSE_HEAD2:
        /* Screen protocol only */
        if (byte == 0x55) {
            p->state = FRAME_PARSE_CMD;
        } else {
            /* Bad second header byte — reset */
            p->state = FRAME_PARSE_IDLE;
        }
        break;

    case FRAME_PARSE_CMD:
        p->cmd = byte;
        p->checksum_calc += byte;
        p->state = FRAME_PARSE_LEN;
        break;

    case FRAME_PARSE_LEN:
        p->len = byte;
        p->checksum_calc += byte;
        if (p->len > PROTOCOL_MAX_DATA_LEN) {
            /* Length exceeds buffer — discard silently */
            p->state = FRAME_PARSE_IDLE;
        } else if (p->len == 0) {
            p->state = FRAME_PARSE_CHECKSUM;
        } else {
            p->state = FRAME_PARSE_DATA;
        }
        break;

    case FRAME_PARSE_DATA:
        if (p->data_idx < p->len) {
            p->data[p->data_idx++] = byte;
            p->checksum_calc += byte;
        }
        if (p->data_idx >= p->len) {
            p->state = FRAME_PARSE_CHECKSUM;
        }
        break;

    case FRAME_PARSE_CHECKSUM:
        p->checksum_rx = byte;
        if ((p->checksum_calc & 0xFF) != p->checksum_rx) {
            /* Checksum mismatch — discard silently, no 0xFF response */
            p->state = FRAME_PARSE_IDLE;
        } else {
            p->state = FRAME_PARSE_TAIL;
        }
        break;

    case FRAME_PARSE_TAIL: {
        uint8_t expected_tail = p->has_double_header ? SCR_FRAME_TAIL : IPAD_FRAME_TAIL;
        if (byte == expected_tail) {
            p->state = FRAME_PARSE_COMPLETE;
            return true;    /* Frame ready! */
        } else {
            /* Bad tail — discard silently */
            p->state = FRAME_PARSE_IDLE;
        }
        break;
    }

    case FRAME_PARSE_COMPLETE:
    case FRAME_PARSE_ERROR:
        /* Reset if we get more bytes after completion */
        p->state = FRAME_PARSE_IDLE;
        break;
    }

    return false;
}

/* ========================================================================= */
/*  Frame Builders                                                           */
/* ========================================================================= */

uint16_t frame_build_ipad(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint16_t idx = 0;
    uint8_t cs = 0;

    buf[idx++] = IPAD_FRAME_HEAD;       /* 0xAA */
    cs += IPAD_FRAME_HEAD;              /* iPad checksum includes 0xAA */

    buf[idx++] = cmd;
    cs += cmd;

    buf[idx++] = len;
    cs += len;

    for (uint8_t i = 0; i < len; i++) {
        buf[idx++] = data[i];
        cs += data[i];
    }

    buf[idx++] = cs;                    /* Checksum */
    buf[idx++] = IPAD_FRAME_TAIL;      /* 0x55 */

    return idx;
}

uint16_t frame_build_screen(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint16_t idx = 0;
    uint8_t cs = 0;

    buf[idx++] = SCR_FRAME_HEAD1;       /* 0xAA */
    buf[idx++] = SCR_FRAME_HEAD2;       /* 0x55 */
    /* Screen checksum does NOT include header */

    buf[idx++] = cmd;
    cs += cmd;

    buf[idx++] = len;
    cs += len;

    for (uint8_t i = 0; i < len; i++) {
        buf[idx++] = data[i];
        cs += data[i];
    }

    buf[idx++] = cs;                    /* Checksum */
    buf[idx++] = SCR_FRAME_TAIL;        /* 0xED */

    return idx;
}
