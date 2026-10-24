// The grain display: a strip stands for the trimmed sample. The marker is the
// position control plus the scan drift, the brackets show the spray range, and
// grains animate while the SDK reports gated notes. The waveform is a
// copied background-worker overview; the GUI never reads PCM or live voices.
#include "display.h"
#include <opxy/sample_display.hpp>

namespace granular {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

constexpr i32 kStripLeft = 40, kStripWidth = 400, kStripMiddle = 120;  // pixels
constexpr i32 kWindowX = 240, kWindowY = 206;
constexpr i32 kMaxFrame = 102;        // Q10 seconds: late frames are clamped to 0.1 s
constexpr i32 kMinLife = 123;         // Q10 seconds: the shortest grain still visible
constexpr i32 kExpired = 1 << 20;
constexpr u32 kMarker = 4, kBracketLeft = 5, kBracketRight = 6, kFirstGrain = 7;

inline u32 random(DisplayState& state) { return state.rng = state.rng * 1664525u + 1013904223u; }
inline u32 fraction15(i32 q31) { return q31 < 0 ? 0u : static_cast<u32>(q31) >> 16; }  // 0..32767
// Evenly spaced anchors, as the engine maps them; values in Q10 of their unit.
inline i32 anchor(u32 f15, const i32* values, u32 count) {
    const u32 scaled = f15 * (count - 1), index = scaled >> 15, part = scaled & 32767u;
    if (index >= count - 1) return values[count - 1];
    return values[index] + ((values[index + 1] - values[index]) * static_cast<i32>(part) >> 15);
}
const i32 kSizeMs[7] = {10, 25, 50, 100, 200, 350, 500};
const i32 kDensityQ8[5] = {128, 256, 512, 1024, 1536};

inline void clear(DisplayState& state) {
    for (u32 i = 0; i < kDisplayGrains; ++i) state.grains[i].age = kExpired;
    state.spawn_due = 0;
}

}

void display_reset(DisplayState& state) {
    state.initialized = state.track = state.previous_event = 0;
    state.rng = 0x6d2b79f5u;
    state.scan_offset = 0;
    state.wave_generation = state.wave_begin = state.wave_end = 0;
    clear(state);
}

void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const i32 elapsed = clamp(input.elapsed, 0, kMaxFrame);
    const u32 position = fraction15(input.slots[0]), size = fraction15(input.slots[1]);
    const u32 density = fraction15(input.slots[2]), spray = fraction15(input.slots[3]);
    const u32 spread = fraction15(input.slots[6]);
    const i32 scan = static_cast<i32>((static_cast<u32>(input.slots[4]) >> 15)) - 32768;  // -32768..32767
    const bool snapshot = !state.initialized || state.track != input.track;
    opxy::SampleDisplay sample{};
    const bool loaded = opxy::sample_display(input.track, sample);
    if (snapshot || input.reduced_motion) {
        clear(state);
        state.scan_offset = 0;
    } else {
        if (input.track < 8 && input.note_serial != state.previous_event) {
            random(state);
        }
        // Scan: up to a full strip in five seconds either way; the middle tenth freezes.
        if (scan > 3277 || scan < -3277) {
            const i32 speed = (scan > 0 ? scan - 3276 : scan + 3276) * 80 / 29492;  // px/s, -80..80
            state.scan_offset += speed * elapsed;
            const i32 span = kStripWidth * unit;
            while (state.scan_offset >= span) state.scan_offset -= span;
            while (state.scan_offset < 0) state.scan_offset += span;
        }
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // Three ordinary retained paths cover the strip. Each closed outline
    // contains 32 upper and 32 lower extrema, scaled within the drawing bounds.
    const bool waveform = loaded && sample.frames && sample.end > sample.begin;
    for (u32 segment = 0; segment < 3; ++segment) {
        const u32 path = 1 + segment;
        // Hidden paths still need a valid retained-geometry key, including
        // the first frame while the background waveform is pending.
        if (!frame.paths[path].key) frame.paths[path].key = 1;
        if (!waveform) { hide(frame, path); continue; }
        if (snapshot || sample.generation != state.wave_generation ||
            sample.begin != state.wave_begin || sample.end != state.wave_end) {
            for (u32 point = 0; point < 32; ++point) {
                const u32 column = segment * 32 + point;
                const u32 source = sample.begin + (sample.end - sample.begin - 1) * column / 95;
                u32 bin = source * opxy::sample_display_bins / sample.frames;
                if (bin >= opxy::sample_display_bins) bin = opxy::sample_display_bins - 1;
                const i32 x = -kStripWidth * unit / 12 + kStripWidth * unit * static_cast<i32>(point) / (6 * 31);
                frame.paths[path].points[point] = {x, -(static_cast<i32>(sample.high[bin]) * 25 * unit) / 32768};
                frame.paths[path].points[63 - point] = {x, -(static_cast<i32>(sample.low[bin]) * 25 * unit) / 32768};
            }
            if (!++frame.paths[path].key) ++frame.paths[path].key;
        }
        const i32 center = kStripLeft * unit + kStripWidth * unit * static_cast<i32>(2 * segment + 1) / 6;
        place(frame, path, center, kStripMiddle * unit, 2 * unit, 5, stroke);
    }
    if (waveform) {
        state.wave_generation = sample.generation;
        state.wave_begin = sample.begin; state.wave_end = sample.end;
    }

    // Marker and brackets along the strip.
    i32 marker = kStripLeft * unit + static_cast<i32>((static_cast<u32>(kStripWidth) * position) >> 15) * unit + state.scan_offset;
    const i32 right = (kStripLeft + kStripWidth) * unit;
    if (marker > right) marker -= kStripWidth * unit;
    place(frame, kMarker, marker, kStripMiddle * unit, unit, 15, stroke);
    const i32 reach = static_cast<i32>((static_cast<u32>(kStripWidth / 2) * spray) >> 15) * unit;
    if (reach >= 2 * unit) {
        place(frame, kBracketLeft, clamp(marker - reach, kStripLeft * unit, right), kStripMiddle * unit, unit, 6, stroke);
        place(frame, kBracketRight, clamp(marker + reach, kStripLeft * unit, right), kStripMiddle * unit, unit, 6, stroke);
    } else {
        hide(frame, kBracketLeft);
        hide(frame, kBracketRight);
    }

    // The grain window outline, as wide as the grain is long.
    const i32 size_ms = anchor(size, kSizeMs, 7);
    const u32 key = 1 + static_cast<u32>(size_ms);
    if (frame.paths[0].key != key) {
        const i32 half_width = (20 + size_ms * 40 / 500) * unit;  // 20..60 px, inside the path extent
        // sin^2(pi i / 63), Q10.
        static const i32 window[64] = {
            0, 3, 10, 23, 40, 62, 89, 120, 155, 193, 234, 278, 325, 373, 423, 474,
            525, 576, 626, 675, 723, 768, 811, 851, 887, 920, 949, 973, 993, 1008, 1018, 1023,
            1023, 1018, 1008, 993, 973, 949, 920, 887, 851, 811, 768, 723, 675, 626, 576, 525,
            474, 423, 373, 325, 278, 234, 193, 155, 120, 89, 62, 40, 23, 10, 3, 0};
        for (u32 i = 0; i < Frame::point_count; ++i) {
            frame.paths[0].points[i].x = -half_width + (2 * half_width / 63) * static_cast<i32>(i);
            frame.paths[0].points[i].y = -(window[i] * 24);  // Q10: a 24-pixel peak
        }
        frame.paths[0].key = key;
    }
    place(frame, 0, kWindowX * unit, kWindowY * unit, unit, 9, stroke);


    // Grains: spawn while active, at the engine's rate, and age out.
    const i32 life = size_ms * unit / 1000 > kMinLife ? size_ms * unit / 1000 : kMinLife;
    const i32 density_q8 = anchor(density, kDensityQ8, 5);
    i32 interval = (size_ms * unit / 1000) * 256 / density_q8;  // Q10 seconds
    if (interval < 20) interval = 20;
    if (!input.reduced_motion && sample.active_notes) {
        state.spawn_due -= elapsed;
        for (u32 n = 0; n < 4 && state.spawn_due <= 0; ++n) {
            state.spawn_due += interval;
            for (u32 i = 0; i < kDisplayGrains; ++i) {
                DisplayGrain& g = state.grains[i];
                if (g.age < g.life) continue;
                const i32 jitter = reach ? static_cast<i32>(random(state) % static_cast<u32>(2 * reach + 1)) - reach : 0;
                g.x = clamp(marker + jitter, kStripLeft * unit, right);
                const i32 lift = static_cast<i32>((static_cast<u32>(40) * spread) >> 15) * unit;
                g.y = kStripMiddle * unit + (lift ? static_cast<i32>(random(state) % static_cast<u32>(2 * lift + 1)) - lift : 0);
                g.age = 0;
                g.life = life;
                break;
            }
        }
    } else {
        state.spawn_due = 0;
    }
    for (u32 i = 0; i < kDisplayGrains; ++i) {
        DisplayGrain& g = state.grains[i];
        if (g.age < g.life && !input.reduced_motion) g.age += elapsed;
        if (g.age >= g.life || g.life <= 0) { hide(frame, kFirstGrain + i); continue; }
        const i32 progress = g.age * unit / g.life;                       // 0..1 Q10
        const i32 bell = progress < 512 ? progress * 2 : (unit - progress) * 2;  // rise then fall
        const i32 scale = unit / 4 + (bell * 5 >> 2);                     // 0.25..1.5
        const u32 shade = static_cast<u32>(clamp(4 + (bell * 11 >> 10), 0, 15));
        place(frame, kFirstGrain + i, g.x, g.y, clamp(scale, unit / 4, 2 * unit), shade, fill_and_stroke);
    }
}
}
