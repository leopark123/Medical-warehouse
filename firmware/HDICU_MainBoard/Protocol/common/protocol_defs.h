/**
 * @file    protocol_defs.h
 * @brief   Protocol constants shared between iPad and Screen protocols
 * @note    iPad frame: AA [CMD] [LEN] [DATA...] [CS] 55
 *          Screen frame: AA 55 [CMD] [LEN] [DATA...] [CS] ED
 *
 *          iPad checksum INCLUDES 0xAA: CS = (AA+CMD+LEN+DATA) & 0xFF
 *          Screen checksum EXCLUDES header: CS = (CMD+LEN+DATA) & 0xFF
 */

#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*  iPad Protocol (Frozen — 冻结总纲 第三章)                                 */
/* ========================================================================= */
#define IPAD_FRAME_HEAD             0xAA
#define IPAD_FRAME_TAIL             0x55
#define IPAD_FRAME_OVERHEAD         4       /* HEAD + CMD + LEN + CS + TAIL = 4 + LEN */

/* iPad APP -> MainController commands */
#define IPAD_CMD_READ_PARAMS        0x01    /* Data len=0, response=0x02 */
#define IPAD_CMD_WRITE_PARAMS       0x03    /* Data len=22, response=0x04 */
#define IPAD_CMD_READ_VITALS        0x05    /* Data len=0, response=0x06 */
#define IPAD_CMD_READ_CURVE         0x07    /* Data len=1, response=0x08 (RESERVED) */

/* MainController -> iPad APP responses */
#define IPAD_RSP_PARAMS             0x02    /* Data len=34 */
#define IPAD_RSP_WRITE_ACK          0x04    /* Data len=2 */
#define IPAD_RSP_VITALS             0x06    /* Data len=20 */
#define IPAD_RSP_CURVE              0x08    /* Data len=102 (RESERVED) */
#define IPAD_RSP_ERROR              0xFF    /* Data len=2 */

/* iPad data lengths */
#define IPAD_WRITE_DATA_LEN         22
#define IPAD_PARAMS_RSP_LEN         34
#define IPAD_WRITE_ACK_LEN          2
#define IPAD_VITALS_RSP_LEN         20
#define IPAD_CURVE_RSP_LEN          102     /* RESERVED: not implemented this phase */
#define IPAD_ERROR_RSP_LEN          2

/* iPad write result codes */
#define IPAD_WRITE_OK               0x00
#define IPAD_WRITE_PARAM_OOB        0x01    /* Parameter out of bounds */
#define IPAD_WRITE_CMD_ERR          0x02    /* Command error */

/* iPad write error detail codes */
#define IPAD_ERR_NONE               0x00
#define IPAD_ERR_TEMP_OOB           0x01
#define IPAD_ERR_HUMID_OOB          0x02
#define IPAD_ERR_O2_OOB             0x03
#define IPAD_ERR_CO2_OOB            0x04
#define IPAD_ERR_FOG_OOB            0x05
#define IPAD_ERR_DISINFECT_OOB      0x06
#define IPAD_ERR_FAN_OOB            0x07
#define IPAD_ERR_NURSING_OOB        0x08

/* iPad error response types */
#define IPAD_EXCEPT_CMD_UNSUPPORTED 0x01
#define IPAD_EXCEPT_PARAM_OOB       0x02
#define IPAD_EXCEPT_CHECKSUM_FAIL   0x03    /* NOTE: checksum fail => silent discard, not 0xFF */
#define IPAD_EXCEPT_LEN_ERR         0x04    /* NOTE: length error => silent discard, not 0xFF */

/* iPad error position codes */
#define IPAD_EPOS_NONE              0x00
#define IPAD_EPOS_HEAD              0x01
#define IPAD_EPOS_CMD               0x02
#define IPAD_EPOS_LEN               0x03
#define IPAD_EPOS_CHECKSUM          0x04
#define IPAD_EPOS_TAIL              0x05

/* iPad timing */
#define IPAD_QUERY_INTERVAL_MS      500
#define IPAD_RESPONSE_DEADLINE_MS   100
#define IPAD_COMM_TIMEOUT_MS        1000
#define IPAD_DISCONNECT_TIMEOUT_MS  3000

/* ========================================================================= */
/*  Screen (Dual-board) Protocol (Frozen — 开发基线 第五章)                   */
/* ========================================================================= */
#define SCR_FRAME_HEAD1             0xAA
#define SCR_FRAME_HEAD2             0x55
#define SCR_FRAME_TAIL              0xED

/* MainController -> Screen commands */
#define SCR_CMD_DISPLAY_DATA        0x01    /* 100ms, 26 bytes */
#define SCR_CMD_STATUS_SYNC         0x02
#define SCR_CMD_ALARM_NOTIFY        0x03
#define SCR_CMD_HEARTBEAT           0x04    /* 1s, 8 bytes */
#define SCR_CMD_TIME_SYNC           0x05

/* Screen -> MainController commands */
#define SCR_CMD_PARAM_SET           0x81    /* ParamID(1B) + Value(2B) */
#define SCR_CMD_KEY_ACTION          0x82    /* KeyID(1B) + ActionType(1B) */
#define SCR_CMD_TIMER_CTRL          0x83    /* TimerType(1B) + Cmd(1B) + Duration(2B) */
#define SCR_CMD_HEARTBEAT_ACK       0x84
#define SCR_CMD_ALARM_ACK           0x85    /* AlarmID(1B) */

/* Screen display data packet length */
#define SCR_DISPLAY_DATA_LEN        26

/* Screen communication timing */
#define SCR_REFRESH_PERIOD_MS       100
#define SCR_HEARTBEAT_PERIOD_MS     1000
#define SCR_HEARTBEAT_TIMEOUT_MS    30000

/* ========================================================================= */
/*  Common Frame Parser Types                                                */
/* ========================================================================= */
#define PROTOCOL_MAX_DATA_LEN       128     /* Max data payload */

typedef enum {
    FRAME_PARSE_IDLE = 0,
    FRAME_PARSE_HEAD2,          /* Screen only: waiting for 0x55 */
    FRAME_PARSE_CMD,
    FRAME_PARSE_LEN,
    FRAME_PARSE_DATA,
    FRAME_PARSE_CHECKSUM,
    FRAME_PARSE_TAIL,
    FRAME_PARSE_COMPLETE,
    FRAME_PARSE_ERROR,
} FrameParseState_t;

typedef struct {
    FrameParseState_t state;
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  data[PROTOCOL_MAX_DATA_LEN];
    uint8_t  data_idx;
    uint8_t  checksum_rx;
    uint8_t  checksum_calc;
    bool     has_double_header;     /* true for screen protocol (AA 55) */
} FrameParser_t;

/* Initialize parser for iPad (single header) or Screen (double header) */
void frame_parser_init(FrameParser_t *p, bool double_header);

/* Feed one byte. Returns true when a complete valid frame is ready. */
bool frame_parser_feed(FrameParser_t *p, uint8_t byte);

/* Build a response frame into buf. Returns total frame length.
 * For iPad: AA CMD LEN DATA... CS 55   (checksum includes 0xAA)
 * For Screen: AA 55 CMD LEN DATA... CS ED (checksum excludes header) */
uint16_t frame_build_ipad(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint8_t len);
uint16_t frame_build_screen(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_DEFS_H */
