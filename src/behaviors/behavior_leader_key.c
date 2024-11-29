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

#include <zmk-leader-key/virtual_key_position.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct leader_sequence_config {
    int32_t key_position_len;
    int32_t key_positions[CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE];

    bool is_pressed;
    // Assign unique virtual key position to each sequence to work along hold-taps.
    int32_t virtual_key_position;
    struct zmk_behavior_binding behavior;
};

struct behavior_leader_key_config {
    size_t sequences_len;
    struct leader_sequence_config *sequences;
};

struct active_leader_key {
    bool is_undecided;
    int32_t press_count;
    int32_t release_count;
    int32_t active_leader_position;
    bool first_release;
    const struct behavior_leader_key_config *config;
};
static struct active_leader_key active_leader_key = {};

static uint32_t current_sequence[CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE] = {-1};

// This is a variant of current_sequence where keys are removed as they are released.
static const struct zmk_position_state_changed
    *leader_pressed_keys[CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE] = {NULL};

// the set of candidate leader based on the currently leader_pressed_keys
static int candidates_len;
static struct leader_sequence_config *sequence_candidates[CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY];

// TODO: simplify handling of completed_sequence_candidates as we no longer allow for nested
// sequences
static int num_comp_candidates;
static struct leader_sequence_config
    *completed_sequence_candidates[CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY];
// a lookup dict that maps a key position to all sequences on that position
static struct leader_sequence_config
    *sequence_lookup[ZMK_KEYMAP_LEN][CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY] = {NULL};

// Store the sequence key pointer in the sequence_lookup array, one pointer for each key position.
// The sequences are sorted shortest-first, then by virtual-key-position.
static int initialize_leader_sequences(struct leader_sequence_config *seq) {
    for (int i = 0; i < seq->key_position_len; i++) {
        int32_t position = seq->key_positions[i];
        if (position >= ZMK_KEYMAP_LEN) {
            LOG_ERR("Unable to initialize leader, key position %d does not exist", position);
            return -EINVAL;
        }

        struct leader_sequence_config *new_seq = seq;
        bool set = false;
        for (int j = 0; j < CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY; j++) {
            struct leader_sequence_config *sequence_at_j = sequence_lookup[position][j];
            if (sequence_at_j == NULL) {
                sequence_lookup[position][j] = new_seq;
                set = true;
                break;
            }
            if (sequence_at_j->key_position_len < new_seq->key_position_len ||
                (sequence_at_j->key_position_len == new_seq->key_position_len &&
                 sequence_at_j->virtual_key_position < new_seq->virtual_key_position)) {
                continue;
            }
            // Put new_seq in this spot, move all other leader up.
            sequence_lookup[position][j] = new_seq;
            new_seq = sequence_at_j;
        }
        if (!set) {
            LOG_ERR(
                "Too many leader for key position %d, CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY %d.",
                position, CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY);
            return -ENOMEM;
        }
    }
    return 0;
}

static bool has_current_sequence(struct leader_sequence_config *sequence, int count) {
    for (int i = 0; i < count; i++) {
        if (sequence->key_positions[i] != current_sequence[i]) {
            return false;
        }
    }
    return true;
}

static bool is_in_current_sequence(int32_t position) {
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE; i++) {
        if (position == current_sequence[i]) {
            return true;
        }
    }
    return false;
}

static bool is_duplicate(struct leader_sequence_config *seq) {
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE; i++) {
        if (sequence_candidates[i] == seq) {
            return true;
        }
    }
    return false;
}

static bool release_key_in_sequence(int32_t position) {
    for (int i = 0; i < active_leader_key.release_count; i++) {
        if (leader_pressed_keys[i] && position == leader_pressed_keys[i]->position) {
            leader_pressed_keys[i] = NULL;
            return true;
        }
    }
    return false;
}

static bool all_keys_released() {
    for (int i = 0; i < active_leader_key.press_count; i++) {
        if (NULL != leader_pressed_keys[i]) {
            return false;
        }
    }
    return true;
}

static void clear_candidates() {
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY; i++) {
        sequence_candidates[i] = NULL;
        completed_sequence_candidates[i] = NULL;
    }
}

static void leader_find_candidates(int32_t position, int count) {
    clear_candidates();
    candidates_len = 0;
    num_comp_candidates = 0;
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_SEQUENCES_PER_KEY; i++) {
        struct leader_sequence_config *sequence = sequence_lookup[position][i];
        if (sequence == NULL) {
            continue;
        }
        if (sequence->key_positions[count] == position && has_current_sequence(sequence, count) &&
            !is_duplicate(sequence)) {
            sequence_candidates[candidates_len] = sequence;
            candidates_len++;
            if (sequence->key_position_len == count + 1) {
                completed_sequence_candidates[num_comp_candidates] = sequence;
                num_comp_candidates++;
            }
        }
    }
}

const struct zmk_listener zmk_listener_leader;

static inline int press_leader_behavior(struct leader_sequence_config *sequence,
                                        int32_t timestamp) {
    struct zmk_behavior_binding_event event = {
        .position = sequence->virtual_key_position,
        .timestamp = timestamp,
    };

    sequence->is_pressed = true;
    return zmk_behavior_invoke_binding(&sequence->behavior, event, true);
}

static inline int release_leader_behavior(struct leader_sequence_config *sequence,
                                          int32_t timestamp) {
    struct zmk_behavior_binding_event event = {
        .position = sequence->virtual_key_position,
        .timestamp = timestamp,
    };

    sequence->is_pressed = false;
    return zmk_behavior_invoke_binding(&sequence->behavior, event, false);
}

void activate_leader_sequence(uint32_t position, const struct behavior_leader_key_config *cfg) {
    LOG_DBG("leader key activated");
    active_leader_key.is_undecided = true;
    active_leader_key.config = cfg;
    active_leader_key.press_count = 0;
    active_leader_key.release_count = 0;
    active_leader_key.active_leader_position = position;
    active_leader_key.first_release = false;
    for (int i = 0; i < CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE; i++) {
        leader_pressed_keys[i] = NULL;
    }
};

void deactivate_leader_sequence() {
    LOG_DBG("leader key deactivated");
    active_leader_key.is_undecided = false;
    clear_candidates();
};

static int position_state_changed_listener(const zmk_event_t *ev) {
    struct zmk_position_state_changed *data = as_zmk_position_state_changed(ev);
    if (data == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Handle release of keys that are part of a completed sequence.
    if (!active_leader_key.is_undecided && !data->state && !all_keys_released()) {
        if (release_key_in_sequence(data->position)) {
            return ZMK_EV_EVENT_HANDLED;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (active_leader_key.is_undecided) {
        if (data->state) { // keydown
            leader_find_candidates(data->position, active_leader_key.press_count);
            LOG_DBG("leader cands: %d comp: %d", candidates_len, num_comp_candidates);
            if (candidates_len == 0) {
                deactivate_leader_sequence();
                return ZMK_EV_EVENT_BUBBLE;
            }
            current_sequence[active_leader_key.press_count] = data->position;
            leader_pressed_keys[active_leader_key.press_count] = data;
            active_leader_key.press_count++;
            for (int i = 0; i < num_comp_candidates; i++) {
                struct leader_sequence_config *seq = completed_sequence_candidates[i];
                if (candidates_len == 1 && num_comp_candidates == 1) {
                    press_leader_behavior(seq, data->timestamp);
                }
            }
        } else { // keyup
            // Don't do anything when the leader key itself is first released.
            if (data->position == active_leader_key.active_leader_position &&
                !active_leader_key.first_release) {
                active_leader_key.first_release = true;
                return ZMK_EV_EVENT_HANDLED;
            }
            if (!is_in_current_sequence(data->position)) {
                return ZMK_EV_EVENT_BUBBLE;
            }

            active_leader_key.release_count++;
            release_key_in_sequence(data->position);

            for (int i = 0; i < num_comp_candidates; i++) {
                struct leader_sequence_config *seq = completed_sequence_candidates[i];
                if (seq->is_pressed && all_keys_released()) {
                    release_leader_behavior(seq, data->timestamp);
                    num_comp_candidates--;
                }
                if (candidates_len == 1 && num_comp_candidates == 0) {
                    deactivate_leader_sequence();
                }
            }
        }
        return ZMK_EV_EVENT_HANDLED;
    }

    return 0;
}

ZMK_LISTENER(leader, position_state_changed_listener);
ZMK_SUBSCRIPTION(leader, zmk_position_state_changed);

static int behavior_leader_key_init(const struct device *dev) {
    const struct behavior_leader_key_config *cfg = dev->config;
    for (int i = 0; i < cfg->sequences_len; i++) {
        initialize_leader_sequences(&cfg->sequences[i]);
    }
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = device_get_binding(binding->behavior_dev);
    activate_leader_sequence(event.position, dev->config);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return 0;
}

static const struct behavior_driver_api behavior_leader_key_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define PROP_SEQUENCES(s)                                                                          \
    {                                                                                              \
        .virtual_key_position = ZMK_VIRTUAL_KEY_POSITION_LEADER(__COUNTER__), .is_pressed = false, \
        .key_positions = DT_PROP(s, key_positions),                                                \
        .key_position_len = DT_PROP_LEN(s, key_positions),                                         \
        .behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, s),                                              \
    }

int zmk_leader_sequence_compare(const void *a, const void *b) {
    struct leader_sequence_config *seq_a = (struct leader_sequence_config *)a;
    struct leader_sequence_config *seq_b = (struct leader_sequence_config *)b;
    if (seq_a->key_position_len < seq_b->key_position_len) {
        return 1;
    }
    if (seq_a->key_position_len > seq_b->key_position_len) {
        return -1;
    }
    if (seq_a->virtual_key_position < seq_b->virtual_key_position) {
        return 1;
    }
    if (seq_a->virtual_key_position > seq_b->virtual_key_position) {
        return -1;
    }
    return 0;
}

    /*qsort(leader_sequences_##n, ARRAY_SIZE(leader_sequences_##n), sizeof(leader_sequences_##n[0]), \*/
    /*      zmk_leader_sequence_compare);                                                            \*/
#define LEAD_INST(n)                                                                               \
    static struct leader_sequence_config leader_sequences_##n[] = {                                \
        DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(n, PROP_SEQUENCES, (, ))};                           \
    static struct behavior_leader_key_config behavior_leader_key_config_##n = {                    \
        .sequences = leader_sequences_##n,                                                         \
        .sequences_len = ARRAY_SIZE(leader_sequences_##n),                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_leader_key_init, NULL, NULL,                               \
                            &behavior_leader_key_config_##n, POST_KERNEL,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_leader_key_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LEAD_INST)
