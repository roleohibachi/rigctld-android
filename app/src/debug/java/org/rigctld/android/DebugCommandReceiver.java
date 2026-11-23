// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.Manifest;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbManager;
import android.os.Build;
import com.js8call.core.BluetoothSerialPortCatalog;
import com.js8call.core.UsbSerialPortCatalog;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

public final class DebugCommandReceiver extends BroadcastReceiver {
    static final String ACTION = "org.rigctld.android.DEBUG_COMMAND";

    @Override public void onReceive(Context context, Intent intent) {
        if (!ACTION.equals(intent.getAction()) || !isOrderedBroadcast()) return;
        String command = intent.getStringExtra("command");
        try {
            JSONObject result;
            if ("status".equals(command)) {
                result = status();
            } else if ("list-devices".equals(command)) {
                result = devices(context);
            } else if ("trace-on".equals(command) || "trace-off".equals(command)) {
                System.loadLibrary("android_rigctld");
                boolean enabled = "trace-on".equals(command);
                CatService.nativeDebugTrace(enabled);
                result = new JSONObject().put("hamlib_trace", enabled);
            } else if ("stop".equals(command)) {
                boolean active = CatService.status().active;
                if (active) {
                    context.startService(new Intent(context, CatService.class).setAction(CatService.ACTION_STOP));
                }
                result = new JSONObject().put("stop_requested", active);
            } else {
                setResult(1, "Unknown debug command", null);
                return;
            }
            setResult(0, result.toString(), null);
        } catch (JSONException | RuntimeException error) {
            setResult(1, error.toString(), null);
        }
    }

    private static JSONObject status() throws JSONException {
        CatService.Status state = CatService.status();
        RadioSelection radio = state.radio;
        JSONObject configuration = null;
        if (radio != null) {
            configuration = new JSONObject()
                .put("transport", switch (radio.transport) {
                    case RadioSelection.USB -> "usb";
                    case RadioSelection.BLUETOOTH -> "bluetooth";
                    default -> "dummy";
                })
                .put("model", radio.model)
                .put("serial_port", nullable(radio.path))
                .put("baud", nullable(radio.baud))
                .put("rts_cts", radio.rtsCts)
                .put("cat_ptt_enabled", radio.allowPtt)
                .put("usb_audio_device", nullable(radio.usbAudioPath))
                .put("trusdx_audio", radio.trusdxAudio)
                .put("bluetooth_audio", radio.bluetoothAudio)
                .put("playback_capture", radio.receivePlayback)
                .put("usb_tx_audio_enabled", radio.usbTx);
        }
        var lastStart = DebugReceiveActivity.lastStart();
        JSONObject request = lastStart == null ? null : new JSONObject()
            .put("id", lastStart.id())
            .put("message", lastStart.message())
            .put("state", lastStart.progress().name().toLowerCase(Locale.ROOT));
        return new JSONObject()
            .put("app_version", BuildConfig.VERSION_NAME)
            .put("hamlib_revision", BuildConfig.HAMLIB_REVISION)
            .put("phone", Build.MODEL)
            .put("android_api", Build.VERSION.SDK_INT)
            .put("endpoint", CatService.ENDPOINT)
            .put("active", state.active)
            .put("listening", state.listening)
            .put("message", state.message)
            .put("receive_audio", nullable(state.receiveAudio))
            .put("receive_playback", nullable(state.receivePlayback))
            .put("transmit_audio", nullable(state.transmitAudio))
            .put("start_rx_request", request == null ? JSONObject.NULL : request)
            .put("radio", configuration == null ? JSONObject.NULL : configuration);
    }

    private static Object nullable(String value) {
        return value == null ? JSONObject.NULL : value;
    }

    static String usbPortId(UsbSerialPortCatalog.SerialPort port) {
        return "android-usb:" + port.getDeviceId() + ":" + port.getPortIndex();
    }

    static String bluetoothPortId(BluetoothSerialPortCatalog.SerialPort port) {
        return "android-bt:" + port.getAddress() + ":" + port.getPortIndex();
    }

    static boolean hasBluetoothPermission(Context context) {
        return context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
            == PackageManager.PERMISSION_GRANTED
            && context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
            == PackageManager.PERMISSION_GRANTED;
    }

    private static JSONObject devices(Context context) throws JSONException {
        UsbManager manager = context.getSystemService(UsbManager.class);
        JSONArray usbSerial = new JSONArray();
        for (var port : UsbSerialPortCatalog.INSTANCE.listPorts(context)) {
            usbSerial.put(new JSONObject().put("id", usbPortId(port)).put("label", port.getLabel()));
        }
        JSONArray usbAudio = new JSONArray();
        for (var input : UsbAudioInputs.list(manager)) {
            usbAudio.put(new JSONObject().put("id", input.deviceName()).put("label", input.label())
                .put("endpoints", new JSONArray(input.endpoints()))
                .put("can_share_rx", UsbAudioMonitor.supportsRxStream(manager.getDeviceList().get(input.deviceName()))));
        }
        boolean bluetoothPermission = hasBluetoothPermission(context);
        JSONArray bluetooth = new JSONArray();
        if (bluetoothPermission) {
            for (var port : BluetoothSerialPortCatalog.INSTANCE.listPorts(context)) {
                bluetooth.put(new JSONObject().put("id", bluetoothPortId(port)).put("label", port.getLabel()));
            }
        }
        return new JSONObject().put("usb_serial", usbSerial).put("usb_audio", usbAudio)
            .put("bluetooth_serial", bluetooth).put("bluetooth_permission", bluetoothPermission);
    }
}
