/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_leader_key

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/matrix.h>
#include <zmk/keymap.h>

#include <zmk-leader-key/keys.h>
#include <zmk-leader-key/virtual_key_position.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct leader_seq_cfg {
    int32_t virtual_key_position;
    bool is_pressed;
    size_t keys_len;
    struct zmk_key_param keys[CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE];
    struct zmk_behavior_binding behavior;
};

struct behavior_leader_key_config {
    size_t sequences_len;
    struct leader_seq_cfg *sequences;
};

const struct behavior_leader_key_config *active_leader_cfg;

// State of currently active leader key instance.
static bool is_undecided;
static int32_t press_count;   /* Total number of pressed keys */
static int32_t release_count; /* Number of currently pressed keys */
static int32_t num_candidates;
static struct leader_seq_cfg *sequence_candidates[CONFIG_ZMK_LEADER_MAX_SEQUENCES];
static struct leader_seq_cfg *completed_sequence;

// Keep track of pressed keys so we can handle their release events.
static struct zmk_key_param leader_pressed_keys[CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE];

static inline int press_leader_behavior(struct leader_seq_cfg *sequence, int32_t timestamp) {
    LOG_DBG("Pressing leader binding");
    struct zmk_behavior_binding_event event = {
        // Assign unique virtual key position to each sequence to work along hold-taps.
        .position = sequence->virtual_key_position,
        .timestamp = timestamp,
    };

    sequence->is_pressed = true;
    return zmk_behavior_invoke_binding(&sequence->behavior, event, true);
}

static inline int release_leader_behavior(struct leader_seq_cfg *sequence, int32_t timestamp) {
    LOG_DBG("Releasing leader binding");
    struct zmk_behavior_binding_event event = {
        .position = sequence->virtual_key_position,
        .timestamp = timestamp,
    };

    sequence->is_pressed = false;
    return zmk_behavior_invoke_binding(&sequence->behavior, event, false);
}

void activate_leader_key(const struct behavior_leader_key_config *cfg) {
    LOG_DBG("Activating leader key");
    active_leader_cfg = cfg;
    is_undecided = true;
    press_count = 0;
    release_count = 0;
    completed_sequence = NULL;
    num_candidates = cfg->sequences_len;
    for (int i = 0; i < cfg->sequences_len; i++) {
        sequence_candidates[i] = &(cfg->sequences[i]);
    }
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE; i++) {
        leader_pressed_keys[i] = (struct zmk_key_param){0};
    }
};

void deactivate_leader_key() {
    LOG_DBG("Deactivating leader key");
    is_undecided = false;
};

// This function compares two key parameters and returns true if they are equal. To be equal, the
// modifier bit mask of the second argument along with any explicit modifiers must weakly contain
// the one of the first argument.
static bool key_param_equals(const struct zmk_key_param *key, const struct zmk_key_param *other) {
    return key->page == other->page && key->id == other->id &&
           (key->modifiers & (other->modifiers | zmk_hid_get_explicit_mods())) == key->modifiers;
}

// This function filters out candidate sequences that are no longer possible given the pressed
// sequence of keys. The function returns false if no sequences are possible and otherwise true. If
// a sequence is completed, it is stored in completed_sequence.
static bool filter_leader_sequences(const struct zmk_key_param *key, int count) {
    int n = 0; /* New number of candidates */
    for (int i = 0; i < num_candidates; i++) {
        struct leader_seq_cfg *seq = sequence_candidates[n];
        if (key_param_equals(&seq->keys[count], key)) {
            if (seq->keys_len == count + 1) {
                LOG_DBG("Sequence completed by 0x%02X - 0x%02X", key->page, key->id);
                completed_sequence = seq;
                return true;
            }
            n++;
        } else {
            for (int j = n; j < num_candidates - (i - n) - 1; j++) {
                sequence_candidates[j] = sequence_candidates[j + 1];
            }
        }
    }

    LOG_DBG("Pressed usage_page 0x%02X keycode 0x%02X implicit_mods 0x%02X, %d candidate sequences "
            "remaining",
            key->page, key->id, key->modifiers, n);
    num_candidates = n;
    return (n > 0);
};

static bool release_key_in_leader_sequence(const struct zmk_key_param *key) {
    for (int i = 0; i < release_count; i++) {
        if (key_param_equals(&leader_pressed_keys[i], key)) {
            for (int j = i; j < release_count; j++) {
                leader_pressed_keys[j] = leader_pressed_keys[j + 1];
            }
            release_count--;
            LOG_DBG("Released usage_page 0x%02X keycode 0x%02X implicit_mods 0x%02X, %d pressed "
                    "keys remaining",
                    key->page, key->id, key->modifiers, release_count);
            return true;
        }
    }
    return false;
}

static int leader_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_key_param key = {
        .modifiers = ev->implicit_modifiers,
        .page = ev->usage_page,
        .id = ev->keycode,
    };

    // A key is pressed while leader is active.
    if (is_undecided && ev->state) {
        if (filter_leader_sequences(&key, press_count++)) {
            leader_pressed_keys[release_count++] = key;
            if (completed_sequence) {
                deactivate_leader_key();
                press_leader_behavior(completed_sequence, ev->timestamp);
            }
            return ZMK_EV_EVENT_HANDLED;
        } else {
            deactivate_leader_key();
            return ZMK_EV_EVENT_BUBBLE;
        }
    }

    // A key in the current sequence is released. Release the invoked behavior if its the last one.
    if (!ev->state && release_key_in_leader_sequence(&key)) {
        if (completed_sequence && completed_sequence->is_pressed && !release_count) {
            release_leader_behavior(completed_sequence, ev->timestamp);
        }
        return ZMK_EV_EVENT_HANDLED;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(leader, leader_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(leader, zmk_keycode_state_changed);

static int behavior_leader_key_init(const struct device *dev) {
    // const struct behavior_leader_key_config *cfg = dev->config;
    // for (int i = 0; i < cfg->sequences_len; i++) {
    //     initialize_leader_sequences(&cfg->sequences[i]);
    // }
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    if (release_count) {
        LOG_ERR("Unable to activate leader key. Previous sequence is still pressed.");
        return ZMK_BEHAVIOR_OPAQUE;
    }
    const struct device *dev = device_get_binding(binding->behavior_dev);
    activate_leader_key(dev->config);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_leader_key_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define SEQUENCE_ITEM(i, n, prop) ZMK_KEY_PARAM_DECODE(DT_PROP_BY_IDX(n, prop, i))

#define PROP_SEQUENCES(n, prop)                                                                    \
    {                                                                                              \
        .virtual_key_position = ZMK_VIRTUAL_KEY_POSITION_LEADER(__COUNTER__),                      \
        .is_pressed = false,                                                                       \
        .keys_len = DT_PROP_LEN(n, prop),                                                          \
        .keys = {LISTIFY(DT_PROP_LEN(n, prop), SEQUENCE_ITEM, (, ), n, prop)},                     \
        .behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, n),                                              \
    }

// int zmk_leader_sequence_compare(const void *a, const void *b) {
//     struct leader_seq_cfg *seq_a = (struct leader_seq_cfg *)a;
//     struct leader_seq_cfg *seq_b = (struct leader_seq_cfg *)b;
//     if (seq_a->keys_len < seq_b->keys_len) {
//         return 1;
//     }
//     if (seq_a->keys_len > seq_b->keys_len) {
//         return -1;
//     }
//     if (seq_a->virtual_key_position < seq_b->virtual_key_position) {
//         return 1;
//     }
//     if (seq_a->virtual_key_position > seq_b->virtual_key_position) {
//         return -1;
//     }
//     return 0;
// }
//
// qsort(leader_sequences_##n, ARRAY_SIZE(leader_sequences_##n), sizeof(leader_sequences_##n[0])
//       zmk_leader_sequence_compare);

#define LEAD_INST(n)                                                                               \
    static struct leader_seq_cfg leader_sequences_##n[] = {                                        \
        DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(n, PROP_SEQUENCES, (, ), key_sequence)};       \
    static struct behavior_leader_key_config behavior_leader_key_config_##n = {                    \
        .sequences = leader_sequences_##n,                                                         \
        .sequences_len = ARRAY_SIZE(leader_sequences_##n),                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_leader_key_init, NULL, NULL,                               \
                            &behavior_leader_key_config_##n, POST_KERNEL,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_leader_key_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LEAD_INST)
