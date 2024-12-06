/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/keys.h>

/**
 * Backport zmk_key_param and decoder from PR #1742.
 */
#ifndef ZMK_KEY_PARAM_DECODE
struct zmk_key_param {
    zmk_mod_flags_t modifiers;
    uint8_t page;
    uint16_t id;
};

#define ZMK_KEY_PARAM_DECODE(param)                                                                \
    (struct zmk_key_param) {                                                                       \
        .modifiers = SELECT_MODS(param), .page = ZMK_HID_USAGE_PAGE(param),                        \
        .id = ZMK_HID_USAGE_ID(param),                                                             \
    }
#endif // ZMK_KEY_PARAM_DECODE
