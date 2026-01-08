// Copyright 2025 Morgan Newell Sun (@eynsai)
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef KNOB_MINIMAL

#    ifndef KNOB_TIMEOUT_MS
#        define KNOB_TIMEOUT_MS 1000
#    endif

#    ifndef KNOB_ACCELERATION_BUFFER_SIZE
#        define KNOB_ACCELERATION_BUFFER_SIZE 10
#    endif

#    ifndef KNOB_THROTTLE_MS
#        define KNOB_THROTTLE_MS 16
#    endif

#    ifndef KNOB_ACCELERATION_SCALE
#        define KNOB_ACCELERATION_SCALE 100.0
#    endif

#    ifndef KNOB_ACCELERATION_BLEND
#        define KNOB_ACCELERATION_BLEND 0.872116
#    endif

#    ifndef KNOB_ADAPTIVE_DRAG_ON_DELAY
#        define KNOB_ADAPTIVE_DRAG_ON_DELAY 100
#    endif

#    ifndef KNOB_ADAPTIVE_DRAG_OFF_DELAY
#        define KNOB_ADAPTIVE_DRAG_OFF_DELAY 100
#    endif

#    ifndef KNOB_SENS_SCALE_ENCODER
#        define KNOB_SENS_SCALE_ENCODER (1.0 / 4096.0)
#    endif

#    ifndef KNOB_SENS_SCALE_WHEEL
#        define KNOB_SENS_SCALE_WHEEL (120.0 / 4096.0)
#    endif

#    ifndef KNOB_SENS_SCALE_DRAG
#        define KNOB_SENS_SCALE_DRAG (30.0 / 4096.0)
#    endif

#    ifndef KNOB_SENS_SCALE_MIDI
#        define KNOB_SENS_SCALE_MIDI (1.0 / 4096.0)
#    endif

#endif