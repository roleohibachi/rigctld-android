// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import com.js8call.core.BluetoothSerialBridge;
import com.js8call.core.UsbSerialBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

public final class CatService extends Service {
    static final String ACTION_STATUS = "org.rigctld.android.STATUS";
    static final String ACTION_STOP = "org.rigctld.android.STOP";
    static final String ENDPOINT = "127.0.0.1:4532";
    private static final String CHANNEL = "cat";
    private static final int NOTIFICATION = 1;

    private static volatile Status status = new Status(false, "Stopped", null);
    private final Handler main = new Handler(Looper.getMainLooper());
    private long server;
    private boolean stopping;
    private boolean listening;
    private String audioStatus;
    private String playbackStatus;
    private String transmitStatus;
    private UsbSerialBridge usb;
    private BluetoothSerialBridge bluetooth;
    private RadioSelection selection;
    private boolean watcherRegistered;
    private String stoppedReason;

    private final BroadcastReceiver detachWatcher = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (selection == null) return;
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (device != null && (selection.path.startsWith("android-usb:" + device.getDeviceId() + ":")
                    || (selection.usbTx && device.getDeviceName().equals(selection.usbAudioPath)))) {
                    stoppedReason = "USB device disconnected";
                    requestStop();
                }
            } else if (BluetoothDevice.ACTION_ACL_DISCONNECTED.equals(intent.getAction())) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (device != null && selection.path.regionMatches(true, 11,
                    device.getAddress().replace(":", ""), 0, 12)) {
                    stoppedReason = "Bluetooth device disconnected";
                    requestStop();
                }
            }
        }
    };

    static final class Status {
        final boolean active;
        final String message;
        final RadioSelection radio;

        Status(boolean active, String message, RadioSelection radio) {
            this.active = active;
            this.message = message;
            this.radio = radio;
        }
    }

    static Status status() {
        return status;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            requestStop();
            return START_NOT_STICKY;
        }
        if (server != 0 || intent == null) return START_NOT_STICKY;
        selection = RadioSelection.readFrom(intent);
        boolean valid = selection.valid();
        int foregroundType = valid && selection.transport != RadioSelection.DUMMY
            ? ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            : (Build.VERSION.SDK_INT >= 34 ? ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE : 0);
        if (valid && selection.receivePlayback) foregroundType |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK;
        try {
            startForeground(NOTIFICATION, notification("Opening CAT service"), foregroundType);
            if (!valid) {
                finish("Invalid rig or port");
                return START_NOT_STICKY;
            }
            publish(true, "Opening CAT service");
            System.loadLibrary("android_rigctld");
            registerTransport();
            server = nativeCreate();
        } catch (RuntimeException | LinkageError error) {
            finish("Could not start CAT: " + error.getMessage());
            return START_NOT_STICKY;
        }
        if (server == 0) {
            finish("Could not allocate the CAT server");
            return START_NOT_STICKY;
        }
        final long runningServer = server;
        RadioSelection activeSelection = selection;
        new Thread(() -> runServer(runningServer, activeSelection), "rigctld").start();
        return START_NOT_STICKY;
    }

    private void registerTransport() {
        String detachAction;
        if (selection.transport == RadioSelection.USB) {
            usb = new UsbSerialBridge(this, selection.rtsCts);
            usb.registerNative();
            detachAction = UsbManager.ACTION_USB_DEVICE_DETACHED;
        } else if (selection.transport == RadioSelection.BLUETOOTH) {
            bluetooth = new BluetoothSerialBridge(this);
            bluetooth.registerNative();
            detachAction = BluetoothDevice.ACTION_ACL_DISCONNECTED;
        } else {
            return;
        }
        IntentFilter filter = new IntentFilter(detachAction);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(detachWatcher, filter, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(detachWatcher, filter);
        }
        watcherRegistered = true;
    }

    private void runServer(long runningServer, RadioSelection radio) {
        AtomicBoolean cancelAudio = new AtomicBoolean();
        AtomicReference<String> outputFailure = new AtomicReference<>();
        Thread capture = null;
        Thread transmit = null;
        long output = 0;
        String error;
        try {
            if (radio.usbTx) {
                output = UsbAudioMonitor.createOutput();
                if (output == 0) throw new IllegalStateException("Could not allocate USB transmit output");
                long activeOutput = output;
                transmit = new Thread(() -> runUsbOutput(runningServer, radio, activeOutput,
                    cancelAudio, outputFailure), "rigctld-usb-transmit");
                transmit.start();
            }
            if (radio.usbAudioPath != null) {
                capture = new Thread(() -> runUsbAudio(runningServer, radio, cancelAudio), "rigctld-usb-audio");
                capture.start();
            }
            error = nativeRun(runningServer, radio.model, radio.path, radio.baud,
                radio.rtsCts ? "Hardware" : "None", radio.allowPtt, radio.usbAudioPath != null,
                radio.trusdxAudio, output);
        } catch (RuntimeException | LinkageError failure) {
            error = "CAT server failed: " + failure.getMessage();
        } finally {
            cancelAudio.set(true);
            joinAudio(capture);
            joinAudio(transmit);
            if (output != 0) UsbAudioMonitor.destroyOutput(output);
        }
        final String result = error == null ? outputFailure.get() : error;
        main.post(() -> finish(result));
    }

    private static void joinAudio(Thread thread) {
        if (thread == null) return;
        // USB workers use bounded polls. Keep their native handles valid
        // until they return, including during an interrupted shutdown.
        boolean interrupted = false;
        while (thread.isAlive()) {
            try {
                thread.join();
            } catch (InterruptedException ignored) {
                interrupted = true;
            }
        }
        if (interrupted) Thread.currentThread().interrupt();
    }

    private void runUsbOutput(long runningServer, RadioSelection radio, long output,
                              AtomicBoolean cancelled, AtomicReference<String> failure) {
        String outcome;
        try {
            UsbManager manager = getSystemService(UsbManager.class);
            UsbDevice device = manager.getDeviceList().get(radio.usbAudioPath);
            outcome = UsbAudioMonitor.output(manager, device, cancelled, message -> main.post(() -> {
                if (server != runningServer || stopping) return;
                transmitStatus = message;
                if (listening) publishListening();
            }), output);
        } catch (RuntimeException | LinkageError error) {
            outcome = "USB transmit output failed: " + error.getMessage();
        }
        if (!cancelled.get()) {
            failure.set(outcome == null ? "USB transmit output ended" : outcome);
            nativeStop(runningServer);
        }
    }

    private void runUsbAudio(long runningServer, RadioSelection radio, AtomicBoolean cancelled) {
        String outcome;
        try {
            UsbManager manager = getSystemService(UsbManager.class);
            UsbDevice device = manager.getDeviceList().get(radio.usbAudioPath);
            if (!UsbAudioMonitor.supportsRxStream(device) || !manager.hasPermission(device)) {
                outcome = "Selected USB audio input unavailable";
            } else {
                Consumer<String> ready = message -> postAudioStatus(runningServer, message, false);
                try (ReceivePlayback playback = radio.receivePlayback ? openPlayback(runningServer) : null) {
                    outcome = UsbAudioMonitor.capture(manager, device, cancelled, ready, runningServer, playback);
                }
            }
        } catch (RuntimeException | LinkageError failure) {
            outcome = "USB audio failed: " + failure.getMessage();
        }
        nativeAudioFailed(runningServer);
        if (!cancelled.get()) {
            postAudioStatus(runningServer, outcome, false);
            if (radio.receivePlayback) postAudioStatus(runningServer, "Receive playback stopped · USB audio ended", true);
        }
    }

    private ReceivePlayback openPlayback(long runningServer) {
        Consumer<String> update = message -> postAudioStatus(runningServer, message, true);
        try {
            return new ReceivePlayback(getSystemService(AudioManager.class), main, update);
        } catch (RuntimeException failure) {
            update.accept("Receive playback unavailable · " + failure.getMessage());
            return null;
        }
    }

    private void postAudioStatus(long runningServer, String message, boolean playback) {
        main.post(() -> {
            if (server != runningServer || stopping) return;
            if (playback) playbackStatus = message;
            else audioStatus = message;
            if (listening) publishListening();
        });
    }

    private void onNativeListening(boolean serialAudioStarted) {
        main.post(() -> {
            if (!stopping && server != 0) {
                listening = true;
                if (selection.trusdxAudio) {
                    audioStatus = serialAudioStarted ? "Waiting for (tr)uSDX serial audio" : "Could not enable (tr)uSDX serial audio";
                    if (serialAudioStarted) watchTruSdxAudio(server);
                }
                publishListening();
            }
        });
    }

    private void watchTruSdxAudio(long activeServer) {
        main.postDelayed(new Runnable() {
            private long lastSampleCount;
            private int idleChecks;

            @Override public void run() {
                if (server != activeServer || stopping) return;
                long count = nativeTruSdxSamples();
                String nextStatus = audioStatus;
                if (count != lastSampleCount) {
                    lastSampleCount = count;
                    idleChecks = 0;
                    nextStatus = "(tr)uSDX serial receive ready · 48 kHz mono PCM16";
                } else if (++idleChecks == 6) {
                    nativeAudioFailed(activeServer);
                    nextStatus = lastSampleCount == 0 ? "No (tr)uSDX serial audio received" : "(tr)uSDX serial audio stalled";
                }
                if (!nextStatus.equals(audioStatus)) {
                    audioStatus = nextStatus;
                    publishListening();
                }
                main.postDelayed(this, 500);
            }
        }, 500);
    }

    private void publishListening() {
        String message = "Listening on " + ENDPOINT;
        if (selection.usbAudioPath != null) {
            message += "\n" + (audioStatus == null ? "Opening C-Media USB receive audio" : audioStatus);
        }
        if (selection.trusdxAudio) message += "\n" + audioStatus;
        if (selection.usbTx) {
            message += "\n" + (transmitStatus == null ? "Opening C-Media USB transmit audio" : transmitStatus);
        }
        if (selection.receivePlayback) {
            message += "\n" + (playbackStatus == null ? "Opening receive playback" : playbackStatus);
        }
        publish(true, message);
        getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(message));
    }

    private void requestStop() {
        if (server == 0) {
            stopSelf();
        } else if (!stopping) {
            stopping = true;
            publish(true, "Stopping");
            if (bluetooth != null) bluetooth.cancelPendingOpen();
            nativeStop(server);
        }
    }

    private void finish(String error) {
        if (server != 0) {
            nativeDestroy(server);
            server = 0;
        }
        if (watcherRegistered) {
            unregisterReceiver(detachWatcher);
            watcherRegistered = false;
        }
        if (usb != null) {
            usb.unregisterNative();
            usb.shutdown();
            usb = null;
        }
        if (bluetooth != null) {
            bluetooth.unregisterNative();
            bluetooth.shutdown();
            bluetooth = null;
        }
        publish(false, error != null ? error : stoppedReason != null ? stoppedReason : "Stopped");
        selection = null;
        stoppedReason = null;
        stopping = false;
        listening = false;
        audioStatus = null;
        playbackStatus = null;
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override
    public void onDestroy() {
        if (server != 0) requestStop();
        super.onDestroy();
    }

    private void publish(boolean active, String message) {
        status = new Status(active, message, selection);
        sendBroadcast(new Intent(ACTION_STATUS).setPackage(getPackageName()));
    }

    private Notification notification(String message) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(
            new NotificationChannel(CHANNEL, "CAT server", NotificationManager.IMPORTANCE_LOW));
        PendingIntent show = PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class),
            PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop = PendingIntent.getService(this, 0,
            new Intent(this, CatService.class).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("Android rigctld")
            .setContentText(message)
            .setContentIntent(show)
            .addAction(new Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build();
    }

    private static native long nativeCreate();
    private native String nativeRun(long handle, int model, String path, String baud, String handshake,
                                    boolean allowPtt, boolean usbAudioRx, boolean trusdxAudioRx, long output);
    private static native void nativeAudioFailed(long handle);
    private static native long nativeTruSdxSamples();
    private static native void nativeStop(long handle);
    private static native void nativeDestroy(long handle);
}
