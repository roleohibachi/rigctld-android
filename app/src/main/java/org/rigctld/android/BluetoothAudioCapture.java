// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

final class BluetoothAudioCapture {
    interface Samples { void accept(short[] samples, int count); }

    static String capture(Context context, String address, AtomicBoolean cancelled,
                          Consumer<String> status, Samples sink) {
        AudioManager manager = context.getSystemService(AudioManager.class);
        AudioDeviceInfo communication = null;
        for (AudioDeviceInfo device : manager.getAvailableCommunicationDevices()) {
            if (device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                    && address.equalsIgnoreCase(device.getAddress().replace(":", ""))) {
                communication = device;
                break;
            }
        }
        if (communication == null) return "Bluetooth headset audio is unavailable";
        int previousMode = manager.getMode();
        AudioDeviceInfo previousDevice = manager.getCommunicationDevice();
        AudioRecord record = null;
        try {
            manager.setMode(AudioManager.MODE_IN_COMMUNICATION);
            if (!manager.setCommunicationDevice(communication)) return "Could not select Bluetooth headset audio";
            AudioDeviceInfo input = null;
            for (int tries = 0; tries < 50 && !cancelled.get(); tries++) {
                for (AudioDeviceInfo device : manager.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
                    if (device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                            && address.equalsIgnoreCase(device.getAddress().replace(":", ""))) input = device;
                }
                if (input != null) break;
                Thread.sleep(100);
            }
            if (cancelled.get()) return "Bluetooth audio stopped";
            if (input == null) return "Bluetooth headset microphone is unavailable";
            int size = AudioRecord.getMinBufferSize(48000, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
            if (size <= 0) return "48 kHz Bluetooth capture is unavailable";
            record = new AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, 48000,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, Math.max(size, 9600));
            if (record.getState() != AudioRecord.STATE_INITIALIZED || !record.setPreferredDevice(input)) {
                return "Could not open Bluetooth audio input";
            }
            record.startRecording();
            short[] samples = new short[960];
            boolean ready = false;
            while (!cancelled.get()) {
                int count = record.read(samples, 0, samples.length);
                if (count <= 0) return "Bluetooth audio read failed (" + count + ")";
                AudioDeviceInfo route = record.getRoutedDevice();
                if (route == null || route.getId() != input.getId()) {
                    return "Bluetooth audio input route changed";
                }
                sink.accept(samples, count);
                if (!ready) {
                    ready = true;
                    status.accept("Bluetooth receive ready · 48 kHz mono PCM16");
                }
            }
            return "Bluetooth audio stopped";
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            return "Bluetooth audio interrupted";
        } finally {
            if (record != null) {
                if (record.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) record.stop();
                record.release();
            }
            manager.clearCommunicationDevice();
            manager.setMode(previousMode);
            if (previousDevice != null) manager.setCommunicationDevice(previousDevice);
        }
    }

    private BluetoothAudioCapture() {}
}
