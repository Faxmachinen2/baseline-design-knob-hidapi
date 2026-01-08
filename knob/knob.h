// Copyright 2025 Morgan Newell Sun (@eynsai)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
// #include "pointing_device.h"
#include "quantum.h"

uint16_t get_as5600_raw(void);
int16_t get_as5600_delta(void);

#ifndef KNOB_MINIMAL

typedef enum {
    KNOB_MODE_OFF = 0,
#    ifdef ENCODER_ENABLE
    KNOB_MODE_ENCODER,
#    endif  // ENCODER_ENABLE
#    ifdef POINTING_DEVICE_ENABLE
    KNOB_MODE_WHEEL_VERTICAL,
    KNOB_MODE_WHEEL_HORIZONTAL,
    KNOB_MODE_DRAG_VERTICAL,
    KNOB_MODE_DRAG_HORIZONTAL,
    KNOB_MODE_DRAG_DIAGONAL,
    KNOB_MODE_ADAPTIVE_DRAG_VERTICAL,
    KNOB_MODE_ADAPTIVE_DRAG_HORIZONTAL,
    KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL,
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
    KNOB_MODE_MIDI,
#    endif  // MIDI_ENABLE
} knob_mode_t;

#    ifdef MIDI_ENABLE
typedef enum {
    MIDI_MODE_SIGNED,
    MIDI_MODE_OFFSET,
    MIDI_MODE_TWOS,
} midi_mode_t;
#    endif  // MIDI_ENABLE

typedef struct {
    knob_mode_t mode;
    uint8_t sensitivity;
    bool acceleration;
    bool reverse;
#    ifdef POINTING_DEVICE_ENABLE
    pointing_device_buttons_t drag_button;
    uint8_t drag_modifiers;
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
    uint8_t midi_channel;
    uint8_t midi_cc;
    midi_mode_t midi_mode;
#    endif  // MIDI_ENABLE
} knob_config_t;

static const knob_config_t default_knob_config = {
    .mode = KNOB_MODE_OFF,
    .sensitivity = 10,
    .acceleration = false,
    .reverse = false,
#    ifdef POINTING_DEVICE_ENABLE
    .drag_button = POINTING_DEVICE_BUTTON1,
    .drag_modifiers = 0,
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
    .midi_channel = 0,
    .midi_cc = 0,
    .midi_mode = MIDI_MODE_SIGNED,
#    endif
};

knob_config_t get_knob_config(void);
void set_knob_config(knob_config_t config);
void reset_knob_config(void);

#endif  // !KNOB_MINIMAL