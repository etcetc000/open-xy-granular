#include <cstdlib>
#include "../display.cpp"
int main() {
    granular::DisplayState state{};
    granular::Frame frame{};
    opxy::draw::Input input{};
    granular::display_reset(state);
    for (auto& slot : input.slots) slot = 1073741824;
    input.elapsed = 16;
    for (unsigned i = 0; i < 100; ++i) {
        input.note_serial = i / 10;
        granular::display(state, input, frame);
        for (const auto& node : frame.nodes)
            if (node.scale <= 0 || node.shade >= 16) std::abort();
    }
    input.reduced_motion = 1;
    granular::display(state, input, frame);
    if (state.scan_offset != 0) std::abort();
}
