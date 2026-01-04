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

#    ifdef RAW_ENABLE
#    define HID_REPORT_ID_SET_CONFIG 0xb1
#    define HID_REPORT_ID_WHEEL 0xa1
#    define HID_REPORT_ID_BUTTON 0xa2
#    define MINIMUM_UPDATE_THROTTLE 10

typedef struct {
    uint16_t wheel_deadzone;
    uint16_t update_throttle;
} hid_config_t;
const hid_config_t default_hid_config = { .wheel_deadzone = 5, .update_throttle = 2 * KNOB_THROTTLE_MS };
hid_config_t hid_config = default_hid_config;

typedef struct {
    uint8_t report_id;
    uint16_t set_wheel_deadzone : 1;
    uint16_t set_update_throttle : 1;
    uint16_t : 14;  // Reserved
    uint8_t reserved00;
    uint16_t wheel_deadzone;
    uint16_t update_throttle;
    uint8_t reserved01[24];
} __attribute__((packed)) hid_report_set_config_t;
STATIC_ASSERT(sizeof(hid_report_set_config_t) == 32);

typedef struct {
    uint8_t report_id;
    int16_t raw_delta;  // Without acceleration/sensitivity
    uint8_t clockwise : 1;
    uint8_t : 7;  // Padding
    int32_t modified_delta;  // With acceleration/sensitivity
    uint8_t pad00[24];  // Padding
} __attribute__((packed)) hid_report_wheel_t;
STATIC_ASSERT(sizeof(hid_report_wheel_t) == 32);
const hid_report_wheel_t default_hid_report_wheel = { .report_id = HID_REPORT_ID_WHEEL };

typedef struct {
    uint8_t left : 1;
    uint8_t middle : 1;
    uint8_t right : 1;
    uint8_t : 5;  // pad
} __attribute__((packed)) button_state_t;
const button_state_t default_button_state = { 0 };

typedef struct {
    uint8_t report_id;
    button_state_t state;
    button_state_t pressed;
    button_state_t released;
    uint8_t pad00[28];  // Pad
} __attribute__((packed)) hid_report_button_t;
STATIC_ASSERT(sizeof(hid_report_button_t) == 32);
const hid_report_button_t default_hid_report_button = { .report_id = HID_REPORT_ID_BUTTON };
#    endif  // RAW_ENABLE

knob_config_t knob_config = {0};
knob_state_t knob_state = {0};
uint32_t current_time = 0;

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
    hid_report_wheel.raw_delta = knob_state.accumulator;
    hid_report_wheel.clockwise = (knob_state.accumulator > 0) ? 1 : 0;
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

    // apply sensitivity
    switch (knob_config.mode) {
        case KNOB_MODE_OFF:
            return;  // unreachable
#    ifdef ENCODER_ENABLE
        case KNOB_MODE_ENCODER:
            delta *= knob_config.sensitivity * KNOB_SENS_SCALE_ENCODER;
            break;
#    endif  // ENCODER_ENABLE
#    ifdef POINTING_DEVICE_ENABLE
        case KNOB_MODE_WHEEL_VERTICAL...KNOB_MODE_WHEEL_HORIZONTAL:
            delta *= knob_config.sensitivity * KNOB_SENS_SCALE_WHEEL;
            break;
        case KNOB_MODE_DRAG_VERTICAL...KNOB_MODE_ADAPTIVE_DRAG_DIAGONAL:
            delta *= knob_config.sensitivity * KNOB_SENS_SCALE_DRAG;
            break;
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef MIDI_ENABLE
        case KNOB_MODE_MIDI:
            delta *= knob_config.sensitivity * KNOB_SENS_SCALE_MIDI;
            break;
#    endif  // MIDI_ENABLE
    }

    // apply reverse
    if (knob_config.reverse) {
        delta *= -1;
    }

    // truncate to integer and save remainder
    delta += knob_state.remainder;
    int delta_truncated = delta;
    knob_state.remainder = delta - delta_truncated;
    
    // apply action
#    ifdef POINTING_DEVICE_ENABLE
    report_mouse_t mouse;
#    endif  // POINTING_DEVICE_ENABLE
#    ifdef RAW_ENABLE
    hid_report_wheel.modified_delta = (int32_t)delta_truncated;
    if (hid_report_wheel.raw_delta > ((int16_t)hid_config.wheel_deadzone)
     || hid_report_wheel.raw_delta < -((int16_t)hid_config.wheel_deadzone)
     || hid_report_wheel.modified_delta > ((int32_t)hid_config.wheel_deadzone)
     || hid_report_wheel.modified_delta < -((int32_t)hid_config.wheel_deadzone)) {
        raw_hid_send((uint8_t*)&hid_report_wheel, sizeof(hid_report_wheel_t));
    }
#    endif  // RAW_ENABLE
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
        hid_report_set_config_t *config = (hid_report_set_config_t*)data;
        if (config->set_wheel_deadzone > 0) {
            hid_config.wheel_deadzone = config->wheel_deadzone;
        }
        if (config->set_update_throttle > 0) {
            hid_config.update_throttle = config->update_throttle < MINIMUM_UPDATE_THROTTLE ? MINIMUM_UPDATE_THROTTLE : config->update_throttle;
        }
    }
#    ifdef VIA_ENABLE
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
button_state_t button_state = default_button_state;
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    hid_report_button_t report = default_hid_report_button;
    uint8_t pressed = (record->event.pressed) ? 1 : 0;
    uint8_t released = 1 - pressed;
    switch (record->event.key.col)
    {
        case 0:
            button_state.left = report.pressed.left = pressed;
            report.released.left = released;
            break;
        case 1:
            button_state.middle = report.pressed.middle = pressed;
            report.released.middle = released;
            break;
        case 2:
            button_state.right = report.pressed.right = pressed;
            report.released.right = released;
            break;
    }
    report.state = button_state;
    raw_hid_send((uint8_t*)&report, sizeof(hid_report_button_t));

    return true;  // Tell QMK to keep processing
}
#    endif  // RAW_ENABLE
