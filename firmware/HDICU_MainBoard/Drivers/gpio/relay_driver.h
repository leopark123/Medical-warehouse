/**
 * @file    relay_driver.h
 * @brief   Relay output driver — maps logical relay IDs to physical GPIO
 * @note    9 relays driven by ULN2003A(U5)/ULN2001(U34)/ULN2001(U33)
 *
 *          GPIO pin mapping requires visual confirmation from schematic.
 *          The signal names and their MCU port/pin connections are:
 *
 *          Schematic-confirmed GPIO (原理图 PIU24 NL网表 2026-04-09):
 *          Signal        | GPIO | ULN Chip
 *          --------------|------|----------
 *          PTC-IO        | PE1  | U5
 *          JIARE-IO      | PE0  | U5
 *          RED-IO        | PB9  | U5
 *          ZIY-IO        | PB8  | U5
 *          O2-IO         | PB7  | U5
 *          JIASHI-IO     | PE4  | U34
 *          FENGJI-IO     | PE3  | U34
 *          YASUO-IO      | PE2  | U34
 *          WH-IO         | PB4  | U33
 */

#ifndef RELAY_DRIVER_H
#define RELAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize all relay GPIO pins as output, all OFF */
void relay_driver_init(void);

/* Apply relay bitmap to physical outputs.
 * Bit N corresponds to BSP_RELAY_xxx index N.
 * 1 = ON, 0 = OFF */
void relay_driver_apply(uint16_t bitmap);

/* Set/clear individual relay */
void relay_driver_set(uint8_t relay_idx, bool on);

/* Read current physical state */
uint16_t relay_driver_read(void);

#endif
