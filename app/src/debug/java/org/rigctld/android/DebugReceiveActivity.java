// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.content.Intent;
import android.hardware.usb.UsbManager;
import android.util.Log;
import com.js8call.core.BluetoothSerialPortCatalog;
import com.js8call.core.UsbSerialPortCatalog;

public final class DebugReceiveActivity extends MainActivity {
    private static final String ACTION_START = "org.rigctld.android.DEBUG_START_RX";
    private static StartResult lastStart;
    private String requestId;

    record StartResult(String id, String message, StartProgress progress) {}

    static StartResult lastStart() {
        return lastStart;
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    @Override protected void onPostResume() {
        super.onPostResume();
        Intent request = getIntent();
        if (!ACTION_START.equals(request.getAction())) return;
        setIntent(new Intent());
        String id = request.getStringExtra("request_id");
        if (id == null || id.isEmpty()) return;
        if (requestId != null) {
            lastStart = new StartResult(id, "Finish the current permission prompt first", StartProgress.FAILED);
            return;
        }
        requestId = id;
        try {
            startRequest(request);
        } catch (RuntimeException error) {
            onRadioStartProgress(error.toString(), StartProgress.FAILED);
        }
    }

    @Override void onRadioStartProgress(String message, StartProgress progress) {
        if (requestId == null) return;
        lastStart = new StartResult(requestId, message, progress);
        if (progress != StartProgress.WAITING) requestId = null;
        Log.i("RigctldDebug", "Start receive: " + message);
    }

    private void startRequest(Intent request) {
        String transport = request.getStringExtra("transport");
        int model = request.getIntExtra("model", 1);
        String port = request.getStringExtra("port");
        int baud = request.getIntExtra("baud", 0);
        String audio = request.getStringExtra("audio");
        boolean rtsCts = request.getBooleanExtra("rts_cts", false);
        boolean playback = request.getBooleanExtra("playback_capture", false);
        boolean trusdx = request.getBooleanExtra("trusdx_audio", false);
        boolean usbTx = request.getBooleanExtra("usb_tx", false);
        boolean catPtt = request.getBooleanExtra("cat_ptt", false);
        if (usbTx && catPtt) throw new IllegalArgumentException("Choose USB transmit audio or CAT PTT");
        if (usbTx && (audio == null || playback || trusdx)) {
            throw new IllegalArgumentException("USB transmit audio requires --audio and excludes playback capture");
        }
        if (baud < 0) throw new IllegalArgumentException("Baud must be positive or omitted");
        String rate = baud == 0 ? "" : Integer.toString(baud);

        int transportId;
        int deviceId = -1;
        if ("usb".equals(transport)) {
            transportId = RadioSelection.USB;
            var serial = UsbSerialPortCatalog.INSTANCE.listPorts(this).stream()
                .filter(candidate -> DebugCommandReceiver.usbPortId(candidate).equals(port))
                .findFirst().orElseThrow(() -> new IllegalArgumentException(
                    "Select a current USB serial port from list-devices"));
            deviceId = serial.getDeviceId();
            if (audio != null && !UsbAudioMonitor.supportsRxStream(
                    getSystemService(UsbManager.class).getDeviceList().get(audio))) {
                throw new IllegalArgumentException("Select a USB audio input with can_share_rx");
            }
            if (trusdx && baud == 0) rate = "115200";
        } else {
            if (audio != null || rtsCts || playback || trusdx) {
                throw new IllegalArgumentException("USB options require USB transport");
            }
            if ("bluetooth".equals(transport)) {
                transportId = RadioSelection.BLUETOOTH;
                if (!DebugCommandReceiver.hasBluetoothPermission(this)) {
                    throw new IllegalStateException("Grant Nearby devices access using Refresh ports in the app");
                }
                if (BluetoothSerialPortCatalog.INSTANCE.listPorts(this).stream().noneMatch(
                        candidate -> DebugCommandReceiver.bluetoothPortId(candidate).equals(port))) {
                    throw new IllegalArgumentException("Select a paired Bluetooth serial port from list-devices");
                }
            } else if ("dummy".equals(transport)) {
                transportId = RadioSelection.DUMMY;
                if (baud != 0) throw new IllegalArgumentException("Dummy rig has no serial baud");
            } else {
                throw new IllegalArgumentException("Select dummy, usb or bluetooth transport");
            }
        }
        RadioSelection selection = new RadioSelection(transportId, model, port, rate, rtsCts, usbTx || catPtt,
            audio, playback, trusdx, usbTx);
        if (usbTx || catPtt) startWithConfirmation(selection, deviceId);
        else startReceiveOnly(selection, deviceId);
    }
}
