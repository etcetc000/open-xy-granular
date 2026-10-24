#include "granular.h"
#include <opxy/q31.hpp>

namespace granular {
namespace {
struct Settings {
    u32 begin, length;        // trimmed region, frames
    u32 length_q12, limit_q12;  // region in Q20.12 and the last readable position
    u32 size;                 // grain length, output samples
    u32 interval;             // output samples between grain starts
    u32 spray_q12;            // half the random position range
    u32 span_q12;             // frames a grain covers at the current step
    u32 step;                 // frames per output sample, Q12
    u32 gain_q15;             // overlap compensation times level, up to 2.0
    u32 spread_q15;
    bool reverse;
};

void silence(i32* output, u32 channels) {
    for (unsigned i = 0; i < kBlock * channels; ++i) output[i] = 0;
}

void spawn(Voice& voice, const Settings& s, const u32 position_q31) {
    Grain* grain = nullptr;
    for (unsigned i = 0; i < kGrains; ++i)
        if (!voice.grains[i].window_step) { grain = &voice.grains[i]; break; }
    if (!grain) return;
    // Control position, scan offset and spray, all modulo the region.
    u32 relative = mul_shift(s.length, position_q31, 31 - kFraction);
    relative = add_mod(relative, voice.scan_position, s.length_q12);
    if (s.spray_q12) {
        relative = add_mod(relative, mul_shift(random(voice), s.spray_q12 << 1, 32), s.length_q12);
        relative = add_mod(relative, s.length_q12 - s.spray_q12, s.length_q12);
    }
    // Keep the whole grain inside the readable region, so it ends on its window.
    if (relative > s.limit_q12) relative = s.limit_q12;
    if (!s.reverse) {
        if (s.span_q12 >= s.limit_q12) relative = 0;
        else if (relative > s.limit_q12 - s.span_q12) relative = s.limit_q12 - s.span_q12;
    } else if (relative < s.span_q12) {
        relative = s.span_q12 <= s.limit_q12 ? s.span_q12 : s.limit_q12;
    }
    grain->position = (s.begin << kFraction) + relative;
    grain->step = s.reverse ? -static_cast<i32>(s.step) : static_cast<i32>(s.step);
    grain->window_phase = 0;
    grain->window_step = 0xffffffffu / s.size;
    i32 pan = 0;
    if (s.spread_q15) {
        const i32 centered = static_cast<i32>(random(voice) >> 17) - 16384;  // -16384..16383
        pan = (centered * static_cast<i32>(s.spread_q15)) >> 14;             // -32767..32767
    }
    grain->gain_left = static_cast<u16>(pan > 0 ? 32767 - pan : 32767);
    grain->gain_right = static_cast<u16>(pan < 0 ? 32767 + pan : 32767);
}

Settings settings(const Voice& voice, const opxy::SampleBlock& block) {
    Settings s{};
    const opxy::SampleView& sample = voice.sample;
    u32 end = block.region.end < sample.frames ? block.region.end : sample.frames;
    s.begin = block.region.begin < end ? block.region.begin : end;
    s.length = end - s.begin;
    s.length_q12 = s.length << kFraction;
    s.limit_q12 = s.length_q12 - 2 * kUnit;  // frame + 1 stays inside the region
    s.reverse = block.region.reverse;
    s.size = anchor(unsigned_q31(block.parameters[1]), kSizeFrames, 7);
    const u32 density_q8 = anchor(unsigned_q31(block.parameters[2]), kDensityQ8, 5);
    // The Q32 window plays size + 1 samples. Round spacing upward so six
    // grains sustain maximum density without an extra rounding slot.
    s.interval = (((s.size + 1) << 8) + density_q8 - 1) / density_q8;
    if (s.interval < kMinInterval) s.interval = kMinInterval;
    u32 spray = mul_shift(unsigned_q31(block.parameters[3]), 2u * sample.rate, 31);
    if (spray > s.length / 2) spray = s.length / 2;
    s.spray_q12 = spray << kFraction;
    s.spread_q15 = unsigned_q31(block.parameters[6]) >> 16;
    // Playback rate: the note's frequency over C4, the pad's tuning and the
    // tune control, then the source rate over the 44.1 kHz output rate.
    u32 rate_q16 = mul_shift(block.pitch[0], kInverseC4Q16, 32);
    const i32 tune_cents = static_cast<i32>(mul_shift(unsigned_q31(block.parameters[5]), 2400, 31)) - 1200;
    rate_q16 = mul_shift(rate_q16, ratio_q14(block.region.pitch_cents + tune_cents), 14);
    u32 step = rate_q16 >> 4;
    if (sample.rate != 44100 && sample.rate >= 8000 && sample.rate <= 192000)
        step = mul_shift(rate_q16, (sample.rate << kFraction) / 44100u, 16);
    s.step = step > kMaxStepQ12 ? kMaxStepQ12 : step;
    const u32 span_frames = mul_shift(s.size, s.step, kFraction);
    s.span_q12 = (span_frames < s.length ? span_frames : s.length) << kFraction;
    // Pad level (Q27, unity 2^27) times the volume control, as Q15 up to 2.0,
    // then divided by the square root of the overlap above unity: uncorrelated
    // grains add in power, so dense clouds keep their loudness and headroom.
    u32 level_q15 = block.region.level >> 12;
    if (level_q15 > 65535) level_q15 = 65535;
    level_q15 = mul_shift(level_q15, unsigned_q31(block.parameters[7]), 31);
    const u32 compensation_q15 = density_q8 <= 256 ? 32767 : isqrt((1073676289u / density_q8) << 8);
    s.gain_q15 = (compensation_q15 * level_q15) >> 15;
    return s;
}

// Scan moves the position control through the region at up to twice real
// time in either direction; the middle tenth of the control freezes it.
void scan(Voice& voice, const Settings& s, i32 parameter) {
    if (voice.scan_position >= s.length_q12) voice.scan_position %= s.length_q12;
    const i32 centered = static_cast<i32>(unsigned_q31(parameter) >> 15) - 32768;  // -32768..32767
    if (centered > -3277 && centered < 3277) return;
    const u32 magnitude = static_cast<u32>(centered < 0 ? -centered : centered) - 3276;  // 1..29492
    // Frames per block, Q12: magnitude / 29492 * 2 * 64 frames, through the
    // Q16 constant 128 * 4096 * 65536 / 29492.
    const u32 output_delta = mul_shift(magnitude, 1165084u, 16);
    const u32 delta = mul_shift(output_delta, (voice.sample.rate << 12) / 44100u, 12) % s.length_q12;
    voice.scan_position = centered > 0 ? add_mod(voice.scan_position, delta, s.length_q12) :
                                         add_mod(voice.scan_position, s.length_q12 - delta, s.length_q12);
}

// Render one grain up to the next spawn boundary. Channel layout is fixed
// outside this loop, and grain state stays local until the run completes.
template <unsigned SourceChannels, unsigned OutputChannels>
void grain_run(Grain& grain, const Settings& s, const i16* data, i32* output, unsigned count) {
    if (!grain.window_step) return;
    u32 position = grain.position, phase = grain.window_phase;
    const u32 window_step = grain.window_step, step = static_cast<u32>(grain.step);
    const u32 begin = s.begin << kFraction;
    // Q31 pan coefficients let the SDK use its exact fractional multiply.
    // These retain the original Q15 truncation without platform code here.
    const i32 gain_left = static_cast<i32>(grain.gain_left) * 65536;
    const i32 gain_right = static_cast<i32>(grain.gain_right) * 65536;
    if (position - begin > s.limit_q12) { grain.window_step = 0; return; }
    // Prove the entire run readable before entering its hot loop. Only the
    // last run of a grain needs division; ordinary runs use two products.
    // count <= 64, window_step <= UINT32_MAX / 441, |step| <= 2^20,
    // so both products fit in 32 bits. The final readable sample still plays.
    bool finished = false;
    const u32 phase_room = 0xffffffffu - phase;
    if (phase_room < window_step * count) {
        count = phase_room / window_step + 1;
        finished = true;
    }
    const u32 magnitude = grain.step < 0 ? 0u - step : step;
    const u32 distance = grain.step < 0 ? position - begin : s.limit_q12 - (position - begin);
    if (distance < magnitude * count) {
        count = distance / magnitude + 1;
        finished = true;
    }
    for (unsigned i = 0; i < count; ++i) {
        const u32 frame = position >> kFraction;
        const i32 fraction = static_cast<i32>(position & (kUnit - 1));
        const i16* at = data + frame * SourceChannels;
        const i32 weight = opxy::q31::multiply(granular_window[phase >> 22], static_cast<i32>(s.gain_q15));
        const i32 a = at[0], b = at[SourceChannels];
        const i32 left = ((a + (((b - a) * fraction) >> kFraction)) * weight) >> 15;
        if (OutputChannels == 2)
            output[i * 2] += opxy::q31::multiply(left, gain_left);
        i32 right = left;
        if (SourceChannels == 2) {
            const i32 c = at[1], d = at[3];
            right = ((c + (((d - c) * fraction) >> kFraction)) * weight) >> 15;
        }
        if (OutputChannels == 2) {
            output[i * 2 + 1] += opxy::q31::multiply(right, gain_right);
        } else {
            output[i] += (left + right) >> 1;
        }
        position += step;
        phase += window_step;
    }
    grain.position = position;
    grain.window_phase = phase;
    if (finished) grain.window_step = 0;
}

template <unsigned SourceChannels, unsigned OutputChannels>
void render_runs(Voice& voice, const Settings& s, u32 position, i32* output) {
    silence(output, OutputChannels);
    unsigned offset = 0;
    while (offset < kBlock) {
        // The original scheduler decrements before testing, so a countdown
        // of one starts a grain on this sample, not the following sample.
        if (voice.next > 1) --voice.next;
        else { spawn(voice, s, position); voice.next = s.interval; }
        unsigned count = voice.next;
        if (count > kBlock - offset) count = kBlock - offset;
        for (unsigned g = 0; g < kGrains; ++g)
            grain_run<SourceChannels, OutputChannels>(voice.grains[g], s, voice.sample.data,
                                                      output + offset * OutputChannels, count);
        voice.next -= count - 1;
        offset += count;
    }
    for (unsigned i = 0; i < kBlock * OutputChannels; ++i)
        output[i] = saturate16(output[i]) * 32768;
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice = Voice();
    voice.sample = sample;
    voice.rng = 0x9e3779b9u ^ sample.frames ^ sample.rate;
}

void start(Voice& voice) {
    for (unsigned i = 0; i < kGrains; ++i) voice.grains[i].window_step = 0;
    voice.next = 0;
    voice.scan_position = 0;
    random(voice);
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const u32 channels = block.output_channels == 2 ? 2 : 1;
    const opxy::SampleView& sample = voice.sample;
    if (!sample.data || !sample.frames || sample.frames >= (1u << 20) || sample.channels < 1 || sample.channels > 2) {
        silence(block.output, channels);
        return;
    }
    const Settings s = settings(voice, block);
    if (s.length < 8 || !s.step || !s.gain_q15) {
        for (unsigned i = 0; i < kGrains; ++i) voice.grains[i].window_step = 0;
        silence(block.output, channels);
        return;
    }
    scan(voice, s, block.parameters[4]);
    const u32 position_q31 = unsigned_q31(block.parameters[0]);
    if (sample.channels == 2) {
        if (channels == 2) render_runs<2, 2>(voice, s, position_q31, block.output);
        else render_runs<2, 1>(voice, s, position_q31, block.output);
    } else {
        if (channels == 2) render_runs<1, 2>(voice, s, position_q31, block.output);
        else render_runs<1, 1>(voice, s, position_q31, block.output);
    }
}
}
