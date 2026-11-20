// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

final class UsbAudioInputs {
    private static final int AUDIO_STREAMING_SUBCLASS = 2;
    private static final int ISOCHRONOUS_USAGE_MASK = 0x30;
    private static final int ISOCHRONOUS_FEEDBACK = 0x10;

    record Input(String deviceName, String label, List<String> endpoints) {
        @Override public String toString() {
            return label;
        }
    }

    static List<Input> list(UsbManager manager) {
        List<Input> inputs = new ArrayList<>();
        for (UsbDevice device : manager.getDeviceList().values()) {
            List<String> endpoints = new ArrayList<>();
            for (int i = 0; i < device.getInterfaceCount(); i++) {
                UsbInterface usbInterface = device.getInterface(i);
                if (usbInterface.getInterfaceClass() != UsbConstants.USB_CLASS_AUDIO
                        || usbInterface.getInterfaceSubclass() != AUDIO_STREAMING_SUBCLASS) continue;
                for (int j = 0; j < usbInterface.getEndpointCount(); j++) {
                    UsbEndpoint endpoint = usbInterface.getEndpoint(j);
                    if (endpoint.getDirection() != UsbConstants.USB_DIR_IN
                            || endpoint.getType() != UsbConstants.USB_ENDPOINT_XFER_ISOC
                            || (endpoint.getAttributes() & ISOCHRONOUS_USAGE_MASK) == ISOCHRONOUS_FEEDBACK) continue;
                    endpoints.add(String.format(Locale.ROOT, "Interface %d, alt %d, endpoint 0x%02x",
                        usbInterface.getId(), usbInterface.getAlternateSetting(), endpoint.getAddress()));
                }
            }
            if (endpoints.isEmpty()) continue;
            String product = device.getProductName();
            String label = String.format(Locale.ROOT, "%s (%04x:%04x)",
                product == null || product.trim().isEmpty() ? "USB audio input" : product,
                device.getVendorId(), device.getProductId());
            inputs.add(new Input(device.getDeviceName(), label, List.copyOf(endpoints)));
        }
        inputs.sort(Comparator.comparing(Input::label).thenComparing(Input::deviceName));
        return inputs;
    }

    private UsbAudioInputs() {}
}
