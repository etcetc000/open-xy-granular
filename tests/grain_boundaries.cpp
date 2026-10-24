// Compare batched grain bounds against the original scalar stop conditions.
#include <cstdlib>
#include "sample.cpp"
using namespace granular;
static void check(bool condition) { if (!condition) std::abort(); }
template<unsigned Source, unsigned Output>
static void exercise() {
    static i16 pcm[520];
    for (unsigned i = 0; i < 520; ++i) pcm[i] = static_cast<i16>(i * 11713u);
    const unsigned counts[] = {1, 2, 31, 32, 63, 64};
    const u32 positions[] = {0, 1, 4095, 4096, 255 * 4096, 256 * 4096};
    const i32 steps[] = {-1048576, -65536, -4096, -1, 1, 4096, 65536, 1048576};
    const u32 phases[] = {0, 0xffffffffu, 0xffffffffu - 9739154u, 0xffffff00u};
    const u32 windows[] = {0xffffffffu / 441, 0xffffffffu / 22050};
    for (auto count : counts) for (auto position : positions) for (auto step : steps)
        for (auto phase : phases) for (auto window : windows) {
            Settings settings{};
            settings.begin = 2; settings.limit_q12 = 256 * 4096;
            settings.gain_q15 = 32767;
            Grain grain{position + 8192, step, phase, window, 12345, 32767};
            Grain reference = grain;
            i32 actual[130], expected[130];
            for (unsigned i = 0; i < 130; ++i) actual[i] = expected[i] = 123;
            for (unsigned i = 0; i < count; ++i) {
                if (reference.position - 8192 > settings.limit_q12) { reference.window_step = 0; break; }
                const u32 frame = reference.position >> 12;
                const i32 fraction = reference.position & 4095;
                const i16* at = pcm + frame * Source;
                i32 left = at[0] + (((at[Source] - at[0]) * fraction) >> 12);
                i32 right = Source == 2 ? at[1] + (((at[3] - at[1]) * fraction) >> 12) : left;
                const i32 weight = (granular_window[reference.window_phase >> 22] * settings.gain_q15) >> 15;
                left = (left * weight) >> 15; right = (right * weight) >> 15;
                if (Output == 2) {
                    expected[1 + i * 2] += (left * reference.gain_left) >> 15;
                    expected[2 + i * 2] += (right * reference.gain_right) >> 15;
                } else expected[1 + i] += (left + right) >> 1;
                reference.position += static_cast<u32>(reference.step);
                const u32 next = reference.window_phase + reference.window_step;
                if (next < reference.window_phase || reference.position - 8192 > settings.limit_q12) {
                    reference.window_step = 0; break;
                }
                reference.window_phase = next;
            }
            grain_run<Source, Output>(grain, settings, pcm, actual + 1, count);
            for (unsigned i = 0; i < 130; ++i) check(actual[i] == expected[i]);
            check(grain.position == reference.position && grain.window_step == reference.window_step);
            if (grain.window_step) check(grain.window_phase == reference.window_phase);
        }
}
int main() { exercise<1, 1>(); exercise<1, 2>(); exercise<2, 1>(); exercise<2, 2>(); }
