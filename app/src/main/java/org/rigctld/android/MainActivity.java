// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import com.js8call.core.BluetoothSerialPortCatalog;
import com.js8call.core.UsbSerialPortCatalog;

public final class MainActivity extends Activity {
    private static final String USB_PERMISSION = "org.rigctld.android.USB_PERMISSION";
    private static final String USB_AUDIO_PERMISSION = "org.rigctld.android.USB_AUDIO_PERMISSION";
    private static final int RECORD_AUDIO_REQUEST = 2;
    private static final String[] BLUETOOTH_PERMISSIONS = {
        Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN
    };
    private TextView status;
    private TextView selectionMessage;
    private Button start;
    private Button stop;
    private Button refresh;
    private Spinner transport;
    private Spinner ports;
    private Spinner usbAudioInputs;
    private TextView usbAudioDetails;
    private TextView usbAudioStatus;
    private Button monitorAudio;
    private Button refreshAudio;
    private CheckBox shareAudio;
    private CheckBox receivePlayback;
    private UsbDevice pendingAudioDevice;
    private RadioSelection pendingAudioRadio;
    private AtomicBoolean monitorCancelled;
    private String audioMonitorReport = "";
    private EditText rigModel;
    private EditText baud;
    private CheckBox rtsCts;
    private CheckBox allowPtt;
    private LinearLayout radioSettings;
    private RadioSelection awaitingUsbPermission;

    private static final class Port {
        final String label;
        final String path;
        final int deviceId;

        Port(String label, String path, int deviceId) {
            this.label = label;
            this.path = path;
            this.deviceId = deviceId;
        }

        @Override public String toString() {
            return label;
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (USB_PERMISSION.equals(intent.getAction())) {
                onUsbPermission();
            } else if (USB_AUDIO_PERMISSION.equals(intent.getAction())) {
                onUsbAudioPermission();
            } else {
                render();
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(24 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText("Android rigctld");
        title.setTextSize(24);
        content.addView(title);
        TextView rig = new TextView(this);
        rig.setText(CatService.ENDPOINT);
        content.addView(rig);
        transport = new Spinner(this);
        transport.setContentDescription("CAT transport");
        transport.setAdapter(adapter(List.of("Dummy rig", "USB serial", "Bluetooth serial")));
        content.addView(transport);
        radioSettings = new LinearLayout(this);
        radioSettings.setOrientation(LinearLayout.VERTICAL);
        content.addView(radioSettings);
        rigModel = numberInput("Hamlib model ID (e.g. 2053)", "2053");
        baud = numberInput("Serial baud (blank uses Hamlib default)", "");
        ports = new Spinner(this);
        ports.setContentDescription("Serial port");
        radioSettings.addView(ports);
        refresh = new Button(this);
        refresh.setText("Refresh ports");
        refresh.setOnClickListener(view -> {
            if (transport.getSelectedItemPosition() == RadioSelection.BLUETOOTH && !hasBluetoothPermission()) {
                requestPermissions(BLUETOOTH_PERMISSIONS, 1);
            } else {
                refreshPorts();
            }
        });
        radioSettings.addView(refresh);
        rtsCts = new CheckBox(this);
        rtsCts.setText("USB RTS/CTS flow control");
        radioSettings.addView(rtsCts);
        allowPtt = new CheckBox(this);
        allowPtt.setText("Allow CAT PTT for this session");
        radioSettings.addView(allowPtt);
        selectionMessage = new TextView(this);
        radioSettings.addView(selectionMessage);
        status = new TextView(this);
        status.setTextIsSelectable(true);
        content.addView(status);
        start = new Button(this);
        start.setText("Start");
        start.setOnClickListener(view -> selectRadio());
        content.addView(start);
        stop = new Button(this);
        stop.setText("Stop");
        stop.setOnClickListener(view -> {
            stop.setEnabled(false);
            startService(new Intent(this, CatService.class).setAction(CatService.ACTION_STOP));
        });
        content.addView(stop);
        Button diagnostics = new Button(this);
        diagnostics.setText("Copy diagnostics");
        diagnostics.setOnClickListener(view -> copyDiagnostics());
        content.addView(diagnostics);
        addUsbAudioPicker(content);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        setContentView(scroll);
        transport.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long itemId) {
                refreshPorts();
            }

            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        refreshPorts();
    }

    @Override
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter(CatService.ACTION_STATUS);
        filter.addAction(USB_PERMISSION);
        filter.addAction(USB_AUDIO_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(receiver, filter);
        }
        render();
        refreshUsbAudioInputs();
    }

    @Override
    protected void onStop() {
        if (monitorCancelled != null) monitorCancelled.set(true);
        unregisterReceiver(receiver);
        super.onStop();
    }

    private void render() {
        CatService.Status state = CatService.status();
        status.setText(state.message);
        boolean ready = !state.active && awaitingUsbPermission == null
            && pendingAudioDevice == null && monitorCancelled == null;
        start.setEnabled(ready);
        stop.setEnabled(state.active);
        transport.setEnabled(ready);
        ports.setEnabled(ready);
        refresh.setEnabled(ready);
        allowPtt.setEnabled(ready && (transport.getSelectedItemPosition() != RadioSelection.USB || !shareAudio.isChecked()));
        rtsCts.setEnabled(ready);
        rigModel.setEnabled(ready);
        baud.setEnabled(ready);
        renderAudioMonitor();
    }

    private <T> ArrayAdapter<T> adapter(List<T> items) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        return adapter;
    }

    private void addUsbAudioPicker(LinearLayout content) {
        TextView label = new TextView(this);
        label.setText("USB audio input diagnostics");
        content.addView(label);
        usbAudioInputs = new Spinner(this);
        usbAudioInputs.setContentDescription("USB audio input to inspect");
        content.addView(usbAudioInputs);
        usbAudioDetails = new TextView(this);
        content.addView(usbAudioDetails);
        usbAudioInputs.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long itemId) {
                UsbAudioInputs.Input input = (UsbAudioInputs.Input) parent.getItemAtPosition(position);
                usbAudioDetails.setText(String.join("\n", input.endpoints()));
                renderAudioMonitor();
            }

            @Override public void onNothingSelected(AdapterView<?> parent) {
                usbAudioDetails.setText("Connect a USB audio input, then refresh.");
                renderAudioMonitor();
            }
        });
        refreshAudio = new Button(this);
        refreshAudio.setText("Refresh USB audio inputs");
        refreshAudio.setOnClickListener(view -> refreshUsbAudioInputs());
        content.addView(refreshAudio);
        shareAudio = new CheckBox(this);
        shareAudio.setText("Share selected USB audio (receive only)");
        shareAudio.setOnCheckedChangeListener((button, enabled) -> {
            if (enabled) allowPtt.setChecked(false);
            render();
        });
        content.addView(shareAudio);
        receivePlayback = new CheckBox(this);
        receivePlayback.setText("Play receive audio for capture apps");
        content.addView(receivePlayback);
        monitorAudio = new Button(this);
        monitorAudio.setText("Monitor");
        monitorAudio.setOnClickListener(view -> {
            if (monitorCancelled != null) {
                monitorCancelled.set(true);
                monitorAudio.setEnabled(false);
            } else {
                requestAudioMonitor();
            }
        });
        content.addView(monitorAudio);
        usbAudioStatus = new TextView(this);
        usbAudioStatus.setTextIsSelectable(true);
        content.addView(usbAudioStatus);
    }

    private void refreshUsbAudioInputs() {
        if (pendingAudioDevice != null || monitorCancelled != null) return;
        UsbAudioInputs.Input previous = (UsbAudioInputs.Input) usbAudioInputs.getSelectedItem();
        List<UsbAudioInputs.Input> inputs = UsbAudioInputs.list(getSystemService(UsbManager.class));
        usbAudioInputs.setAdapter(adapter(inputs));
        if (inputs.isEmpty()) usbAudioDetails.setText("Connect a USB audio input, then refresh.");
        if (previous != null) {
            for (int i = 0; i < inputs.size(); i++) {
                if (inputs.get(i).deviceName().equals(previous.deviceName())) {
                    usbAudioInputs.setSelection(i);
                    break;
                }
            }
        }
        renderAudioMonitor();
    }

    private UsbDevice selectedAudioDevice() {
        UsbAudioInputs.Input input = (UsbAudioInputs.Input) usbAudioInputs.getSelectedItem();
        return input == null ? null : getSystemService(UsbManager.class).getDeviceList().get(input.deviceName());
    }

    private void renderAudioMonitor() {
        CatService.Status state = CatService.status();
        boolean sharing = state.active && state.radio != null && state.radio.usbAudioPath != null;
        boolean running = monitorCancelled != null;
        boolean available = !running && !sharing && pendingAudioDevice == null;
        usbAudioInputs.setEnabled(available && usbAudioInputs.getCount() > 0);
        refreshAudio.setEnabled(available);
        monitorAudio.setText(running ? "Stop monitor" : "Monitor");
        monitorAudio.setEnabled(running ? !monitorCancelled.get()
            : available && UsbAudioMonitor.supports(selectedAudioDevice()));
        boolean usbSelected = transport.getSelectedItemPosition() == RadioSelection.USB;
        shareAudio.setVisibility(usbSelected ? View.VISIBLE : View.GONE);
        shareAudio.setEnabled(available && !state.active && awaitingUsbPermission == null);
        receivePlayback.setVisibility(usbSelected && shareAudio.isChecked() ? View.VISIBLE : View.GONE);
        receivePlayback.setEnabled(shareAudio.isEnabled());
    }

    private void requestAudioMonitor() {
        UsbDevice device = selectedAudioDevice();
        if (!UsbAudioMonitor.supports(device)) {
            usbAudioStatus.setText("Selected USB input is unavailable. Refresh USB audio inputs.");
            renderAudioMonitor();
            return;
        }
        requestAudioPermissions(device, null);
    }

    private void requestAudioPermissions(UsbDevice device, RadioSelection radio) {
        pendingAudioDevice = device;
        pendingAudioRadio = radio;
        render();
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, RECORD_AUDIO_REQUEST);
        } else {
            requestUsbAudioPermission();
        }
    }

    private void requestUsbAudioPermission() {
        if (pendingAudioDevice == null) return;
        UsbManager manager = getSystemService(UsbManager.class);
        UsbDevice device = manager.getDeviceList().get(pendingAudioDevice.getDeviceName());
        if (device == null || device.getDeviceId() != pendingAudioDevice.getDeviceId()) {
            finishAudioPermission("USB input disconnected. Refresh USB audio inputs.");
        } else if (manager.hasPermission(device)) {
            onUsbAudioPermission();
        } else {
            PendingIntent result = PendingIntent.getBroadcast(this, 1,
                new Intent(USB_AUDIO_PERMISSION).setPackage(getPackageName()),
                PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_MUTABLE);
            manager.requestPermission(device, result);
        }
    }

    private void finishAudioPermission(String message) {
        pendingAudioDevice = null;
        pendingAudioRadio = null;
        usbAudioStatus.setText(message);
        render();
    }

    private void onUsbAudioPermission() {
        if (pendingAudioDevice == null) return;
        UsbManager manager = getSystemService(UsbManager.class);
        UsbDevice device = pendingAudioDevice;
        if (!manager.hasPermission(device)) {
            finishAudioPermission("USB audio permission was not granted.");
            return;
        }
        RadioSelection radio = pendingAudioRadio;
        pendingAudioDevice = null;
        pendingAudioRadio = null;
        if (radio != null) {
            startServiceForRadio(radio);
            return;
        }
        UsbAudioInputs.Input input = (UsbAudioInputs.Input) usbAudioInputs.getSelectedItem();
        String label = input.label();
        AtomicBoolean cancelled = new AtomicBoolean();
        monitorCancelled = cancelled;
        audioMonitorReport = "";
        usbAudioStatus.setText(label + "\nOpening USB input…");
        render();
        new Thread(() -> {
            String outcome;
            try {
                outcome = UsbAudioMonitor.measure(manager, device, cancelled, progress -> runOnUiThread(() -> {
                    if (!isDestroyed() && monitorCancelled == cancelled && !cancelled.get()) {
                        usbAudioStatus.setText(label + "\n" + progress);
                    }
                }));
            } catch (RuntimeException | LinkageError failure) {
                outcome = "USB monitor failed: " + failure.getClass().getSimpleName() + ": " + failure.getMessage();
            }
            String report = label + "\n" + outcome;
            runOnUiThread(() -> {
                if (isDestroyed() || monitorCancelled != cancelled) return;
                monitorCancelled = null;
                audioMonitorReport = report;
                usbAudioStatus.setText(report);
                refreshUsbAudioInputs();
                render();
            });
        }, "usb-audio-monitor").start();
    }

    private EditText numberInput(String label, String value) {
        TextView title = new TextView(this);
        title.setText(label);
        EditText input = new EditText(this);
        input.setId(View.generateViewId());
        title.setLabelFor(input.getId());
        input.setInputType(InputType.TYPE_CLASS_NUMBER);
        input.setText(value);
        radioSettings.addView(title);
        radioSettings.addView(input);
        return input;
    }

    private boolean hasBluetoothPermission() {
        return checkSelfPermission(BLUETOOTH_PERMISSIONS[0]) == PackageManager.PERMISSION_GRANTED
            && checkSelfPermission(BLUETOOTH_PERMISSIONS[1]) == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == 1) refreshPorts();
        if (requestCode == RECORD_AUDIO_REQUEST) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                requestUsbAudioPermission();
            } else {
                finishAudioPermission("Microphone permission is required for USB audio capture.");
            }
        }
    }

    private void refreshPorts() {
        int choice = transport.getSelectedItemPosition();
        radioSettings.setVisibility(choice == RadioSelection.DUMMY ? View.GONE : View.VISIBLE);
        rtsCts.setVisibility(choice == RadioSelection.USB ? View.VISIBLE : View.GONE);
        List<Port> available = new ArrayList<>();
        String message = "";
        if (choice == RadioSelection.USB) {
            for (UsbSerialPortCatalog.SerialPort port : UsbSerialPortCatalog.INSTANCE.listPorts(this)) {
                available.add(new Port(port.getLabel(),
                    "android-usb:" + port.getDeviceId() + ":" + port.getPortIndex(), port.getDeviceId()));
            }
            if (available.isEmpty()) message = "Connect the radio by USB, then refresh ports.";
        } else if (choice == RadioSelection.BLUETOOTH) {
            if (hasBluetoothPermission()) {
                for (BluetoothSerialPortCatalog.SerialPort port : BluetoothSerialPortCatalog.INSTANCE.listPorts(this)) {
                    available.add(new Port(port.getLabel(),
                        "android-bt:" + port.getAddress() + ":" + port.getPortIndex(), -1));
                }
                if (available.isEmpty()) message = "Pair the radio in Android settings, then refresh ports.";
            } else {
                message = "Select Refresh ports to grant Nearby devices access.";
            }
        }
        ports.setAdapter(adapter(available));
        selectionMessage.setText(message);
        render();
    }

    private void selectRadio() {
        int choice = transport.getSelectedItemPosition();
        if (choice == RadioSelection.DUMMY) {
            start(new RadioSelection(choice, 1, null, "", false, false));
            return;
        }
        Port port = (Port) ports.getSelectedItem();
        if (port == null) {
            selectionMessage.setText("Select a serial port, or refresh ports.");
            return;
        }
        int model;
        try {
            model = Integer.parseInt(rigModel.getText().toString());
            if (model < 2) throw new NumberFormatException();
        } catch (NumberFormatException invalidModel) {
            rigModel.setError("Enter a Hamlib radio model ID");
            return;
        }
        String rate = baud.getText().toString();
        if (!rate.isEmpty()) {
            try {
                if (Integer.parseInt(rate) <= 0) throw new NumberFormatException();
            } catch (NumberFormatException invalidBaud) {
                baud.setError("Enter a positive serial baud rate");
                return;
            }
        }
        String audioPath = null;
        if (choice == RadioSelection.USB && shareAudio.isChecked()) {
            UsbDevice audio = selectedAudioDevice();
            if (!UsbAudioMonitor.supportsRxStream(audio)) {
                selectionMessage.setText("Select the C-Media (0d8c:013c) USB input to share receive audio.");
                return;
            }
            audioPath = audio.getDeviceName();
        }
        RadioSelection selection = new RadioSelection(choice, model, port.path, rate,
            choice == RadioSelection.USB && rtsCts.isChecked(), audioPath == null && allowPtt.isChecked(),
            audioPath, audioPath != null && receivePlayback.isChecked());
        if (selection.allowPtt) {
            new AlertDialog.Builder(this).setTitle("Allow PTT for this session?")
                .setMessage("Local rigctld clients can key the radio. For testing, connect a dummy load and select low power.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Allow PTT", (dialog, button) -> startWithPermission(selection, port.deviceId))
                .show();
        } else {
            startWithPermission(selection, port.deviceId);
        }
    }

    private void startWithPermission(RadioSelection selection, int deviceId) {
        if (selection.transport != RadioSelection.USB) {
            start(selection);
            return;
        }
        UsbManager manager = getSystemService(UsbManager.class);
        UsbDevice selected = manager.getDeviceList().values().stream()
            .filter(device -> device.getDeviceId() == deviceId).findFirst().orElse(null);
        if (selected == null) {
            selectionMessage.setText("USB device disconnected. Refresh ports.");
        } else if (manager.hasPermission(selected)) {
            start(selection);
        } else {
            awaitingUsbPermission = selection;
            render();
            PendingIntent result = PendingIntent.getBroadcast(this, 0,
                new Intent(USB_PERMISSION).setPackage(getPackageName()),
                PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_MUTABLE);
            manager.requestPermission(selected, result);
        }
    }

    private void onUsbPermission() {
        RadioSelection selection = awaitingUsbPermission;
        awaitingUsbPermission = null;
        if (selection == null) return;
        UsbManager manager = getSystemService(UsbManager.class);
        for (UsbDevice device : manager.getDeviceList().values()) {
            if (selection.path.startsWith("android-usb:" + device.getDeviceId() + ":") && manager.hasPermission(device)) {
                start(selection);
                return;
            }
        }
        selectionMessage.setText("USB permission was not granted. Select Start to retry.");
        render();
    }

    private void start(RadioSelection selection) {
        if (selection.usbAudioPath != null) {
            UsbDevice audio = getSystemService(UsbManager.class).getDeviceList().get(selection.usbAudioPath);
            if (!UsbAudioMonitor.supportsRxStream(audio)) {
                selectionMessage.setText("USB audio disconnected. Refresh USB audio inputs.");
                render();
                return;
            }
            requestAudioPermissions(audio, selection);
            return;
        }
        startServiceForRadio(selection);
    }

    private void startServiceForRadio(RadioSelection selection) {
        start.setEnabled(false);
        Intent service = new Intent(this, CatService.class);
        selection.writeTo(service);
        startForegroundService(service);
    }

    private void copyDiagnostics() {
        CatService.Status state = CatService.status();
        RadioSelection radio = state.radio;
        String report = "Android rigctld " + BuildConfig.VERSION_NAME + "\n"
            + "Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ") · " + Build.MODEL + "\n"
            + "Hamlib " + BuildConfig.HAMLIB_REVISION + "\n";
        if (radio != null) {
            report += "Transport " + transport.getAdapter().getItem(radio.transport) + "\n"
                + "Hamlib model " + radio.model + "\n"
                + "Baud " + (radio.baud == null || radio.baud.isEmpty() ? "Hamlib default" : radio.baud)
                + " · USB RTS/CTS " + radio.rtsCts + "\n"
                + "CAT PTT enabled " + radio.allowPtt + "\n";
        }
        StringBuilder diagnostics = new StringBuilder(report).append(state.message).append("\nUSB audio inputs:");
        List<UsbAudioInputs.Input> inputs = UsbAudioInputs.list(getSystemService(UsbManager.class));
        if (inputs.isEmpty()) diagnostics.append(" none");
        for (UsbAudioInputs.Input input : inputs) {
            diagnostics.append('\n').append(input.label()).append(" · ")
                .append(String.join("; ", input.endpoints()));
        }
        if (!audioMonitorReport.isEmpty()) diagnostics.append("\nUSB audio monitor:\n").append(audioMonitorReport);
        getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("rigctld diagnostics", diagnostics));
    }
}
