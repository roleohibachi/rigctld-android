# Android radio service — Codex project handoff

2026-11-20 · Working title: Android rigctld (not a branding decision)

## Goal

Build a small, standalone, open-source Android application that provides common amateur-radio hardware infrastructure to independent modem applications. Configure a radio once, then use it with any compatible client. Prefer reusing JS8Android and upstream Hamlib code over writing new infrastructure. Keep modem DSP, workflows and UI in their respective applications.

Android is the only application target. Existing Linux rigctld remains useful as a client/server comparison for development and diagnosis. iOS, macOS and Windows app ports are out of scope.

Long-term clients could include JS8Android, FreeDV/RADE, SSTV, RTTY/CW, APRS/Dire Wolf, FT8, XARA/VARA-compatible clients and Mercury. Existing higher-level interfaces (KISS, JS8 JSON, modem TCP APIs, etc.) should remain intact. A universal modem frontend is explicitly out of scope.

## Starting material

- JS8Android: https://github.com/JS8Call-improved/Android-port
- Hamlib: https://github.com/Hamlib/Hamlib
- Hamlib streaming initiative: https://github.com/Hamlib/Hamlib/issues/1940
- Current rigctld manual: https://hamlib.sourceforge.net/snapshots/man1/rigctld.1.html
- Initial streaming commit: https://github.com/Hamlib/Hamlib/commit/2ef1e1dade5ce8336ee724bf274952ed93940dd0
- Alternative reference: https://github.com/pepefrog1234/RADE_decode_Android (evaluate its rigctld packaging and serial bridging; verify implementation and licensing before reuse).
- Optional reference for Android native audio: https://github.com/google/oboe
- Prior discussion (members-only): https://js8call.groups.io/g/main/topic/116923988

### Maintainer context

On January 6, 2026, AD8IS proposed a separate Hamlib Android app managing USB/BT and serving local rigctld. Kelly-Shane Fuller (punk-kaos), JS8Android developer, responded that he sees the upside but lacks bandwidth to maintain two apps. He said he deliberately made the Hamlib port portable and that other apps could reuse it with a small USB shim. He also separated the JS8 core into a library for portability. Source: Groups.io thread, PDF supplied by user, pp. 19–20. This is encouragement, not permission to make decisions on his behalf or a commitment by him to integrate the result.

AD8IS also tested early Bluetooth CAT/audio with the FX-4CR successfully (Jan 9, p. 14); early USB audio on that rig was not working. The app has evolved since those reports. Do not treat January bugs as current without retesting. The new Hamlib streaming work postdates the original proposal.

Project should not add maintenance obligations to Kelly-Shane. Prefer contributing portable fixes upstream and coordinating shared libraries if he is interested; the new project should be independently maintainable. Do not publish, contact upstream, or open PRs/issues without user authorization.

## Design principles

1. Reuse working code, minimize patches, and do not carry JS8-specific DSP/UI/build dependencies into the new app.
2. Use genuine upstream rigctld protocol and implementation wherever feasible; do not write a partial clone if an embeddable or executable daemon can work. Bind TCP to loopback by default (127.0.0.1:4532); no LAN exposure by default. Consider Android app-to-app trust: loopback is not private to one app.
3. Centralize USB permissions, serial/BT transport, rig profile, CAT/PTT lifecycle, and eventually physical audio device selection/calibration. Prefer explicit user selection and actual route verification; never silently route transmit audio to the phone speaker. Stock Android does not generally let ordinary apps provide a system-wide virtual sound card. Clients will need explicit streaming support or an adapter.
4. Preserve standalone use of JS8Android. It may eventually consume a shared library or connect as a client; neither is required for first release. Avoid a hard dependency on an unapproved upstream change.
5. No root, custom ROM, cloud backend or new wire protocol for MVP. No persistent background work without appropriate Android foreground-service lifecycle and permissions.
6. Separate hardware-level audio calibration from modem-specific drive/offset/timing. Prefer native audio transport and upstream resampling/buffering where possible.
7. Treat RF transmission as a shared resource. Do not automatically key during testing. Design explicit transmit ownership, safe disconnect/stop behavior, and PTT failsafes; distinguish what software can guarantee from a failed physical CAT connection. Use dummy load/low power and deliberate user consent for real TX tests.
8. Help hardware owners reproduce, fix, verify and submit bugs as their own pull requests, with AI assistance if useful. Route confirmed upstream Hamlib defects to Hamlib. Follow [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).

## Phase 0 — audit first (initial Codex task)

Inspect the cloned JS8Android repository read-only before implementing. Record exact git commit, branch, and relevant licenses. Also inspect the matching pinned Hamlib revision and current upstream development branch, documenting actual versions/revisions.

- Trace Gradle/CMake/NDK build scripts, native Hamlib linkage, Android patches, JNI, rig abstraction, USB serial bridge, Bluetooth CAT, network CAT and rig shutdown. Determine where OS-owned handles are opened and how native code consumes them. Locate actual files, classes and entry points with line references. Verify whether build scripts pin an older revision and what changes are required to update.
- Trace audio device enumeration, actual selected routes, audio capture/playback, BT profile handling, gain, resampling, lifecycle, PTT ordering, and special rig protocols (QMX/(tr)uSDX/others if implemented). Separate generic code from modem-specific code and identify dependencies.
- Compare ways to run real rigctld on unrooted Android: in-process native daemon via JNI vs app-packaged native executable where platform rules permit vs other upstream-supported mechanisms. Check permissions, executable packaging restrictions, USB/BT bridging, shutdown semantics, and how to avoid reinventing the command parser. Examine RADE only as a reference if useful.
- Inspect Hamlib streaming docs and actual source at a recorded revision (`HAMLIB_STREAMING.md`, rigctld, backend callbacks, tests). Determine whether generic USB/Bluetooth audio can be attached to an arbitrary CAT backend, or whether a backend/wrapper/extension is necessary. Confirm how RX/TX, rate negotiation, timing, metadata, and multiple clients actually work. Do not assume these are already implemented on Android, generic sound cards, or all radios.
- Determine a minimal extraction strategy: existing files to reuse, adaptations, dependencies, license obligations, and what must be written. Identify patches suitable for Hamlib upstream versus Android-only glue. Consider the simplest viable independent source layout and a later shared library; do not prematurely refactor JS8Android.
- If build tools are already available, inspect/run a baseline build without editing tracked source. Report missing SDK/NDK/tooling or other blockers; no destructive setup steps.

Deliver `AUDIT.md` with architecture/data-flow maps, a file/symbol inventory, dependency and license notes, exact revisions, alternatives and their tradeoffs, open questions, and recommended MVP implementation/test plan. Distinguish confirmed findings from hypotheses. Stop after audit and recommendation; no production source changes during phase 0.

## Hardware and Android coverage

The project owner’s only Android device is a Pixel 8a. The first reported run used Android 17 (API 37). See [verification status](README.md#verification-status) for completed checks. Their radios cover these paths:

| Rig | CAT and audio paths to verify |
| --- | --- |
| FX-4CR | USB CAT with USB audio, and Bluetooth CAT with Bluetooth audio. Exercise both Hamlib model selections over each transport: BG2FX FX4/C/CR/L (2053) and Kenwood TS-590S (2031). |
| (tr)uSDX | USB serial carrying both CAT and rig-specific audio on the same link. Verify CAT independently in Phase 1 and the multiplexed RX/TX path in Phase 2. |

Target affordable, older Android hardware. Android 12 (API 31) is the provisional minimum for the full feature set because it provides explicit communication-device routing for Bluetooth audio. This is a design target pending hardware coverage. Other testers will need to verify additional phone models, the Android version floor, and other radios. Record verified combinations by phone, Android version, rig firmware, Hamlib model and transport. See the [compatibility rationale](AUDIT.md#android-version-and-hardware-coverage).

## Phase 1 — CAT-only MVP

Build a minimal independent APK, reusing the selected components. Record provenance, retain licenses/notices and maintain reproducible builds. Keep UI limited to rig model, transport, serial/BT device, serial parameters, start/stop, connection status, localhost endpoint and useful diagnostics. Support USB serial then Bluetooth SPP (classic RFCOMM; BLE or other profiles only if actually supported by reused code). Expose the standard rigctld protocol.

Acceptance criteria:
- Installs on ordinary unrooted ARM64 Android. The Pixel 8a is the primary test device. Validate the proposed Android version floor on an older device through a contributor.
- Standard desktop Hamlib rigctl client can connect through adb forwarding, and an Android client can connect via loopback; no bespoke CAT protocol needed.
- Dummy backend or fake serial tests cover protocol and service lifecycle before hardware use.
- On the FX-4CR, read/set frequency and mode and query PTT for both Hamlib models over USB and Bluetooth. Run a CAT-only sanity check on the (tr)uSDX using its Hamlib model. Confirm actual radio settings. Test PTT only deliberately on dummy load/low power. Validate errors, unplug/replug, BT disconnect, permission denial, service stop and safe TX release behavior.
- JS8Android can use its existing NET rigctl mode for CAT while continuing to manage its own audio during this phase, where hardware/transport permits. Verify; do not require edits to JS8Android for MVP.
- No physical radio required for automated test suite; no automatic over-the-air TX.

## Later phases (not MVP)

### Phase 2: shared audio

Android rigctld owns the physical CAT and audio routes for each rig profile. Configure a modem client once to use Android rigctld as its CAT and audio source. Switching profiles in Android rigctld may require the client to reconnect or renegotiate the stream. Its configured endpoint and audio source must stay the same.

Reuse Android audio and special rig transport code where practical. Prove reliable device selection (input/output), verified route, capture/playback, gain and diagnostics on real hardware. Handle sample rates and timestamps; account for Android processing and Bluetooth profile limitations. Keep ordinary UI audio separate from RF audio. Define behavior on hot unplug/reconnect, starvation, backgrounding and route changes. Use foreground service as needed. Do not hold microphone unnecessarily during CAT-only operation.

Evaluate Oboe/AAudio first for low-latency USB PCM in both directions. Retain Android framework audio as a comparison or route-specific fallback if measurements justify it. See the [Phase 0 audit](AUDIT.md) for routing and latency acceptance checks.

The first USB receive milestone is the FX-4CR on the stock Pixel 8a. The Pixel exposes QCC3024 (`0a12:4007`) to framework audio clients even while USB host enumeration also sees C-Media (`0d8c:013c`). The receive-only monitor uses an Android-granted `UsbDeviceConnection` file descriptor and native usbfs isochronous transfers. The owner reported working audio on C-Media and no frames on QCC3024. Reuse this capture path for the FX-4CR profile and keep it independent of the CH340 CAT interface. Use Oboe/AAudio for profiles whose selected endpoints are available through framework audio. [Linux and Pixel observations](README.md#verification-status) [Android USB audio policy](https://android.googlesource.com/platform/frameworks/base/+/master/services/usb/java/com/android/server/usb/UsbAlsaManager.java) [Android USB connection](https://developer.android.com/reference/android/hardware/usb/UsbDeviceConnection#getFileDescriptor())

Keep CAT PTT off for this milestone. First report the actual USB identity, format, frame count, peak/RMS and capture errors while reading CAT frequency on the CH340. With a known RX signal, confirm continuous audio at 48 kHz mono PCM16 after the QCC3024 is present and after unplug/replug. Report an unavailable C-Media route to the user and stop capture on route loss. Then feed that capture into JS8Android over the service's fixed audio endpoint and verify decoding. Stock microphone-only apps remain a separate compatibility check under Android's input policy.

Offer Bluetooth audio alongside USB for rigs that support it. A rig profile should specify the CAT transport, audio transport and supported RX/TX directions. Use Android’s communication audio route for supported headset-profile rigs and evaluate vendor audio links separately. Start Bluetooth verification with the FX-4CR from the earlier JS8Android report. Confirm the actual RX/TX routes, concurrent CAT and audio, usable bandwidth, latency and reconnect behavior before listing a rig and profile as supported. On route loss, mute TX audio and request PTT release.

Include (tr)uSDX audio over its shared CAT serial link in this phase. One session should own the port and handle CAT reply and PCM framing together.

### Phase 3: upstream Hamlib streaming integration

Prefer Hamlib's existing streaming architecture once its support for generic audio hardware is established. If upstream lacks a suitable hook, propose a small reusable extension or backend; don't invent a competing protocol without showing why reuse fails. Prove RX with one client and then multiple if supported; prove bidirectional TX with buffering/timing and exclusive ownership. Verify client access control, PTT timing, disconnect and timeout recovery. Test negotiated audio against known samples and measure latency/jitter, not just waterfall appearance. Preserve CAT-only compatibility.

Use Hamlib receive streaming as the client interface. A client selects **Rigctld audio** and negotiates an RX stream at the same `127.0.0.1:4532` address it uses for CAT. The initial stream is 48 kHz mono PCM16. Keep the TCP control listener and negotiated UDP sockets on loopback. Prototype against the audited Hamlib revision with a dummy backend first, including stream ownership, disconnect cleanup and the existing CAT regression cases. Then attach an Android capture provider to the live CAT rig session through an instance-level integration and keep Hamlib backend capability tables immutable. Let the foreground service own the physical capture session. Feed bounded PCM queues, preserve sample positions, and signal a discontinuity or close the stream on route loss so clients can reconnect to the same address without stale audio. [Hamlib streaming audit](AUDIT.md#hamlib-streaming-integration) [rigctld streaming commands](https://github.com/Hamlib/Hamlib/blob/master/doc/man1/rigctld.1)

### Phase 4: integration and shared dependencies

Build a reusable client adapter/library if needed (existing modem audio abstractions, rather than rewriting DSP). Demonstrate one existing Android modem with shared CAT/audio and one independent RX client. Package a shared Android hardware library only when the boundary is justified; offer it to JS8Android and other interested projects. Evaluate licensing before sharing components across projects.

Use JS8Android as the first modem client. Feed its existing decoder from the service’s PCM stream through an explicit rigctld audio source in the client. First decode over FX-4CR USB with CAT active. Once Bluetooth audio is also verified, switch the FX-4CR profile from USB to Bluetooth in Android rigctld and recover decoding with the same JS8Android settings. Repeat across two rigs once the (tr)uSDX adapter is available.

Use Robot36 next to verify an independent receiver with an existing PCM source boundary, then GGMorse for a narrowband CW decoder and a separate check of stock Android input routing.

Use stock [GGMorse](https://play.google.com/store/apps/details?id=com.ggerganov.GGMorse) to test apps that use the default recording input. On the Pixel 8a, connect the FX-4CR over USB, leave Android rigctld in CAT-only mode with PTT disabled, and close other audio capture apps. Tune to readable CW and open GGMorse with microphone permission. While it decodes, use `adb shell dumpsys media.audio_policy` and `adb shell dumpsys audio` to check the active recording route. Count this as verified only if GGMorse decodes CW and the active input is the rig's USB audio device. Record the Android build, app version, and route. Repeat with Bluetooth once its audio profile is known.

Android selects the default recording input for stock GGMorse. To make GGMorse follow the selected rig profile, evaluate feeding the same rigctld PCM endpoint used by JS8Android into GGMorse's decoder callback. Verify receive-only decoding with a fixed client configuration as the service switches routes. [Android input policy](https://developer.android.com/media/platform/sharing-audio-input) [GGMorse decoder API](https://github.com/ggerganov/ggmorse/blob/master/include/ggmorse/ggmorse.h)

For Robot36, add a rigctld source alongside its existing audio sources. The development branch also supports user-approved Android playback capture, which could serve as an optional receive-only bridge for compatible builds. Robot36 2.17 uses microphone capture. If testing the playback bridge, verify that Robot36 receives rigctld audio and that local media playback stays off the rig's audio output. Overriding Android's system input policy or injecting microphone audio requires privileges unavailable to the stock APK. [Robot36 audio sources](https://github.com/xdsopl/robot36#audio-inputs) [Robot36 2.17 capture](https://github.com/xdsopl/robot36/blob/v2.17/app/src/main/java/xdsopl/robot36/MainActivity.java#L275) [Android playback capture](https://developer.android.com/media/platform/av-capture)

## Working style

- Build small, testable increments with logical commits; keep references and rationale in the repository.
- Favor existing mechanisms over abstraction for its own sake. Prefer current upstream APIs over private forks only after verifying compatibility.
- If source contradicts this handoff, explain the contradiction and recommend changes. This is a planning handoff, not a verified source-code audit or an immutable spec.
- Keep a concise build/test log. Do not fabricate successful hardware tests; ask the user for real-device tests when necessary.
