// Copyright 2025 Morgan Newell Sun (@eynsai)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <math.h>
#include <assert.h>

// #include "baseline_design/knob/post_config.h"
#include "i2c_master.h"
// #include "modifiers.h"
// #include "pointing_device.h"
// #include "report.h"
// #include "timer.h"
#include "raw_hid.h"

#include "knob.h"

// ============================================================================
// AS5600
// ============================================================================

#define AS5600_DEV_ADDR 0x6C  // (0x36 << 1)
#define AS5600_REG_ADDR 0x0C
#define AS5600_LENGTH 2
#define AS5600_TIMEOUT 100
#define AS5600_MASK 0x0FFF

int16_t as5600_raw = 0;
int16_t as5600_delta = 0;

static void housekeeping_task_read_as5600(void) {

    // save previous raw angle
    int16_t as5600_raw_prev = as5600_raw;
    
    // read raw angle
    uint8_t buffer[2];
    i2c_read_register(AS5600_DEV_ADDR, AS5600_REG_ADDR, buffer, AS5600_LENGTH, AS5600_TIMEOUT);
    int16_t as5600_raw_noisy = AS5600_MASK & (((int16_t)buffer[0] << 8) | buffer[1]);

    // simulated backlash for debouncing
    int16_t as5600_delta_noisy = as5600_raw_noisy - as5600_raw_prev;
    if (as5600_delta_noisy >= 2048) {
        as5600_delta_noisy -= 4096;
    } else if (as5600_delta_noisy < -2048) {
        as5600_delta_noisy += 4096;
    }
    if (as5600_delta_noisy > 1) {
        as5600_raw = as5600_raw_noisy - 1;
    } else if (as5600_delta_noisy < -1) {
        as5600_raw = as5600_raw_noisy + 1;
    }
    if (as5600_raw >= 4096) {
        as5600_raw -= 4096;
    } else if (as5600_raw < 0) {
        as5600_raw += 4096;
    }

    // compute delta
    as5600_delta = as5600_raw - as5600_raw_prev;
    if (as5600_delta >= 2048) {
        as5600_delta -= 4096;
    } else if (as5600_delta < -2048) {
        as5600_delta += 4096;
    }
}

uint16_t get_as5600_raw(void) {
    return as5600_raw;
}

int16_t get_as5600_delta(void) {
    return as5600_delta;
}

#ifndef KNOB_MINIMAL

// ============================================================================
// HID API
// ============================================================================

#    ifdef RAW_ENABLE
#    define HID_REPORT_ID_SET_CONFIG 0xb1
#    define HID_REPORT_ID_SET_LAYER 0xb2
#    define HID_REPORT_ID_SET_RGB 0xb3
#    define HID_REPORT_ID_WHEEL 0xa1
#    define HID_REPORT_ID_BUTTON 0xa2
#    define MINIMUM_UPDATE_THROTTLE 10
#    define HID_WHEEL_SCALE (1.0 / (4096.0 * 6))  // VIA sensitivity multiplies by 2-10x, so this gives 1/revolution at 50% sensitivity

STATIC_ASSERT(sizeof(float) == 4);
typedef float float32_t;

typedef struct {
    uint16_t wheel_deadzone;
    uint16_t update_throttle;
} hid_config_t;
const hid_config_t default_hid_config = { .wheel_deadzone = 8, .update_throttle = KNOB_THROTTLE_MS };

typedef struct {
    uint8_t report_id;
    uint8_t set_wheel_deadzone : 1;
    uint8_t set_update_throttle : 1;
    uint8_t : 6;  // Reserved
    uint8_t reserved00;
    uint8_t reserved01;
    uint16_t wheel_deadzone;
    uint16_t update_throttle;
    uint8_t reserved02[24];
} __attribute__((packed)) hid_report_set_config_t;
STATIC_ASSERT(sizeof(hid_report_set_config_t) == 32);

typedef struct {
    uint8_t report_id;
    uint8_t layer;
    uint8_t reserved00[30];
} __attribute__((packed)) hid_report_set_layer_t;
STATIC_ASSERT(sizeof(hid_report_set_layer_t) == 32);

typedef struct {
    uint8_t report_id;
    uint8_t set_right : 1;
    uint8_t set_middle: 1;
    uint8_t set_left: 1;
    uint8_t : 5;
    rgb_t left;
    rgb_t middle;
    rgb_t right;
    uint8_t reserved00[21];
} __attribute__((packed)) hid_report_set_rgb_t;
STATIC_ASSERT(sizeof(hid_report_set_rgb_t) == 32);

typedef struct {
    uint8_t report_id;
    uint8_t layer;
    uint8_t raw_clockwise : 1;
    uint8_t : 7;
    int16_t raw_delta;  // Without acceleration/sensitivity, 1 rotation = 4096
    uint8_t processed_clockwise : 1;
    uint8_t : 7;
    float32_t processed_delta;  // With acceleration/sensitivity, 1 rotation = 1.0 (at sensitivity 1)
    uint8_t pad00[22];  // Padding
} __attribute__((packed)) hid_report_wheel_t;
STATIC_ASSERT(sizeof(hid_report_wheel_t) == 32);
const hid_report_wheel_t default_hid_report_wheel = { .report_id = HID_REPORT_ID_WHEEL };

typedef struct {
    uint8_t right : 1;
    uint8_t middle : 1;
    uint8_t left : 1;
    uint8_t : 5;  // pad
} __attribute__((packed)) button_states_t;
const button_states_t default_button_states = { 0 };

typedef struct {
    uint8_t report_id;
    uint8_t layer;
    uint8_t pressed : 1;
    uint8_t : 7;
    uint8_t button_id;
    button_states_t button_states;
    uint8_t pad00[27];  // Pad
} __attribute__((packed)) hid_report_button_t;
STATIC_ASSERT(sizeof(hid_report_button_t) == 32);
const hid_report_button_t default_hid_report_button = { .report_id = HID_REPORT_ID_BUTTON };
#    endif  // RAW_ENABLE

// ============================================================================
// ACCELERATION HELPERS
// ============================================================================

#    define ACCELERATION_CONST_P ((float) KNOB_ACCELERATION_BLEND / (float) KNOB_ACCELERATION_SCALE)
#    define ACCELERATION_CONST_Q ((float) KNOB_ACCELERATION_BLEND + 1.0)
#    define ACCELERATION_CONST_R ((float) KNOB_ACCELERATION_SCALE)

typedef struct {
    float items[KNOB_ACCELERATION_BUFFER_SIZE];
    float current_sum;
    size_t current_size;
    size_t next_index;
} ring_buffer_t;

static void ring_buffer_reset(ring_buffer_t* rb) {
    rb->current_sum  = 0;
    rb->current_size = 0;
    rb->next_index   = 0;
}

static void ring_buffer_push(ring_buffer_t* rb, float item) {
    if (rb->current_size == KNOB_ACCELERATION_BUFFER_SIZE) {
        rb->current_sum -= rb->items[rb->next_index];
    } else {
        rb->current_size++;
    }
    rb->items[rb->next_index] = item;
    rb->current_sum += item;
    rb->next_index = (rb->next_index + 1) % KNOB_ACCELERATION_BUFFER_SIZE;
}

static float ring_buffer_mean(ring_buffer_t* rb) {
    return rb->current_size > 0 ? rb->current_sum / rb->current_size : 0;
}

// ============================================================================
// KNOB STATE
// ============================================================================

#    ifdef POINTING_DEVICE_ENABLE
typedef enum {
    DRAG_STATE_DEACTIVATED = 0,
    DRAG_STATE_ACTIVATING,
    DRAG_STATE_ACTIVATED,
} drag_state_t;
#    endif  // POINTING_DEVICE_ENABLE

typedef struct {
    uint32_t last_motion_time;
    uint32_t last_action_time;
    int16_t accumulator;
    float remainder;
    ring_buffer_t acceleration_buffer;
#    ifdef POINTING_DEVICE_ENABLE
    drag_state_t drag_state;
    uint32_t drag_time; 
#    endif  // POINTING_DEVICE_ENABLE
} knob_state_t;

knob_config_t knob_config = {0};
knob_state_t knob_state = {0};
uint32_t current_time = 0;

#    ifdef RAW_ENABLE
hid_config_t hid_config = default_hid_config;
#    endif  // RAW_ENABLE

// ============================================================================
// DRAG HELPERS
// ============================================================================

#    ifdef POINTING_DEVICE_ENABLE

void start_dragging(void) {
    report_mouse_t mouse = pointing_device_get_report();
    mouse.buttons = pointing_device_handle_buttons(mouse.buttons, true, knob_config.drag_button);
    pointing_device_set_report(mouse);
    if (knob_config.drag_modifiers != 0) {
        register_mods(knob_config.drag_modifiers);
    }
}

void stop_dragging(void) {
    report_mouse_t mouse = pointing_device_get_report();
    mouse.buttons = pointing_device_handle_buttons(mouse.buttons, false, knob_config.drag_button);
    pointing_device_set_report(mouse);
    if (knob_config.drag_modifiers != 0) {
        unregister_mods(knob_config.drag_modifiers);
    }
}

#    endif  // POINTING_DEVICE_ENABLE

// ============================================================================
// MIDI HELPERS
// ============================================================================

#    ifdef MIDI_ENABLE

extern MidiDevice midi_device;

static void midi_send_relative_cc(int delta, uint8_t channel, uint8_t cc, midi_mode_t mode) {
    if (delta > 63)  delta = 63;
    if (delta < -63) delta = -63;
    uint8_t value = 0;
    if (delta == 0) {
        return;
        // if (mode == MIDI_MODE_OFFSET) value = 64;
        // else value = 0;
    } else {
        if (delta > 63)  delta = 63;
        if (delta < -63) delta = -63;
        switch (mode) {
            case MIDI_MODE_OFFSET:
                value = (uint8_t)(64 + delta);
                break;
            case MIDI_MODE_TWOS:
                if (delta > 0) value = (uint8_t)delta;
                else {
                    int8_t d = delta;
                    uint8_t v = (uint8_t)d & 0x7F;
                    value = v ? v : 0x40;
                }
                break;
            case MIDI_MODE_SIGNED: {
                uint8_t mag = (uint8_t)((delta >= 0) ? delta : -delta);
                uint8_t sign = (delta < 0) ? 0x40 : 0x00;
                value = (uint8_t)(sign | mag);
                break;
            }
        }
    }
    midi_send_cc(&midi_device, channel, cc, value);
}

#    endif  // MIDI_ENABLE

// ============================================================================
// KNOB FUNCTIONALITY
// ============================================================================

void reset_knob_state(void) {
    knob_state.last_motion_time = current_time;
    knob_state.last_action_time = current_time;
    knob_state.accumulator = 0;
    knob_state.remainder = 0;
    ring_buffer_reset(&knob_state.acceleration_buffer);
}

static void housekeeping_task_knob_modes(void) {

    // avoid repeated timer reads by doing it once and saving the value
    current_time = timer_read32();

    // skip everything if the knob is set to off
    if (knob_config.mode == KNOB_MODE_OFF) {
        return;
    }

    // reset state after a period of no activity
    // Bug: Should probably keep returning while knob is inactive,
    // but only does it once every KNOB_TIMEOUT_MS.
    if (as5600_delta == 0) {
        if (TIMER_DIFF_32(current_time, knob_state.last_motion_time) > KNOB_TIMEOUT_MS) {
            reset_knob_state();
            return;
        }
    } else {
        knob_state.accumulator += as5600_delta;
        knob_state.last_motion_time = current_time;
    }

    // throttle rate at which actions are performed
#    ifdef RAW_ENABLE
    if (TIMER_DIFF_32(current_time, knob_state.last_action_time) < hid_config.update_throttle) {
#    else
    if (TIMER_DIFF_32(current_time, knob_state.last_action_time) < KNOB_THROTTLE_MS) {
#    endif  // RAW_ENABLE
        return;
    }
    knob_state.last_action_time = current_time;

#    ifdef RAW_ENABLE
    hid_report_wheel_t hid_report_wheel = default_hid_report_wheel;
    hid_report_wheel.layer = get_highest_layer(layer_state);
    hid_report_wheel.raw_delta = knob_state.accumulator;
    hid_report_wheel.raw_clockwise = (hid_report_wheel.raw_delta > 0) ? 1 : 0;
#    endif  // RAW_ENABLE

    // zero out the accumulator when ready to perform an action
    float delta = knob_state.accumulator;
    knob_state.accumulator = 0;

    // apply acceleration
    if (knob_config.acceleration) {
        float speed = fabsf(delta);
        ring_buffer_push(&knob_state.acceleration_buffer, speed);
        if (delta != 0) {
            // v_out = p * square(min(v_in - r, 0)) + q * (v_in - r) + r
            speed = ring_buffer_mean(&knob_state.acceleration_buffer);
            float speed_offset = speed - ACCELERATION_CONST_R;
            float scale_factor = ACCELERATION_CONST_Q * speed_offset + ACCELERATION_CONST_R;
            if (speed_offset < 0) {
                scale_factor += ACCELERATION_CONST_P * speed_offset * speed_offset;
            }
            scale_factor /= speed;
            delta *= scale_factor;
        }
    }

    // apply reverse
    if (knob_config.reverse) {
        delta *= -1;
    }

    // apply sensitivity
    delta *= knob_config.sensitivity;

#    ifdef RAW_ENABLE
    hid_report_wheel.processed_delta = delta * HID_WHEEL_SCALE;
    hid_report_wheel.processed_clockwise = (hid_report_wheel.processed_delta > 0) ? 1 : 0;
    if (hid_report_wheel.raw_delta > ((int16_t)hid_config.wheel_deadzone)
     || hid_report_wheel.raw_delta < -((int16_t)hid_config.wheel_deadzone)) {
        raw_hid_send((uint8_t*)&hid_report_wheel, sizeof(hid_report_wheel_t));
    }
#    endif  // RAW_ENABLE

    // apply mode scale
    switch (knob_config.mode) {
        case KNOB_MODE_OFF:
            return;  // unreachable
#    ifdef ENCODER_ENABLE
        case KNOB_MODE_ENCODER:
            delta *= KNOB_SENS_SCALE_ENCODER;
            break;
#    endif  // ENCODER_ENABLE
#    ifdef POINTING_DEVICE_ENABLE
        case KNOB_MODE_WHEEL_VERTICAL...KNOB_MODE_WHEEL_HORIZONTAL:
            delta *= KNOB_SENS_SCALE_WHEEL;
            break;
        case KNOB_MODE_DRAG_VERTICAL...KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL:
            delta *= KNOB_SENS_SCALE_DRAG;
            break;
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
        case KNOB_MODE_MIDI:
            delta *= KNOB_SENS_SCALE_MIDI;
            break;
#    endif  // MIDI_ENABLE
    }

    // truncate to integer and save remainder
    delta += knob_state.remainder;
    int delta_truncated = delta;
    knob_state.remainder = delta - delta_truncated;
    
    // apply action
#    ifdef POINTING_DEVICE_ENABLE
    report_mouse_t mouse;
#    endif  // POINTING_DEVICE_ENABLE

    switch (knob_config.mode) {
#    ifdef ENCODER_ENABLE
        case KNOB_MODE_ENCODER:
            while (delta_truncated > 0) {
                encoder_queue_event(0, true);
                delta_truncated -= 1;
            } 
            while (delta_truncated < 0) {
                encoder_queue_event(0, false);
                delta_truncated += 1;
            }
            break;
#    endif  // ENCODER_ENABLE
#    ifdef POINTING_DEVICE_ENABLE
        case KNOB_MODE_WHEEL_VERTICAL...KNOB_MODE_DRAG_DIAGONAL: 
            if (delta_truncated == 0) {
                break;
            }
            mouse = pointing_device_get_report();
            switch (knob_config.mode) {
                case KNOB_MODE_WHEEL_VERTICAL:
                    mouse.v += delta_truncated * -1;
                    break;
                case KNOB_MODE_WHEEL_HORIZONTAL:
                    mouse.h += delta_truncated;
                    break;
                case KNOB_MODE_DRAG_VERTICAL:
                    mouse.y += delta_truncated * -1;
                    break;
                case KNOB_MODE_DRAG_HORIZONTAL:
                    mouse.x += delta_truncated;
                    break;
                case KNOB_MODE_DRAG_DIAGONAL:
                    mouse.y += delta_truncated * -1;
                    mouse.x += delta_truncated;
                    break;
                default:
                    return;  // unreachable
            }
            pointing_device_set_report(mouse);
            break;
        case KNOB_MODE_ADAPTIVE_DRAG_VERTICAL...KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL:
            switch (knob_state.drag_state) {
                case DRAG_STATE_DEACTIVATED:
                    if (delta_truncated != 0) {
                        knob_state.drag_state = DRAG_STATE_ACTIVATING;
                        knob_state.drag_time = current_time;
                        start_dragging();
                    }
                    break;
                case DRAG_STATE_ACTIVATING:
                    if (TIMER_DIFF_32(current_time, knob_state.drag_time) >= KNOB_ADAPTIVE_DRAG_ON_DELAY) {
                        knob_state.drag_state = DRAG_STATE_ACTIVATED;
                    }
                    break;
                case DRAG_STATE_ACTIVATED:
                    if (delta_truncated != 0) {
                        knob_state.drag_time = current_time;
                        mouse = pointing_device_get_report();
                        switch (knob_config.mode) {
                            case KNOB_MODE_ADAPTIVE_DRAG_VERTICAL:
                                mouse.y += delta_truncated * -1;
                                break;
                            case KNOB_MODE_ADAPTIVE_DRAG_HORIZONTAL:
                                mouse.x += delta_truncated;
                                break;
                            case KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL:
                                mouse.y += delta_truncated * -1;
                                mouse.x += delta_truncated;
                                break;
                            default:
                                return;  // unreachable
                        }
                        pointing_device_set_report(mouse);
                    } else {
                        if (TIMER_DIFF_32(current_time, knob_state.drag_time) >= KNOB_ADAPTIVE_DRAG_OFF_DELAY) {
                            knob_state.drag_state = DRAG_STATE_DEACTIVATED;
                            stop_dragging();
                        }
                    }
                    break;
            }
            break;
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
        case KNOB_MODE_MIDI:
            midi_send_relative_cc(delta_truncated, knob_config.midi_channel, knob_config.midi_cc, knob_config.midi_mode);
            break;
#    endif  // MIDI_ENABLE
        default:
            return;  // unreachable
    }
    return;
}

// ============================================================================
// PUBLIC KNOB API
// ============================================================================

void set_knob_mode(knob_mode_t mode) {
#    ifdef POINTING_DEVICE_ENABLE
    if ((KNOB_MODE_DRAG_VERTICAL <= knob_config.mode) && (knob_config.mode <= KNOB_MODE_DRAG_DIAGONAL)) {
        stop_dragging();
    }
#    endif  // POINTING_DEVICE_ENABLE
    reset_knob_state();
    knob_config.mode = mode;
#    ifdef POINTING_DEVICE_ENABLE
    if ((KNOB_MODE_DRAG_VERTICAL <= knob_config.mode) && (knob_config.mode <= KNOB_MODE_DRAG_DIAGONAL)) {
        start_dragging();
    }
    if ((KNOB_MODE_ADAPTIVE_DRAG_VERTICAL <= knob_config.mode) && (knob_config.mode <= KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL)) {
        knob_state.drag_state = DRAG_STATE_DEACTIVATED;
    }
#    endif  // POINTING_DEVICE_ENABLE
}

knob_config_t get_knob_config(void) {
    return knob_config;
}

void set_knob_config(knob_config_t config) {
    set_knob_mode(config.mode);
    knob_config = config;
}

void reset_knob_config(void) {
    set_knob_config(default_knob_config);
}

#    ifdef RAW_ENABLE
#    ifdef VIA_ENABLE
bool via_command_kb(uint8_t *data, uint8_t length) {
#    else
void raw_hid_receive(uint8_t *data, uint8_t length) {
#    endif  // VIA_ENABLE
    assert(length == 32);
    if (data[0] == HID_REPORT_ID_SET_CONFIG) {
        hid_report_set_config_t *report = (hid_report_set_config_t*)data;
        if (report->set_wheel_deadzone > 0) {
            hid_config.wheel_deadzone = report->wheel_deadzone;
        }
        if (report->set_update_throttle > 0) {
            hid_config.update_throttle = report->update_throttle < MINIMUM_UPDATE_THROTTLE ? MINIMUM_UPDATE_THROTTLE : report->update_throttle;
        }
    }
    else if (data[0] == HID_REPORT_ID_SET_LAYER) {
        hid_report_set_layer_t *report = (hid_report_set_layer_t*)data;
        if (report->layer < 32) {
            layer_move(report->layer);
        }
    }
    else if (data[0] == HID_REPORT_ID_SET_RGB) {
        hid_report_set_rgb_t *report = (hid_report_set_rgb_t*)data;
        if (report->set_left > 0) {
            rgblight_setrgb_at(report->left.r, report->left.g, report->left.b, 0);
        }
        if (report->set_middle > 0) {
            rgblight_setrgb_at(report->middle.r, report->middle.g, report->middle.b, 1);
        }
        if (report->set_right > 0) {
            rgblight_setrgb_at(report->right.r, report->right.g, report->right.b, 2);
        }
    }
#    ifdef VIA_ENABLE
    else {
        return false;  // Message not handled by HID api
    }
    return true;
#    endif  // VIA_ENABLE
}
#    endif  // RAW_ENABLE

#endif // !KNOB_MINIMAL

// ============================================================================
// QMK HOOKS
// ============================================================================

void keyboard_pre_init_kb(void){
    i2c_init();
#ifndef KNOB_MINIMAL
    current_time = timer_read32();
    reset_knob_config();
#endif // !KNOB_MINIMAL
    keyboard_pre_init_user();
}

void housekeeping_task_kb(void) {
    housekeeping_task_read_as5600();
#ifndef KNOB_MINIMAL
    housekeeping_task_knob_modes();
#endif // !KNOB_MINIMAL
    housekeeping_task_user();
}

#    ifdef RAW_ENABLE
button_states_t button_states = default_button_states;
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    hid_report_button_t report = default_hid_report_button;
    report.layer = get_highest_layer(layer_state);
    report.pressed = (record->event.pressed) ? 1 : 0;
    report.button_id = record->event.key.col;
    switch (report.button_id)
    {
        case 0:
            button_states.left = report.pressed;
            break;
        case 1:
            button_states.middle = report.pressed;
            break;
        case 2:
            button_states.right = report.pressed;
            break;
    }
    report.button_states = button_states;
    raw_hid_send((uint8_t*)&report, sizeof(hid_report_button_t));

    return true;  // Tell QMK to keep processing
}
#    endif  // RAW_ENABLE
