# Granular for Open-XY

A standalone sample instrument for the OP-XY: a cloud of windowed grains
from a frozen or scanning position, with stereo spread and a custom display.
Original Granular engine and display by **veff**, from
[the galfaroth submission](https://github.com/galfaroth/open-xy/commit/9e9f87b811c869049a0732806521a50f48a41e34);
see [NOTICE.md](NOTICE.md) and [LICENSE](LICENSE).

## SDK requirement

Targets Open-XY SDK **1.1**, currently on
[`sdk/sample-machine-pages`](https://github.com/etcetc000/open-xy/tree/sdk/sample-machine-pages).
It uses the standard `sample.cpp`, drawing callbacks, `singleSample` and
`stereoOutput` declarations. It contains no firmware addresses, assembly,
platform code or build scripts. The SDK owns assignment, registration,
controls, storage and packaging.

This is a development port undergoing hardware evaluation. It is not yet a
published catalog release. The SDK branch must land before catalog admission.
The browser builder can include its artwork and firmware objects; sample
instruments do not yet have browser audio preview.

## Controls

Press an encoder to switch between its two parameters. Shift + turn makes
fine adjustments to the shown parameter.

| Encoder | Main | Alternate |
| --- | --- | --- |
| 1 | Position in the trimmed sample | Scan, backward/frozen/forward; the middle tenth freezes |
| 2 | Grain size, 10-500 ms | Tune, -12 to +12 semitones |
| 3 | Density, 0.5-8 grains per grain length | Stereo spread |
| 4 | Spray, up to 2 seconds | Volume |

The sample button assigns one sample across the track's keyboard. Notes play
chromatically around middle C; saved region tuning, trim, direction and level
apply. Source sample rates and mono/stereo layouts are respected. Each voice
has a bounded pool of nine grains, including a slot for scheduling rounding.
The SDK supplies all 256 stock voice handles.

The screen shows the loaded sample's waveform, position and spray indicators,
a grain window and simulated grain motion. The SDK prepares a copied waveform
overview in the background; this instrument draws it with ordinary SVG paths.
Dots continue while the SDK reports sounding notes, including release tails.
Their positions and count illustrate the controls, not individual audio grains.
Presets include Cloud, Freeze, Scanner and Stutter.

Maximum density processes about eight overlapping grains per sounding note.
The 256 storage handles do not establish playable polyphony. Audio runs each
grain in short batches between spawn boundaries to reduce repeated scheduling
and state traffic; a deterministic sound-reference test guards its arithmetic.
Device CPU qualification, including the SDK's 24-note planning target, remains
open. The SDK build and test commands report this cost as unestimated.

## Check and build

From an SDK 1.1 checkout, with this repository beside it:

```sh
python sdk.py test --machine ../open-xy-granular --host-cxx clang++
python sdk.py compile --machine ../open-xy-granular --out <external-output>/granular-module
python sdk.py build --machine ../open-xy-granular --stock-os <stock>/os.ldr --stock-content <stock>/content --out <external-output>/granular-build
```

Use fresh output directories outside every checkout. `tests/` checks silence,
stereo/mono buffer boundaries, scan direction and freeze, live trim changes,
density and display behavior. Firmware verification belongs to the SDK's
[release process](https://github.com/etcetc000/open-xy/blob/sdk/sample-machine-pages/docs/build.md#release-verification).
No command writes a device.

Engine 36 and page 84 in `machine.json` are requests. Each firmware's saved
layout assigns stable numbers and can choose others when these are occupied.
Keep this machine's ID and parameter order stable for saved projects.
