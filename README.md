# Android rigctld

Android rigctld provides a local CAT service over USB serial or classic Bluetooth RFCOMM. Use the Hamlib dummy rig to check CAT and the receive streaming protocol without connecting a radio. Shared audio from physical radios is planned for a later phase.

## Verification status

The owner installed debug build `0.1-dev` on a Pixel 8a running Android 17 (API 37) and confirmed that the Hamlib dummy rig (model 1) listened on `127.0.0.1:4532`.

On the same phone, an FX-4CR connected over USB using the BG2FX backend (model 2053) and the CH340 interface (`1a86:7523`). Desktop `rigctl` over wireless ADB repeatedly returned 14,064,000 Hz, CW, 500 Hz passband and PTT off. This verifies read-only CAT on that combination. USB writes, stop/restart and the other physical configurations still need device test results.

The owner also reported that Ham2K PoLo on the Pixel successfully connected to the same USB CAT session at `localhost:4532`.

The FX-4CR also passed Bluetooth RFCOMM CAT using model 2053. The CAT frequency followed the owner’s dial change from 7,061,900 to 7,058,700 Hz. A direct rigctld `F` command then tuned to 7,058,800 Hz and a second command restored 7,058,700 Hz. Both returned `RPRT 0`, and each frequency was read back after a delay. PTT was off before and after. The rig’s hardware revision has yet to be recorded.

The owner verified spot tuning in Ham2K Logger Next (HaLo) 26.11.0 over the same Bluetooth CAT session after selecting its rigctld equipment model. A temporary CAT trace showed raw Kenwood commands with the original client profile and valid rigctld replies after the change, including a readback of 14,338,000 Hz in USB mode.

The owner reported successful CW decoding in stock GGMorse with the FX-4CR connected over USB to the Pixel 8a, without an audio adapter or an in-app source selector. Android can automatically route capture to a compatible USB audio device. The active recording device was not independently confirmed. [Android USB audio](https://source.android.com/docs/core/audio/usb#host-mode-applications)

On the same USB setup, the owner reported a gray waterfall in Robot36 2.17 (67) with Android rigctld stopped. The symptom persisted after restarting Robot36 and the rig, reconnecting USB, clearing HaLo's USB launch default, and trying 48 kHz, both stereo channels, and Fixed Point. ADB matched Robot36's active, unsilenced recording session to the rig's `USB-Audio - QCC3024` input. To reproduce, keep PTT off and compare Robot36 with a known-working USB audio app using the same received signal, one app at a time. Use `adb shell dumpsys audio` and `adb shell dumpsys media.audio_policy` to confirm each app's route. The captured signal level and cause are still unknown.

JS8Android also showed a blank waterfall on the Pixel. ADB matched its unsilenced capture to the QCC3024, and all 23 logged sample triplets were zero. On a Linux PC the same FX-4CR connection enumerated two audio inputs, the QCC3024 (`0a12:4007`) and the C-Media USB PnP Sound Device (`0d8c:013c`). `arecord -D hw:CARD=QCC3024,DEV=0 -f S16_LE -r 48000 -c 2 -d 4 -t raw /dev/null` failed with an I/O error and zero frames on two attempts. Using `hw:CARD=Device,DEV=0`, `-c 1` and `-d 8` captured 384,000 frames with a peak of 7,187 and RMS of 1,542 in signed 16-bit units.

The owner confirmed that the USB audio diagnostics picker from `5a5cda7` lists both FX-4CR inputs on the Pixel 8a. With the direct USB monitor from `fd0c54a`, the owner reported frames from C-Media, none from QCC3024, and level changes when adjusting the rig's AUX output.

On 2026-11-23, the Pixel 8a on Android 17 (API 37) enumerated playback endpoints on both FX-4CR USB audio devices. At the time of the check, Android audio policy exposed QCC3024 (`0a12:4007`, interface 2 alternate 1, endpoint `0x03` OUT) as the only selectable USB playback route. C-Media (`0d8c:013c`, interface 1 alternate 1, endpoint `0x01` OUT) had appeared briefly in the route log during attachment. A separate muted `AudioTrack` probe held the QCC3024 route and its playback head advanced from 30,720 to 82,560 frames at 48 kHz stereo PCM16. The probe sent only zero samples. No PTT command was sent and the subsequent CAT PTT readback was `0`. The owner reported a 0.1 W setting and a dummy load limited to 1 W. Radio modulation and USB route-loss handling remain unverified. Compare `adb shell dumpsys usb` and `adb shell dumpsys media.audio_policy` to reproduce the device and policy inventory.

The direct C-Media output check on the same setup completed 96,384 all-zero stereo PCM16 frames at 48 kHz with zero USB packet errors out of 2,008 packets. A second check stopped on request after 25,728 all-zero frames with zero packet errors. The checks ran with the CAT service stopped. Afterward, C-Media receive sharing resumed, JS8Android reconnected, and CAT again reported PTT off.

The network version of the silence check passed on the same Pixel and FX-4CR. Hamlib sent 48,000 zero-valued mono PCM16 frames through the rigctld TX stream, and the C-Media USB output completed all 48,000 as stereo frames. Across startup, playback and drain, USB completed 2,536 packets with zero errors. The check confirmed simulated PTT off on the dummy rig, which owned the CAT connection for the check.

The Docker host suite verifies 48 kHz mono PCM16 over IPv4 loopback UDP from the embedded dummy rig and from a per-session provider fed a known sample pattern. Both answer CAT on a second TCP connection. The tests also cover stream ownership, disconnect cleanup, multicast rejection, and TX rejection for ordinary sessions. Run them with `bash scripts/docker-build.sh test`.

The host test runner has an explicit `dummy-tx-loopback` mode. Hamlib routes
48 kHz mono PCM16 from the TX stream back through the dummy RX stream with
CAT PTT disabled. The test compares 480 consecutive returned samples.
After building, you can repeat the check in two terminals in the Docker
build environment:

```bash
.work/native/rigctld-host 14532 dummy-tx-loopback
```

```bash
.work/native/test-dummy-tx-loopback 127.0.0.1:14532
```

The test client also accepts a nonsilent, 960-byte raw S16LE file as its second
argument. The normal Android app keeps audio transmit disabled. To serve
the same dummy loopback to Android instrumentation on an ARM64 device:

```bash
bash scripts/docker-build.sh dummy-server
adb push .work/native-dummy-android/rigctld-dummy-test-server /data/local/tmp/
adb shell chmod 755 /data/local/tmp/rigctld-dummy-test-server
adb shell /data/local/tmp/rigctld-dummy-test-server 14532 dummy-tx-loopback
```

Keep the last command running while the Android client test runs. Both TCP
and UDP stay on the test device's loopback interface.

The Docker host suite also exercises a playback fixture with simulated PTT.
One TCP client owns both its 48 kHz mono PCM16 audio stream and PTT, and
audio starts flowing to the sink after a successful key command. For normal
completion, the client marks its final UDP block with Hamlib's end-of-burst
flag and sends PTT off on the same connection. The server waits briefly for
the final block before requesting a bounded drain. Disconnect, playback
failure, missing end-of-burst, or stalled audio discards pending samples
and releases PTT in the fixture. The physical USB output and the normal
JS8Android transmit controls still need to be connected to this path.

On the Pixel 8a, build `df54398` streamed FX-4CR C-Media audio to the pinned Hamlib `rigstreamtest` client on loopback. During a five-second 48 kHz mono float32 receive test, the client reported 915,396 bytes, nonzero levels and zero gaps, overruns and underruns. A concurrent CAT frequency query returned 7,046,700 Hz. A second client connection opened native PCM16 and reported 255,728 bytes with zero gaps over three seconds. The pinned diagnostic client logs a warning when it parses the extended `stream_caps:` response header and uses float32 to display levels even for PCM16. Measure levels in its default float32 mode. USB reconnect and JS8Android decoding through the stream are still pending.

The owner reported that Robot36 Playback Test (`2af2390`) successfully received FX-4CR audio through rigctld's Playback Capture bridge (`845953a`) on the Pixel 8a. SSTV image decoding was not reported. The test APK was installed alongside the stock Robot36 release.

The owner verified that **GGMorse Rigctld** receives the FX-4CR's network stream on the Pixel 8a. Its signal level followed the rig's line level, and the owner observed apparent CW decoding. A separate software test fed a 700 Hz, 18 WPM synthetic signal through the embedded Hamlib daemon to the same JNI receiver and decoded `CQ CQ DE TEST` after an initial misread letter. The Play Store GGMorse app coexisted with the test app on the Pixel.

On 2026-11-23, **JS8Android Rigctld Test** on the Pixel 8a also received live FX-4CR C-Media USB audio over rigctld while CAT was connected at `localhost:4532`. The client negotiated 48 kHz audio, decimated it to 12 kHz, and logged nonzero PCM levels in the decoder. CAT read 7.049600 MHz, USB, 2,200 Hz passband and PTT off. A live decode with a known JS8 signal is still pending.

## Build and test with Docker

From a Linux host with Docker access, run these commands from the workspace root. The Android image downloads the pinned SDK, NDK and build tools and accepts Google’s Android SDK licenses inside the image.

```sh
bash scripts/docker-build.sh apk
bash scripts/docker-build.sh test
```

The debug APK is `app/build/outputs/apk/debug/app-debug.apk`. It targets ARM64 devices and declares Android 12 (API 31) as its minimum. Device qualification is tracked separately in the [hardware matrix](HANDOFF.md#hardware-and-android-coverage).

To compare the FX-4CR's inputs on Android, close other audio apps, connect the rig by USB, keep CAT PTT off and tune to a readable signal. Tap **Refresh USB audio inputs**, select C-Media (`0d8c:013c`), and tap **Monitor**. Grant the microphone and USB permissions if prompted. Android requires microphone permission for direct access to a USB audio capture device.

The monitor claims the selected USB capture interface for five seconds and displays live peak/RMS levels and a summary of frames, nonzero samples and USB packet errors. It releases the interface when the test ends. Repeat on a quiet frequency to check whether levels respond to reception. Repeat on QCC3024 (`0a12:4007`) for comparison. **Copy diagnostics** includes the most recent result and the detected endpoints. If CAT is running, confirm a frequency read still works during capture. After an unplug/replug, refresh the list and repeat on C-Media. This monitor uses direct USB Audio Class capture for the two FX-4CR interfaces and displays levels locally.

To check the C-Media output with no RF, stop rigctld, select C-Media
(`0d8c:013c`), and tap **Check USB output (silence)**. It checks the
advertised format, then runs a temporary dummy server on phone loopback port
14533. An internal Hamlib client sends one second of silence through the TX
stream, waits for USB completion and checks dummy PTT off. The output accepts
only zero samples. **Stop monitor** cancels the check. Report the network
frame count and any USB packet errors. This verifies the network and USB path
with simulated PTT. Physical TX and modulation are separate hardware checks.

To test shared receive audio on the FX-4CR, stop the short monitor, choose **USB serial** for CAT, and select **C-Media (`0d8c:013c`)**. Enable **Share selected USB audio (receive only)** and tap **Start**. Grant microphone and USB permissions if prompted. When status shows **C-Media USB receive ready**, a local Hamlib streaming client built from the pinned revision can request one 48 kHz mono PCM16 stream at `127.0.0.1:4532`. Confirm sample level changes with the FX-4CR AUX output and check CAT frequency readback on a second connection. On USB disconnect the client must stop using the stream. Reconnect the rig, refresh the ports and inputs, and restart the service. The client can reopen at the same address. JS8Android needs a rigctld audio adapter to consume this stream.

### Robot36 Playback Capture

Put the radio in receive-only mode, then follow the shared receive setup above. Before tapping **Start**, also enable **Play receive audio for capture apps**. Wait for **Receive playback ready · phone speaker**. Rigctld starts with silence, checks the phone speaker route, and stops playback if Android changes it. The network stream can continue independently.

Open a Robot36 build with **Playback Capture** and tap that control to grant Android's capture permission. Keep other media playback off during the test. Check for a live waterfall and try decoding a known SSTV transmission. Set the rig's AUX level for a clear signal. If rigctld reports that the route changed, include its status and `adb shell dumpsys media.audio_policy` output in the report. Robot36's `v2.17` tag uses microphone capture. The separate **Robot36 Playback Test** APK was built from `2af2390954aa248725042ba6b1332bbcca68c357` with SDK and target API 35 and app ID `xdsopl.robot36.rigctldtest`. [Robot36 input modes](https://github.com/xdsopl/robot36#audio-inputs)

### GGMorse over rigctld

Run `bash scripts/docker-build.sh ggmorse` to build the separate **GGMorse Rigctld** app. The APK is `ggmorse-client/build/outputs/apk/debug/ggmorse-client-debug.apk`. The build uses the upstream decoder pinned in `ggmorse-client/upstream-version` and the same Hamlib revision as the daemon.

Start rigctld with **Share selected USB audio (receive only)** enabled. Tune to a readable CW signal at an audio pitch within GGMorse's detection range of 200–1,200 Hz. Open **GGMorse Rigctld**, tap **Connect**, and check for signal level and decoded text. The client opens 48 kHz mono PCM16 at `127.0.0.1:4532` and releases its receive stream on disconnect or when the app leaves the foreground. This is a separate receive-only app built with the public GGMorse library and can coexist with the Play Store app. [GGMorse source](https://github.com/ggerganov/ggmorse)

### (tr)uSDX serial receive audio

Connect the (tr)uSDX by USB and close any app that owns its serial port. Select **USB serial**, Hamlib model **2055**, the rig's serial port, and **115200** baud. Leave **USB RTS/CTS** and **CAT PTT** off. For a first check, start with audio sharing off and read the dial frequency and mode using the CAT commands below. Record the firmware version and compare the replies with the rig's display.

Stop the service, enable **Share (tr)uSDX serial audio (receive only)**, and start again. This option uses the selected CAT port for the serial audio protocol. Wait for **(tr)uSDX serial receive ready**, then connect GGMorse Rigctld to the usual same-phone endpoint. Tune to CW, check for signal levels and decoded text, and read CAT frequency and mode again during audio. Stop and restart once, then unplug/replug and select the port again. If the status reports missing or stalled audio, copy diagnostics and note whether CAT still responds. The first build uses the documented 7,825 Hz receive rate and converts unsigned PCM to the same 48 kHz mono PCM16 stream the FX-4CR uses. The host tests verify framing and sample conversion. The radio's timing, simultaneous CAT/audio and recovery still need hardware verification. [Radio protocol documentation](https://dl2man.de/5-trusdx-details/)

## Verify on a phone

Use ADB and a desktop Hamlib installation on a machine that can reach the phone. Install the APK, open Android rigctld, select **Dummy rig** and tap **Start**. With the screen showing **Listening on 127.0.0.1:4532**, run:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
# Open the app and start the dummy rig after installing.
adb forward tcp:14532 tcp:4532
rigctl -m 2 -r 127.0.0.1:14532 f
rigctl -m 2 -r 127.0.0.1:14532 m
```

Check that frequency and mode are returned. Tap **Stop**, check that `rigctl` can no longer connect, then start again and repeat. If reporting an issue, include the phone and Android version, the status text, the command and output, and whether pressing Stop or restarting changes the result. For an Android client, use NET rigctl at `127.0.0.1:4532`.

In **Ham2K Logger Next (HaLo)**, choose **Radio controlled via rigctld** as the equipment model, set the host to `127.0.0.1` and port to `4532`, and pick that entry as the **Main VFO** in Station settings. Choose the physical radio model in Android rigctld. A client configured for Kenwood sends raw commands such as `FA;` and `IF;`, which the rigctld endpoint cannot interpret.

## Verify CAT with a radio

1. Connect the USB cable or pair the radio in Android’s Bluetooth settings. Select the corresponding transport, tap **Refresh ports**, and select the port. Enter the Hamlib model ID and optionally override its baud rate. USB uses the app’s RTS/CTS checkbox, off by default. Turn it on if your rig and adapter require hardware flow control.
2. Tap **Start** and grant USB access if prompted. Use `rigctl -m 2 -r 127.0.0.1:14532 f`, `m` and `t` to query frequency, mode and PTT through the same ADB forward. Then verify a frequency and mode change. Try stop/restart and disconnect/reconnect for each applicable combination in the [hardware matrix](HANDOFF.md#hardware-and-android-coverage).
3. For an issue, use **Copy diagnostics** and include the shortest reproduction, firmware version and settings. The copy includes the last session’s build, model and transport and omits the selected device path.

Start the hardware matrix with the FX-4CR over USB using BG2FX model **2053**. Leave **Allow CAT PTT** unchecked. Compare the frequency and mode returned by CAT with the rig’s display and include those results and the PTT status when reporting the test.

For wireless ADB, keep the phone’s USB port available for the rig. Before starting USB CAT, disconnect any other app that is using the rig’s CAT port. If **Refresh ports** is empty, `adb shell dumpsys usb` can distinguish an absent USB device from a device that Android sees but the app does not list. USB baud settings take effect after port detection.

The listener binds to loopback. Other Android apps can connect locally. Physical sessions expose common CAT commands and require explicit opt-in to key the radio. With that opt-in, one connection can claim PTT, with release attempted on owner disconnect, a three-minute timeout or service stop. The app reports an unconfirmed release if the CAT link fails. Perform actual keying checks deliberately at low power into a dummy load.

## Implementation and source provenance

The APK links the pinned Hamlib library and upstream rigctld command parser into one JNI library. The app owns the socket and service lifecycle. The host smoke tests use the same server and parser with a real Hamlib `rigctl` client. They check CAT reads and writes, simulated PTT and connection ownership, concurrent clients, stop, restart and port conflicts.

Hamlib is pinned at `af198ace7697c84f5d8ce084271208e383012792`. Builds keep sources and output under `.work/hamlib/<revision>/`. The Android serial patches and USB/Bluetooth bridge code come from JS8Android commit `1abc75d9ca23096ead784f827ae0b87a712f861b`. The embedded parser preserves Hamlib’s protocol and rejects CLI commands that can exit or stall the app process. The audit’s local reference links assume a separate JS8Android checkout in `Android-port/`. See [patches](native/hamlib-patches/) and the [audit](AUDIT.md).

Application source is GPL-3.0-or-later. The APK bundles the relevant Hamlib and GPL license texts. The build script identifies and fetches the exact Hamlib source and applies the included patches. [Project license](LICENSE)

Hardware owners can follow the [contribution workflow](CONTRIBUTING.md) to reproduce, verify and submit fixes.
