// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.content.Intent;

final class RadioSelection {
    static final int DUMMY = 0;
    static final int USB = 1;
    static final int BLUETOOTH = 2;
    static final int TRUSDX_MODEL = 2055;
    static final int FX4_MODEL = 2053;

    final int transport;
    final int model;
    final String path;
    final String baud;
    final boolean rtsCts;
    final boolean allowPtt;
    final String usbAudioPath;
    final boolean receivePlayback;
    final boolean trusdxAudio;
    final boolean usbTx;

    RadioSelection(int transport, int model, String path, String baud, boolean rtsCts, boolean allowPtt) {
        this(transport, model, path, baud, rtsCts, allowPtt, null, false, false, false);
    }

    RadioSelection(int transport, int model, String path, String baud, boolean rtsCts, boolean allowPtt,
                   String usbAudioPath, boolean receivePlayback, boolean trusdxAudio, boolean usbTx) {
        this.transport = transport;
        this.model = model;
        this.path = path;
        this.baud = baud;
        this.rtsCts = rtsCts;
        this.allowPtt = allowPtt;
        this.usbAudioPath = usbAudioPath;
        this.receivePlayback = receivePlayback;
        this.trusdxAudio = trusdxAudio;
        this.usbTx = usbTx;
    }

    void writeTo(Intent intent) {
        intent.putExtra("transport", transport);
        intent.putExtra("model", model);
        intent.putExtra("path", path);
        intent.putExtra("baud", baud);
        intent.putExtra("rtsCts", rtsCts);
        intent.putExtra("allowPtt", allowPtt);
        intent.putExtra("usbAudioPath", usbAudioPath);
        intent.putExtra("receivePlayback", receivePlayback);
        intent.putExtra("trusdxAudio", trusdxAudio);
        intent.putExtra("usbTx", usbTx);
    }

    static RadioSelection readFrom(Intent intent) {
        return new RadioSelection(
            intent.getIntExtra("transport", DUMMY), intent.getIntExtra("model", 1),
            intent.getStringExtra("path"), intent.getStringExtra("baud"),
            intent.getBooleanExtra("rtsCts", false), intent.getBooleanExtra("allowPtt", false),
            intent.getStringExtra("usbAudioPath"), intent.getBooleanExtra("receivePlayback", false),
            intent.getBooleanExtra("trusdxAudio", false), intent.getBooleanExtra("usbTx", false));
    }

    boolean valid() {
        if (usbTx && (model != FX4_MODEL || usbAudioPath == null || !allowPtt || receivePlayback || trusdxAudio)) {
            return false;
        }
        if (usbAudioPath != null && (transport != USB || (allowPtt && !usbTx))) return false;
        if (receivePlayback && usbAudioPath == null) return false;
        if (trusdxAudio && (transport != USB || model != TRUSDX_MODEL || allowPtt || rtsCts
                || !"115200".equals(baud) || usbAudioPath != null)) return false;
        if (transport == DUMMY) return model == 1 && (path == null || path.isEmpty());
        if (model <= 1 || path == null) return false;
        if (transport == USB) return path.matches("android-usb:[0-9]+:[0-9]+");
        if (transport == BLUETOOTH) return path.matches("android-bt:[0-9a-fA-F]{12}:[0-9]+");
        return false;
    }
}
