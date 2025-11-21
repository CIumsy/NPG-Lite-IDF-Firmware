/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef GAP_H
#define GAP_H

/* Includes */
/* NimBLE GAP APIs */
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <stdint.h>

extern bool device_connected;
extern bool streaming;
extern uint16_t conn_handle;
extern uint16_t data_char_handle;
extern uint16_t control_char_handle;

/* Public function declarations */
void adv_init(void);
int gap_init(void);

#endif // GAP_H
