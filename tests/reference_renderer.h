// Scalar reference for grain scheduling, bounds and fixed-point mixing.
// Uses the six-grain controls; rendering stays sample-by-sample.
namespace granular {
static void reference_render(Voice& voice, const opxy::SampleBlock& block) {
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
    const u32 begin_q12 = s.begin << kFraction;
    const i16* data = sample.data;
    const u32 source_channels = sample.channels;
    i32* output = block.output;
    for (unsigned i = 0; i < kBlock; ++i) {
        if (voice.next) --voice.next;
        if (!voice.next) {
            spawn(voice, s, position_q31);
            voice.next = s.interval;
        }
        i32 left_sum = 0, right_sum = 0;
        for (unsigned g = 0; g < kGrains; ++g) {
            Grain& grain = voice.grains[g];
            if (!grain.window_step) continue;
            // Live trim edits may exclude a grain retained from the previous block.
            if (grain.position - begin_q12 > s.limit_q12) { grain.window_step = 0; continue; }
            const u32 frame = grain.position >> kFraction;
            const i32 fraction = static_cast<i32>(grain.position & (kUnit - 1));
            const i16* at = data + frame * source_channels;
            const i32 a = at[0], b = at[source_channels];
            i32 left = a + (((b - a) * fraction) >> kFraction);
            i32 right = left;
            if (source_channels == 2) {
                const i32 c = at[1], d = at[3];
                right = c + (((d - c) * fraction) >> kFraction);
            }
            // Window and gain in one Q15 factor (at most 65535), then pan.
            const i32 weight = static_cast<i32>(((granular_window[grain.window_phase >> 22] >> 16) * s.gain_q15) >> 15);
            left = (left * weight) >> 15;
            right = (right * weight) >> 15;
            if (channels == 2) {
                left_sum += (left * grain.gain_left) >> 15;
                right_sum += (right * grain.gain_right) >> 15;
            } else {
                left_sum += (left + right) >> 1;
            }
            grain.position += static_cast<u32>(grain.step);
            const u32 next_phase = grain.window_phase + grain.window_step;
            // The window completing, or leaving the readable region, frees the grain.
            if (next_phase < grain.window_phase || grain.position - begin_q12 > s.limit_q12) grain.window_step = 0;
            else grain.window_phase = next_phase;
        }
        output[i * channels] = saturate16(left_sum) * 32768;
        if (channels == 2) output[i * channels + 1] = saturate16(right_sum) * 32768;
    }
}
}
