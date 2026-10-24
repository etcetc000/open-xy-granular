// Granular sample playback: a cloud of short, windowed, pitched grains read
// from a chosen position of the pad's trimmed sample. Fixed-point only; the
// audio callback neither allocates nor divides per sample.
#pragma once
#include <opxy/sample_engine.hpp>
#include "tables.h"

namespace granular {
using opxy::i16;
using opxy::i32;
using opxy::u16;
using opxy::u32;

constexpr unsigned kGrains = 9;           // density up to 8, plus one for integer scheduling/window rounding
constexpr unsigned kBlock = 64;
constexpr unsigned kFraction = 12;        // position: sample frames in Q20.12
constexpr u32 kUnit = 1u << kFraction;
// 44100 / 261.6256 Hz in Q16: a note's phase increment times this, over 2^32,
// is its frequency relative to C4, the pitch stock plays a sample unchanged at.
constexpr u32 kInverseC4Q16 = 11046847u;
constexpr u32 kMinInterval = 32;          // output samples between grain starts
constexpr u32 kMaxStepQ12 = 1u << 20;     // 256 frames per output sample
constexpr u32 kMaxSprayFrames = 88200;    // 2 s at 44.1 kHz

struct Grain {
    u32 position;       // absolute sample frame, Q20.12
    i32 step;           // frames per output sample, Q12; negative plays backwards
    u32 window_phase;   // 0..2^32 spans one sin^2 window
    u32 window_step;    // 0 marks a free grain
    u16 gain_left;      // Q15 pan gains for stereo output
    u16 gain_right;
};

struct Voice {
    opxy::SampleView sample;
    Grain grains[kGrains];
    u32 next;            // output samples until the next grain starts
    u32 rng;
    u32 scan_position;   // offset added to the position control, Q20.12 within the region
};

// Grain length in output samples and density (grains per grain length, Q8)
// for the evenly spaced anchors in definition.json.
static const u32 kSizeFrames[7] = {441, 1102, 2205, 4410, 8820, 15435, 22050};
static const u32 kDensityQ8[5] = {128, 256, 512, 1024, 2048};

// (a * b) >> shift for the full 64-bit product, in 16-bit limbs: the module
// links no 64-bit runtime. shift is 0..63.
inline u32 mul_shift(u32 a, u32 b, unsigned shift) {
    const u32 al = a & 0xffffu, ah = a >> 16, bl = b & 0xffffu, bh = b >> 16;
    const u32 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const u32 mid = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    const u32 low = (mid << 16) | (ll & 0xffffu);
    const u32 high = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
    if (shift == 0) return low;
    if (shift >= 32) return high >> (shift - 32);
    return (low >> shift) | (high << (32 - shift));
}
// (a + b) mod m for a, b < m, without overflowing when m exceeds 2^31.
inline u32 add_mod(u32 a, u32 b, u32 m) { return a >= m - b ? a - (m - b) : a + b; }
inline u32 unsigned_q31(i32 value) { return value < 0 ? 0u : static_cast<u32>(value); }
inline u32 random(Voice& voice) { return voice.rng = voice.rng * 1664525u + 1013904223u; }

// Piecewise-linear lookup over evenly spaced anchors, matching an "anchors"
// mapping. Anchor values stay below 2^15 so the interpolation fits 32 bits.
inline u32 anchor(u32 q31, const u32* values, unsigned count) {
    const u32 scaled = mul_shift(q31, (count - 1) << 16, 31);
    const u32 index = scaled >> 16, fraction = scaled & 0xffffu;
    if (index >= count - 1) return values[count - 1];
    return values[index] + ((values[index + 1] - values[index]) * fraction >> 16);
}

// 2^(cents / 1200) in Q14, exact at semitones, linear between them, within
// +/-60 semitones.
inline u32 ratio_q14(i32 cents) {
    if (cents < -6000) cents = -6000;
    if (cents > 6000) cents = 6000;
    unsigned up = 0, down = 0;
    while (cents > 1200) { cents -= 1200; ++up; }
    while (cents < -1200) { cents += 1200; ++down; }
    const u32 shifted = static_cast<u32>(cents + 1200);
    const u32 note = shifted / 100, fraction = shifted % 100;
    const u32 ratio = note >= 24 ? granular_semitone_q14[24] :
        granular_semitone_q14[note] +
        ((granular_semitone_q14[note + 1] - granular_semitone_q14[note]) * fraction + 50) / 100;
    return (ratio << up) >> down;
}

// Integer square root, for the per-block overlap compensation.
inline u32 isqrt(u32 value) {
    u32 result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}

inline i32 saturate16(i32 value) { return value > 32767 ? 32767 : value < -32767 ? -32767 : value; }

void initialize(Voice&, const opxy::SampleView&);
void start(Voice&);
void render_sample(Voice&, const opxy::SampleBlock&);
}
