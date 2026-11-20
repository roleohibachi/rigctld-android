# Contributing hardware fixes

Hardware owners can reproduce, test and submit fixes on the exact setup that fails. If you have the radio, you can lead the fix even if you are new to the code. An AI coding assistant can help trace the path, write a patch and prepare your pull request. Reports with a clear reproduction are welcome if you cannot take the fix further.

## Reproduce and locate the failure

1. Record the app revision, Hamlib revision, phone model, Android version, rig and firmware, transport, and selected Hamlib model. For CAT, include port, baud, framing, flow control and CAT mode. For USB, include adapter chipset or VID/PID and interface number where known. For audio, include requested and actual input/output routes, sample rates, channel counts and the failure timing if available. For Bluetooth audio, include the profile if known, the affected direction and whether CAT was active at the same time.
2. Reproduce with the fewest steps possible. Include the expected result, observed result and a short log excerpt from a single run. Start with receive-only queries such as frequency and mode. Review logs for private information including Bluetooth addresses, device serial numbers, credentials and station identifiers before sharing.
3. If the same connection is available on Linux, compare against stock Hamlib with the same rig model, transport and settings. Record the exact version used. For serial CAT, a first comparison is `rigctl -m MODEL_ID -r SERIAL_DEVICE -s BAUD f`, substituting the actual model, device and rate and applying the same additional settings if needed. Use `m` to query mode. If current upstream fixes the problem, record that revision so this project can consider updating its pin.

| Where the evidence points | Where to contribute |
| --- | --- |
| Android permissions, USB/Bluetooth bridge, app service lifecycle, packaging or audio routing | Open your PR in this Android project. |
| Hamlib model/backend behavior or rigctld protocol reproduced in stock Hamlib, or otherwise isolated to Hamlib source | Prepare your PR in [Hamlib](https://github.com/Hamlib/Hamlib). If you need upstream help identifying the fix, bring the minimal reproduction to [Hamlib issues](https://github.com/Hamlib/Hamlib/issues). |
| Ownership unclear, or the hardware path exists only on Android | Start here with the evidence. A comparison across different transports may leave ownership unresolved. |

## Turn the reproduction into your PR

1. Fork the repository that owns the defect and create a branch. Share the sanitized reproduction with your coding assistant. For example, “Read AGENTS.md if present. Trace this failure, identify the project that owns the fix, and help me prepare the smallest patch. Give me the exact checks to run on my hardware and draft the PR for my review.”
2. Build from the documented toolchain for that repository. Run the same minimum reproduction before and after the patch. Include a fake-transport test or sanitized transcript when it can demonstrate the regression without the radio. Keep real TX opt-in, low power and into a dummy load.
3. Review the diff and open the PR from your fork. Include exact revisions, the hardware and settings, reproduction steps, before/after results, automated test results and any gaps. If both projects need changes, link their PRs.

The [audit](AUDIT.md) identifies the Android reference paths and the planned build and hardware verification sequence.
