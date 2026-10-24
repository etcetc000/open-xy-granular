#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "sample.cpp"

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
int main() {
    using namespace granular;
    static opxy::i16 pcm[88200];
    for (unsigned i = 0; i < 88200; ++i) pcm[i] = static_cast<opxy::i16>((i * 97u) % 30000u - 15000);
    opxy::u32 pitch[64];
    for (auto& p : pitch) p = 25480119u;  // Middle C at 44.1 kHz.
    opxy::i32 parameters[8] = {257698038, 1073741824, 1073741824, 214748365,
                               1073741824, 1073741824, 644245094, 2147483647};
    opxy::i32 guarded[130];
    opxy::SampleView sample{pcm, 44100, 44100, 2};
    Voice voice{};
    initialize(voice, sample); start(voice);
    opxy::SampleBlock block{guarded + 1, parameters, pitch, {1200, 0, true},
        {0, 44100, opxy::sample_unity_level, 0, false}, nullptr, 0, 2, 0, 0};
    bool sounded = false;
    for (unsigned b = 0; b < 100; ++b) {
        guarded[0] = 123456; guarded[129] = 654321;
        render_sample(voice, block);
        check(guarded[0] == 123456 && guarded[129] == 654321, "Stereo block overwrote its bounds");
        check(voice.scan_position == 0, "Default scan must freeze");
        for (unsigned i = 1; i < 129; ++i) sounded |= guarded[i] != 0;
    }
    check(sounded, "A valid sample must sound");
    for (opxy::i32 scan_value : {0, 2147483647}) {
        start(voice); parameters[4] = scan_value;
        render_sample(voice, block);
        check(scan_value == 0 ? voice.scan_position > (43800u << 12) :
                               voice.scan_position > 0 && voice.scan_position < (200u << 12),
              "Scan must move in both directions");
    }
    // A new start resets drift, and live trim changes invalidate old grains before reading them.
    start(voice); check(voice.scan_position == 0, "Start must reset scan");
    parameters[4] = 1073741824;
    render_sample(voice, block);
    block.region.begin = 44080; block.region.end = 44100;
    render_sample(voice, block);
    for (const auto& grain : voice.grains)
        check(!grain.window_step || (grain.position >> 12) >= 44080, "Live trim left a grain outside its region");
    for (unsigned length : {0u, 1u, 7u}) {
        block.region.begin = 0; block.region.end = length;
        render_sample(voice, block);
        for (unsigned i = 1; i < 129; ++i) check(guarded[i] == 0, "Empty and tiny regions must be silent");
    }
    // Maximum density must sustain six grains, with no rounding overflow slot.
    block.region.end = 44100; parameters[2] = 2147483647;
    for (opxy::i32 size : {0, 357913941, 1073741824, 2147483647}) {
        parameters[1] = size;
        const auto configured = settings(voice, block);
        check(configured.interval >= 74, "Minimum spawn spacing must bound block work");
        check(configured.interval * kGrains >= configured.size + 1,
              "Six slots must cover the complete window");
        start(voice); unsigned maximum = 0;
        for (unsigned b = 0; b < 800; ++b) {
            render_sample(voice, block);
            unsigned count = 0;
            for (const auto& grain : voice.grains) count += grain.window_step != 0;
            if (count > maximum) maximum = count;
        }
        check(maximum == 6, "Maximum density must sustain exactly six grains");
    }
    sample.channels = 1; sample.rate = 48000;
    initialize(voice, sample); start(voice); block.output_channels = 1;
    guarded[65] = 123456;
    render_sample(voice, block);
    check(guarded[65] == 123456, "Mono block overwrote its bounds");
    sample.data = nullptr; initialize(voice, sample); start(voice);
    render_sample(voice, block);
    for (unsigned i = 1; i < 65; ++i) check(guarded[i] == 0, "Missing sample must be silent");
}
