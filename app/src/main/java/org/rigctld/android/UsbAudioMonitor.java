// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

final class UsbAudioMonitor {
    private static final int SAMPLE_RATE = 48000;

    // These are the two USB Audio Class 1 capture endpoints observed on the FX-4CR.
    private record Format(int interfaceId, int endpointAddress, int channels, boolean setSampleRate) {}

    static boolean supports(UsbDevice device) {
        return device != null && format(device) != null;
    }

    static boolean supportsRxStream(UsbDevice device) {
        return device != null && device.getVendorId() == 0x0d8c && device.getProductId() == 0x013c;
    }

    private static Format format(UsbDevice device) {
        int vendor = device.getVendorId();
        int product = device.getProductId();
        if (vendor == 0x0d8c && product == 0x013c) return new Format(2, 0x82, 1, true);
        if (vendor == 0x0a12 && product == 0x4007) return new Format(1, 0x83, 2, false);
        return null;
    }

    static String measure(UsbManager manager, UsbDevice device, AtomicBoolean cancelled,
                          Consumer<String> update) {
        return capture(manager, device, cancelled, update, 0, null);
    }

    static String capture(UsbManager manager, UsbDevice device, AtomicBoolean cancelled,
                          Consumer<String> update, long server, ReceivePlayback playback) {
        return transfer(manager, device, cancelled, update, server, playback, false, 0);
    }

    static String checkSilentOutput(UsbManager manager, UsbDevice device, AtomicBoolean cancelled) {
        return output(manager, device, cancelled, null, 0);
    }

    static String output(UsbManager manager, UsbDevice device, AtomicBoolean cancelled,
                         Consumer<String> update, long handle) {
        if (!supportsRxStream(device)) return "Select the C-Media USB audio device.";
        return transfer(manager, device, cancelled, update, 0, null, true, handle);
    }

    static native long createOutput();
    static native void destroyOutput(long handle);
    private static native String nativeOutput(int fd, AtomicBoolean cancelled,
                                              Consumer<String> update, long handle);

    private static String transfer(UsbManager manager, UsbDevice device, AtomicBoolean cancelled,
                                   Consumer<String> update, long server, ReceivePlayback playback,
                                   boolean output, long outputHandle) {
        Format format = output ? new Format(1, 0x01, 2, true) : format(device);
        String direction = output ? "playback" : "capture";
        if (format == null) return "This USB input does not have a monitor format yet.";
        UsbInterface idle = null;
        UsbInterface stream = null;
        UsbEndpoint endpoint = null;
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface candidate = device.getInterface(i);
            if (candidate.getId() != format.interfaceId()) continue;
            if (candidate.getAlternateSetting() == 0) idle = candidate;
            if (candidate.getAlternateSetting() != 1) continue;
            for (int j = 0; j < candidate.getEndpointCount(); j++) {
                UsbEndpoint candidateEndpoint = candidate.getEndpoint(j);
                boolean matches = output
                    ? candidate.getInterfaceClass() == UsbConstants.USB_CLASS_AUDIO
                        && candidate.getInterfaceSubclass() == 2
                        && candidateEndpoint.getType() == UsbConstants.USB_ENDPOINT_XFER_ISOC
                        && candidateEndpoint.getInterval() == 1
                        && candidateEndpoint.getMaxPacketSize() >= 192
                    : UsbAudioInputs.isCaptureEndpoint(candidate, candidateEndpoint);
                if (candidateEndpoint.getAddress() == format.endpointAddress() && matches) {
                    stream = candidate;
                    endpoint = candidateEndpoint;
                    break;
                }
            }
        }
        if (idle == null || endpoint == null) return "Expected USB " + direction + " endpoint was not found.";
        if (cancelled.get()) return "Monitor stopped.";
        UsbDeviceConnection connection = manager.openDevice(device);
        if (connection == null) return "Could not open the selected USB device.";
        try {
            if (output && !hasStereoOutputFormat(connection.getRawDescriptors())) {
                return "USB playback does not advertise 48 kHz stereo PCM16.";
            }
            if (!connection.claimInterface(stream, true)) return "Could not claim the USB " + direction + " interface.";
            try {
                if (cancelled.get()) return "Monitor stopped.";
                if (!connection.setInterface(stream)) return "Could not start the USB " + direction + " interface.";
                if (format.setSampleRate()) {
                    // UAC1 SET_CUR sampling frequency control on the capture endpoint.
                    byte[] rate = {(byte) SAMPLE_RATE, (byte) (SAMPLE_RATE >> 8), 0};
                    int result = connection.controlTransfer(0x22, 0x01, 0x0100,
                        endpoint.getAddress(), rate, rate.length, 500);
                    if (result != rate.length) return "Could not set USB " + direction + " to 48 kHz (" + result + ").";
                }
                System.loadLibrary("android_rigctld");
                if (output) return nativeOutput(connection.getFileDescriptor(), cancelled, update, outputHandle);
                return nativeMeasure(connection.getFileDescriptor(), endpoint.getAddress(),
                    endpoint.getMaxPacketSize(), format.channels(),
                    cancelled, update, server, playback);
            } finally {
                connection.setInterface(idle);
                connection.releaseInterface(stream);
            }
        } finally {
            connection.close();
        }
    }

    private static boolean hasStereoOutputFormat(byte[] descriptors) {
        if (descriptors == null) return false;
        boolean output = false;
        boolean pcm = false;
        for (int offset = 0; offset + 2 <= descriptors.length;) {
            int length = descriptors[offset] & 0xff;
            if (length < 2 || length > descriptors.length - offset) return false;
            int type = descriptors[offset + 1] & 0xff;
            if (type == 4) {
                output = length >= 9 && descriptors[offset + 2] == 1
                    && descriptors[offset + 3] == 1 && descriptors[offset + 5] == 1
                    && descriptors[offset + 6] == 2;
                pcm = false;
            } else if (output && type == 0x24 && length >= 7) {
                int subtype = descriptors[offset + 2] & 0xff;
                if (subtype == 1) pcm = descriptors[offset + 5] == 1 && descriptors[offset + 6] == 0;
                if (subtype == 2 && pcm && length >= 8 && descriptors[offset + 3] == 1
                        && descriptors[offset + 4] == 2 && descriptors[offset + 5] == 2
                        && descriptors[offset + 6] == 16) {
                    int count = descriptors[offset + 7] & 0xff;
                    if (count == 0 && length >= 14) {
                        return rateAt(descriptors, offset + 8) <= SAMPLE_RATE
                            && rateAt(descriptors, offset + 11) >= SAMPLE_RATE;
                    }
                    for (int index = 0; index < count && 8 + 3 * (index + 1) <= length; index++) {
                        if (rateAt(descriptors, offset + 8 + 3 * index) == SAMPLE_RATE) return true;
                    }
                }
            }
            offset += length;
        }
        return false;
    }

    private static int rateAt(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8) | ((bytes[offset + 2] & 0xff) << 16);
    }

    private static native String nativeMeasure(int fd, int endpoint, int packetSize, int channels,
                                               AtomicBoolean cancelled, Consumer<String> update, long server,
                                               ReceivePlayback playback);

    private UsbAudioMonitor() {}
}
