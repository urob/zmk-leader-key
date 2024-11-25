/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/matrix.h>
#include <zmk/sensors.h>

/**
 * Gets the virtual key position to use for the leader sequence with the given index.
 */
#define ZMK_VIRTUAL_KEY_POSITION_LEADER(index) (ZMK_KEYMAP_LEN + ZMK_KEYMAP_SENSORS_LEN + (index))

void zmk_leader_activate(int32_t timeout, bool timeout_on_activation, uint32_t position);
void zmk_leader_deactivate();
