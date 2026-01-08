// Copyright 2025 Morgan Newell Sun (@eynsai)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "baseline_design/knob/knob.h"
#include QMK_KEYBOARD_H
#include "quantum.h"
#include "knob.h"

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_1, KC_2, KC_3)
};
// clang-format on

void keyboard_pre_init_user(void) {
    knob_config_t config = {
        .mode = KNOB_MODE_WHEEL_VERTICAL,
        .sensitivity = 8,
        .acceleration = true,
        .reverse = false,
    };
    set_knob_config(config);
}
