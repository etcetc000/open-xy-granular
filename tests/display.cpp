#include <cstdlib>
#include "../display.cpp"
static opxy::SampleDisplay sample{};
static bool available = true;
extern "C" bool opxy_sample_display(opxy::u32, opxy::SampleDisplay* out) { *out = sample; return available; }
int main() {
    granular::DisplayState state{};
    granular::Frame frame{};
    opxy::draw::Input input{};
    granular::display_reset(state);
    for (auto& slot : input.slots) slot = 1073741824;
    input.elapsed = 16;
    available = false;
    granular::display(state, input, frame);
    if (!opxy::draw::valid(frame)) std::abort();
    available = true;
    sample.generation = 1; sample.frames = sample.end = 44100;
    sample.rate = 44100; sample.channels = 2; sample.active_notes = 1;
    for (unsigned bin = 0; bin < opxy::sample_display_bins; ++bin) {
        sample.low[bin] = -16000; sample.high[bin] = 16000;
    }
    for (unsigned i = 0; i < 300; ++i) {
        input.note_serial = 1;  // one held note, not repeated triggers
        input.slots[0] = i < 100 ? 0 : i < 200 ? 2147483647 : 1073741824;
        granular::display(state, input, frame);
        if (!opxy::draw::valid(frame)) std::abort();
        unsigned visible = 0;
        for (unsigned g = 0; g < granular::kDisplayGrains; ++g)
            visible += frame.nodes[7 + g].x != opxy::draw::hidden_x * opxy::draw::unit;
        if (i > 10 && !visible) std::abort();
        for (unsigned path = 1; path < 4; ++path)
            if (frame.nodes[path].x == opxy::draw::hidden_x * opxy::draw::unit) std::abort();
        for (const auto& node : frame.nodes)
            if (node.scale <= 0 || node.shade >= 16) std::abort();
    }
    if (frame.paths[1].points[0].y >= 0 || frame.paths[1].points[63].y <= 0) std::abort();
    const auto wave_key = frame.paths[1].key;
    // Note presses must neither hide nor rebuild a stable loaded waveform.
    for (unsigned i = 0; i < 60; ++i) {
        ++input.note_serial;
        sample.active_notes = 1 + i % 6;
        granular::display(state, input, frame);
        if (!opxy::draw::valid(frame) || frame.paths[1].key != wave_key) std::abort();
        for (unsigned path = 1; path < 4; ++path)
            if (frame.nodes[path].x == opxy::draw::hidden_x * opxy::draw::unit) std::abort();
    }
    granular::display(state, input, frame);
    if (frame.paths[1].key != wave_key) std::abort();
    ++sample.generation; sample.high[0] = 8000;
    granular::display(state, input, frame);
    if (frame.paths[1].key == wave_key) std::abort();
    sample.active_notes = 0;
    for (unsigned i = 0; i < 100; ++i) granular::display(state, input, frame);
    for (unsigned g = 0; g < granular::kDisplayGrains; ++g)
        if (frame.nodes[7 + g].x != opxy::draw::hidden_x * opxy::draw::unit) std::abort();
    available = false;
    granular::display(state, input, frame);
    if (frame.nodes[1].x != opxy::draw::hidden_x * opxy::draw::unit) std::abort();
    input.reduced_motion = 1;
    granular::display(state, input, frame);
    if (state.scan_offset != 0) std::abort();
    // The 24-pixel grain window must remain in Q10, not collapse below a pixel.
    if (frame.paths[0].points[31].y > -23 * opxy::draw::unit ||
        frame.paths[0].points[31].y < -24 * opxy::draw::unit) std::abort();
}
