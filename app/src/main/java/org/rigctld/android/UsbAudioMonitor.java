// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.hardware.usb.UsbDevice;
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

    private static Format format(UsbDevice device) {
        int vendor = device.getVendorId();
        int product = device.getProductId();
        if (vendor == 0x0d8c && product == 0x013c) return new Format(2, 0x82, 1, true);
        if (vendor == 0x0a12 && product == 0x4007) return new Format(1, 0x83, 2, false);
        return null;
    }

    static String measure(UsbManager manager, UsbDevice device, AtomicBoolean cancelled,
                          Consumer<String> update) {
        Format format = format(device);
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
                if (candidateEndpoint.getAddress() == format.endpointAddress()
                        && UsbAudioInputs.isCaptureEndpoint(candidate, candidateEndpoint)) {
                    stream = candidate;
                    endpoint = candidateEndpoint;
                    break;
                }
            }
        }
        if (idle == null || endpoint == null) return "Expected USB capture endpoint was not found.";
        if (cancelled.get()) return "Monitor stopped.";
        UsbDeviceConnection connection = manager.openDevice(device);
        if (connection == null) return "Could not open the selected USB input.";
        try {
            if (!connection.claimInterface(stream, true)) return "Could not claim the USB capture interface.";
            try {
                if (!connection.setInterface(stream)) return "Could not start the USB capture interface.";
                if (format.setSampleRate()) {
                    // UAC1 SET_CUR sampling frequency control on the capture endpoint.
                    byte[] rate = {(byte) SAMPLE_RATE, (byte) (SAMPLE_RATE >> 8), 0};
                    int result = connection.controlTransfer(0x22, 0x01, 0x0100,
                        endpoint.getAddress(), rate, rate.length, 500);
                    if (result != rate.length) return "Could not set the USB input to 48 kHz (" + result + ").";
                }
                System.loadLibrary("android_rigctld");
                return nativeMeasure(connection.getFileDescriptor(), endpoint.getAddress(),
                    endpoint.getMaxPacketSize(), format.channels(), cancelled, update);
            } finally {
                connection.setInterface(idle);
                connection.releaseInterface(stream);
            }
        } finally {
            connection.close();
        }
    }

    private static native String nativeMeasure(int fd, int endpoint, int packetSize, int channels,
                                               AtomicBoolean cancelled, Consumer<String> update);

    private UsbAudioMonitor() {}
}
