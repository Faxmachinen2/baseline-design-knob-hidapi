// Copyright 2025 Morgan Newell Sun (@eynsai)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "keycodes.h"
#include "rgblight.h"
#include QMK_KEYBOARD_H
#include "quantum.h"
#include "modifiers.h"
#include "knob.h"

#include "../../knob.h"
#include "./config.h"

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(LT(2, KC_MPRV),    KC_MPLY,            KC_MNXT     ),
    [1] = LAYOUT(LT(2, KC_HOME),    KC_LSFT,            KC_END      ),
    [2] = LAYOUT(_______,           TO(0),              TO(1)       ),
    [3] = LAYOUT(_______,           _______,            _______     ),
    [4] = LAYOUT(_______,           _______,            _______     ),
    [5] = LAYOUT(_______,           _______,            _______     ),
    [6] = LAYOUT(_______,           _______,            _______     ),
    [7] = LAYOUT(_______,           _______,            _______     )
};
// clang-format on

// ============================================================================
// STATE VARIABLES
// ============================================================================

enum value_ids {

    id_mode                     = 0,
    id_backlight                = 1,
    id_sensitivity              = 2,
    id_reverse                  = 3,
    id_acceleration             = 4,
    id_scroll_direction         = 5,
    id_drag_direction           = 6,
    id_drag_button              = 7,
    id_drag_modifiers           = 8,
    id_midi_channel             = 9,
    id_midi_cc                  = 10,
    LAYER_CONFIG_8_SIZE         = 11,

    id_backlight_color          = 11,
    id_encoder_keycode_cw       = 13,
    id_encoder_keycode_ccw      = 15,
    LAYER_CONFIG_SIZE           = 17,

    id_midi_mode                = 17,
    GLOBAL_CONFIG_SIZE          = 18 - LAYER_CONFIG_SIZE,

};

typedef struct {
    uint8_t layers[DYNAMIC_KEYMAP_LAYER_COUNT][LAYER_CONFIG_SIZE];
    uint8_t global[GLOBAL_CONFIG_SIZE];
} custom_config_t;
custom_config_t custom_config = {0};
_Static_assert(sizeof(custom_config_t) <= VIA_EEPROM_CUSTOM_CONFIG_SIZE, "VIA custom config size too small");

uint8_t highest_layer = 0;
uint16_t encoder_keycode_cw = KC_NO;
uint16_t encoder_keycode_ccw = KC_NO;

// ============================================================================
// KNOB CONFIG
// ============================================================================

void update_active_config(void) {

    knob_config_t knob_config = default_knob_config;

    // set parameters used by all modes
    knob_config.sensitivity = custom_config.layers[highest_layer][id_sensitivity] + 1;
    knob_config.acceleration = custom_config.layers[highest_layer][id_acceleration];
    knob_config.reverse = custom_config.layers[highest_layer][id_reverse];
    if (custom_config.layers[highest_layer][id_backlight]) {
        rgblight_enable_noeeprom();
        rgblight_sethsv_noeeprom(custom_config.layers[highest_layer][id_backlight_color], custom_config.layers[highest_layer][id_backlight_color + 1], rgblight_get_val());
    } else {
        rgblight_disable_noeeprom();
    }

    switch (custom_config.layers[highest_layer][id_mode]) {

        // off
        case 0:
            knob_config.mode = KNOB_MODE_OFF;
            break;

        // encoder
        case 1:
            knob_config.mode = KNOB_MODE_ENCODER;
            encoder_keycode_cw = custom_config.layers[highest_layer][id_encoder_keycode_cw] << 8 | custom_config.layers[highest_layer][id_encoder_keycode_cw + 1];
            encoder_keycode_ccw = custom_config.layers[highest_layer][id_encoder_keycode_ccw] << 8 | custom_config.layers[highest_layer][id_encoder_keycode_ccw + 1];
            break;

        // scroll wheel
        case 2:
            knob_config.mode = KNOB_MODE_WHEEL_VERTICAL + custom_config.layers[highest_layer][id_scroll_direction];
            break;

        // mouse drag (automatic)
        case 3:
            knob_config.mode = KNOB_MODE_ADAPTIVE_DRAG_VERTICAL + custom_config.layers[highest_layer][id_drag_direction];
            knob_config.drag_button = custom_config.layers[highest_layer][id_drag_button];
            knob_config.drag_modifiers = custom_config.layers[highest_layer][id_drag_modifiers];
            break;

        // mouse drag (always on)
        case 4:
            knob_config.mode = KNOB_MODE_DRAG_VERTICAL + custom_config.layers[highest_layer][id_drag_direction];
            knob_config.drag_button = custom_config.layers[highest_layer][id_drag_button];
            knob_config.drag_modifiers = custom_config.layers[highest_layer][id_drag_modifiers];
            break;

        // midi relative cc
        case 5:
            knob_config.mode = KNOB_MODE_MIDI;
            knob_config.midi_channel = custom_config.layers[highest_layer][id_midi_channel];
            knob_config.midi_cc = custom_config.layers[highest_layer][id_midi_cc];
            knob_config.midi_mode = custom_config.global[id_midi_mode - LAYER_CONFIG_SIZE];
            break;
    }

    // write the constructed config
    set_knob_config(knob_config);
}

// ============================================================================
// VIA AND EEPROM
// ============================================================================

void custom_config_set_value(uint8_t *data) {
    uint8_t value_id = data[0];
    if (value_id < LAYER_CONFIG_SIZE) {
        uint8_t value_layer = data[1];
        custom_config.layers[value_layer][value_id] = data[2];
        if (value_id >= LAYER_CONFIG_8_SIZE) {
            custom_config.layers[value_layer][value_id + 1] = data[3];
        }
    } else {
        custom_config.global[value_id - LAYER_CONFIG_SIZE] = data[1];
    }
}

void custom_config_get_value(uint8_t *data) {
    uint8_t value_id = data[0];
    if (value_id < LAYER_CONFIG_SIZE) {
        uint8_t value_layer = data[1];
        data[2] = custom_config.layers[value_layer][value_id];
        if (value_id >= LAYER_CONFIG_8_SIZE) {
            data[3] = custom_config.layers[value_layer + 1][value_id];
        }
    } else {
        data[1] = custom_config.global[value_id - LAYER_CONFIG_SIZE];
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id = &(data[0]);
    uint8_t *channel_id = &(data[1]);
    uint8_t *value_id_and_data = &(data[2]);
    if (*channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }
    switch (*command_id) {
        case id_custom_set_value:
            custom_config_set_value(value_id_and_data);
            update_active_config();
            break;
        case id_custom_get_value:
            custom_config_get_value(value_id_and_data);
            break;
        case id_custom_save:
            via_update_custom_config((const void *)&custom_config, 0, sizeof(custom_config_t));
            update_active_config();
            break;
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

// ============================================================================
// QMK HOOKS
// ============================================================================

void keyboard_post_init_user(void) {
    via_read_custom_config((void *)&custom_config, 0, sizeof(custom_config_t));
    update_active_config();
}

// called when EEPROM is reset
void eeconfig_init_user(void) {
    memset(&custom_config, 0, sizeof(custom_config_t));

    custom_config.layers[0][id_mode] = KNOB_MODE_ENCODER;
    custom_config.layers[0][id_backlight] = 1;
    custom_config.layers[0][id_sensitivity] = KNOB_VIA_DEFAULT_SENS_0;
    custom_config.layers[0][id_reverse] = 0;
    custom_config.layers[0][id_backlight_color] = KNOB_VIA_DEFAULT_HUE_0;  // hue
    custom_config.layers[0][id_backlight_color + 1] = KNOB_VIA_DEFAULT_SAT_0;  // saturation
    custom_config.layers[0][id_encoder_keycode_cw] = (KC_VOLU >> 8) & 0xFFFF;  // MSB
    custom_config.layers[0][id_encoder_keycode_cw + 1] = KC_VOLU & 0xFFFF;  // LSB
    custom_config.layers[0][id_encoder_keycode_ccw] = (KC_VOLD >> 8) & 0xFFFF;  // MSB
    custom_config.layers[0][id_encoder_keycode_ccw + 1] = KC_VOLD & 0xFFFF;  // LSB

    custom_config.layers[1][id_mode] = KNOB_MODE_WHEEL_VERTICAL;
    custom_config.layers[1][id_backlight] = 1;
    custom_config.layers[1][id_sensitivity] = KNOB_VIA_DEFAULT_SENS_1;
    custom_config.layers[1][id_reverse] = 1;
    custom_config.layers[1][id_backlight_color] = KNOB_VIA_DEFAULT_HUE_1;  // hue
    custom_config.layers[1][id_backlight_color + 1] = KNOB_VIA_DEFAULT_SAT_1;  // saturation

    custom_config.layers[2][id_mode] = KNOB_MODE_OFF;
    custom_config.layers[2][id_backlight] = 0;

    via_update_custom_config((const void *)&custom_config, 0, sizeof(custom_config_t));
    update_active_config();
}

layer_state_t layer_state_set_user(layer_state_t state) {
    highest_layer = get_highest_layer(state);
    update_active_config();
    return state;
}

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (clockwise) {
        tap_code(encoder_keycode_cw);
    } else {
        tap_code(encoder_keycode_ccw);
    }
    return false;
}
