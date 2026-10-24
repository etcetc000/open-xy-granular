#pragma once
#include <opxy/draw.hpp>

namespace granular {
// Four mutable outlines (grain window and waveform), a position marker, two spray
// brackets and 22 grain dots; 16 grey palette entries.
constexpr unsigned kDisplayGrains = 22;
using Frame = opxy::draw::Frame<4, 64, 7 + kDisplayGrains, 0, 16>;
struct DisplayGrain { opxy::i32 x, y, age, life; };  // Q10 pixels and Q10 seconds
struct DisplayState {
    opxy::u32 initialized, track, previous_event, rng;
    opxy::u32 wave_generation, wave_begin, wave_end;
    opxy::i32 spawn_due;       // Q10 seconds until the next grain
    opxy::i32 scan_offset;     // Q10 pixels along the strip
    DisplayGrain grains[kDisplayGrains];
};
}
