# Android rigctld

Android rigctld provides a local CAT service over USB serial or classic Bluetooth RFCOMM. Use the Hamlib dummy rig to check the service and protocol without connecting a radio. Shared USB and Bluetooth audio are planned for a later phase.

## Verification status

The owner installed debug build `0.1-dev` on a Pixel 8a running Android 17 (API 37) and confirmed that the Hamlib dummy rig (model 1) listened on `127.0.0.1:4532`.

On the same phone, an FX-4CR connected over USB using the BG2FX backend (model 2053) and the CH340 interface (`1a86:7523`). Desktop `rigctl` over wireless ADB repeatedly returned 14,064,000 Hz, CW, 500 Hz passband and PTT off. This verifies read-only CAT on that combination. USB writes, stop/restart and the other physical configurations still need device test results.

The owner also reported that Ham2K PoLo on the Pixel successfully connected to the same USB CAT session at `localhost:4532`.

The FX-4CR also passed Bluetooth RFCOMM CAT using model 2053. The CAT frequency followed the owner’s dial change from 7,061,900 to 7,058,700 Hz. A direct rigctld `F` command then tuned to 7,058,800 Hz and a second command restored 7,058,700 Hz. Both returned `RPRT 0`, and each frequency was read back after a delay. PTT was off before and after. The rig’s hardware revision has yet to be recorded.

The owner verified spot tuning in Ham2K Logger Next (HaLo) 26.11.0 over the same Bluetooth CAT session after selecting its rigctld equipment model. A temporary CAT trace showed raw Kenwood commands with the original client profile and valid rigctld replies after the change, including a readback of 14,338,000 Hz in USB mode.

The owner reported successful CW decoding in stock GGMorse with the FX-4CR connected over USB to the Pixel 8a, without an audio adapter or an in-app source selector. Android can automatically route capture to a compatible USB audio device. The active recording device was not independently confirmed. [Android USB audio](https://source.android.com/docs/core/audio/usb#host-mode-applications)

On the same USB setup, the owner reported a gray waterfall in Robot36 2.17 (67) with Android rigctld stopped. The symptom persisted after restarting Robot36 and the rig, reconnecting USB, clearing HaLo's USB launch default, and trying 48 kHz, both stereo channels, and Fixed Point. ADB matched Robot36's active, unsilenced recording session to the rig's `USB-Audio - QCC3024` input. To reproduce, keep PTT off and compare Robot36 with a known-working USB audio app using the same received signal, one app at a time. Use `adb shell dumpsys audio` and `adb shell dumpsys media.audio_policy` to confirm each app's route. The captured signal level and cause are still unknown.

JS8Android also showed a blank waterfall on the Pixel. ADB matched its unsilenced capture to the QCC3024, and all 23 logged sample triplets were zero. On a Linux PC the same FX-4CR connection enumerated two audio inputs, the QCC3024 (`0a12:4007`) and the C-Media USB PnP Sound Device (`0d8c:013c`). `arecord -D hw:CARD=QCC3024,DEV=0 -f S16_LE -r 48000 -c 2 -d 4 -t raw /dev/null` failed with an I/O error and zero frames on two attempts. Using `hw:CARD=Device,DEV=0`, `-c 1` and `-d 8` captured 384,000 frames with a peak of 7,187 and RMS of 1,542 in signed 16-bit units.

The owner confirmed that the USB audio diagnostics picker from `5a5cda7` lists both FX-4CR inputs on the Pixel 8a. With the direct USB monitor from `fd0c54a`, the owner reported frames from C-Media, none from QCC3024, and level changes when adjusting the rig's AUX output. Continuous capture with simultaneous CAT, recovery after reconnect, and client decoding through the service are still pending.

## Build and test with Docker

From a Linux host with Docker access, run these commands from the workspace root. The Android image downloads the pinned SDK, NDK and build tools and accepts Google’s Android SDK licenses inside the image.

```sh
bash scripts/docker-build.sh apk
bash scripts/docker-build.sh test
```

The debug APK is `app/build/outputs/apk/debug/app-debug.apk`. It targets ARM64 devices and declares Android 12 (API 31) as its minimum. Device qualification is tracked separately in the [hardware matrix](HANDOFF.md#hardware-and-android-coverage).

To compare the FX-4CR's inputs on Android, close other audio apps, connect the rig by USB, keep CAT PTT off and tune to a readable signal. Tap **Refresh USB audio inputs**, select C-Media (`0d8c:013c`), and tap **Monitor**. Grant the microphone and USB permissions if prompted. Android requires microphone permission for direct access to a USB audio capture device.

The monitor claims the selected USB capture interface for five seconds and displays live peak/RMS levels and a summary of frames, nonzero samples and USB packet errors. It releases the interface when the test ends. Repeat on a quiet frequency to check whether levels respond to reception. Repeat on QCC3024 (`0a12:4007`) for comparison. **Copy diagnostics** includes the most recent result and the detected endpoints. If CAT is running, confirm a frequency read still works during capture. After an unplug/replug, refresh the list and repeat on C-Media. This monitor uses direct USB Audio Class capture for the two FX-4CR interfaces and displays levels locally. Shared PCM streaming is pending.

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

Hamlib is pinned at `1b2461aba99f0dde0531e1732c00e60aeee40657`. The Android serial patches and USB/Bluetooth bridge code come from JS8Android commit `1abc75d9ca23096ead784f827ae0b87a712f861b`. The embedded parser preserves Hamlib’s protocol and rejects CLI commands that can exit or stall the app process. The audit’s local reference links assume a separate JS8Android checkout in `Android-port/`. See [patches](native/hamlib-patches/) and the [audit](AUDIT.md).

Application source is GPL-3.0-or-later. The APK bundles the relevant Hamlib and GPL license texts. The build script identifies and fetches the exact Hamlib source and applies the included patches. [Project license](LICENSE)

Hardware owners can follow the [contribution workflow](CONTRIBUTING.md) to reproduce, verify and submit fixes.
