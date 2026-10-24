// Compare the optimized renderer with independent scalar scheduling/mixing.
// Covers channel layouts, changing controls, trim/direction edits and retriggers.
#include <cstdio>
#include <cstdlib>
#include "sample.cpp"
#include "reference_renderer.h"
int main() {
    static opxy::i16 pcm[88200];
    opxy::u32 random = 0x12345678u, hash = 2166136261u;
    for (auto& value : pcm) { random = random * 1664525u + 1013904223u; value = static_cast<opxy::i16>(random >> 16); }
    opxy::i32 parameters[8], output[130], expected[128];
    opxy::u32 pitch[64];
    const opxy::u32 rates[] = {22050, 44100, 48000};
    for (unsigned source_channels = 1; source_channels <= 2; ++source_channels)
        for (unsigned output_channels = 1; output_channels <= 2; ++output_channels)
            for (auto rate : rates) {
                granular::Voice voice{}, reference{};
                opxy::SampleView sample{pcm, 44100, rate, source_channels};
                granular::initialize(voice, sample); granular::start(voice);
                granular::initialize(reference, sample); granular::start(reference);
                opxy::SampleBlock block{output + 1, parameters, pitch, {1200, 0, true},
                    {0, 44100, opxy::sample_unity_level, false, 0}, nullptr, 0, output_channels, 0, 0};
                for (unsigned b = 0; b < 1300; ++b) {
                    if (b % 97 == 0) { granular::start(voice); granular::start(reference); }
                    if (b % 17 == 0) {
                        for (auto& parameter : parameters) {
                            random = random * 1664525u + 1013904223u;
                            parameter = static_cast<opxy::i32>(random >> 1);
                        }
                        parameters[2] = 2147483647;  // sustained maximum overlap
                        for (auto& value : pitch) value = 12740059u + (random & 0x1ffffffu);
                        block.region.begin = (b % 3) * 173;
                        block.region.end = b % 5 ? 44100 - (b % 7) * 419 : block.region.begin + 7;
                        block.region.reverse = b % 2;
                        block.region.pitch_cents = static_cast<opxy::i32>(b % 2401) - 1200;
                    }
                    output[0] = 1234567; output[1 + 64 * output_channels] = 7654321;
                    granular::render_sample(voice, block);
                    auto reference_block = block;
                    reference_block.output = expected;
                    granular::reference_render(reference, reference_block);
                    if (output[0] != 1234567 || output[1 + 64 * output_channels] != 7654321) std::abort();
                    for (unsigned i = 0; i < 64 * output_channels; ++i) {
                        if (output[i + 1] != expected[i]) std::abort();
                        hash = (hash ^ static_cast<opxy::u32>(output[i + 1])) * 16777619u;
                    }
                }
            }
    // Six-grain schedule; the independent scalar renderer must agree first.
    if (hash != 0x4ffbe5c5u) std::abort();
    std::printf("render reference: %08x\n", hash);
}
