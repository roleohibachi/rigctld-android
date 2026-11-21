// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.ggmorse;

import android.app.Activity;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    private static final int MAX_TEXT = 8192;
    private final StringBuilder text = new StringBuilder();
    private AtomicBoolean cancelled;
    private TextView status;
    private TextView transcript;
    private ScrollView scroll;
    private Button connect;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(24 * getResources().getDisplayMetrics().density);
        content.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("GGMorse Rigctld");
        title.setTextSize(24);
        content.addView(title);
        status = new TextView(this);
        status.setText("Disconnected · 127.0.0.1:4532");
        content.addView(status);
        connect = new Button(this);
        connect.setText("Connect");
        connect.setOnClickListener(view -> {
            if (cancelled == null) startReceive();
            else stopReceive();
        });
        content.addView(connect);
        Button clear = new Button(this);
        clear.setText("Clear decoded text");
        clear.setOnClickListener(view -> {
            text.setLength(0);
            transcript.setText("");
        });
        content.addView(clear);
        transcript = new TextView(this);
        transcript.setTypeface(Typeface.MONOSPACE);
        transcript.setTextIsSelectable(true);
        transcript.setTextSize(19);
        transcript.setHint("Decoded CW appears here");
        scroll = new ScrollView(this);
        scroll.addView(transcript);
        content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(content);
    }

    private void startReceive() {
        AtomicBoolean stop = new AtomicBoolean();
        cancelled = stop;
        status.setText("Connecting to 127.0.0.1:4532…");
        connect.setText("Disconnect");
        new Thread(() -> {
            String result;
            try {
                System.loadLibrary("ggmorse_rigctld");
                result = receive(stop);
            } catch (RuntimeException | LinkageError failure) {
                result = "Receiver failed: " + failure.getMessage();
            }
            String message = result;
            runOnUiThread(() -> {
                if (isDestroyed() || cancelled != stop) return;
                cancelled = null;
                status.setText(stop.get() ? "Disconnected · 127.0.0.1:4532" : message);
                connect.setText("Connect");
                connect.setEnabled(true);
            });
        }, "rigctld-cw-receiver").start();
    }

    private void stopReceive() {
        if (cancelled == null) return;
        cancelled.set(true);
        connect.setEnabled(false);
        status.setText("Disconnecting…");
    }

    @Override protected void onStop() {
        stopReceive();
        super.onStop();
    }

    private void onReceiveStatus(String message) {
        runOnUiThread(() -> {
            if (!isDestroyed() && cancelled != null && !cancelled.get()) status.setText(message);
        });
    }

    private void onDecodedText(String decoded) {
        runOnUiThread(() -> {
            if (isDestroyed() || cancelled == null || cancelled.get()) return;
            text.append(decoded);
            if (text.length() > MAX_TEXT) text.delete(0, text.length() - MAX_TEXT);
            transcript.setText(text);
            scroll.post(() -> scroll.fullScroll(View.FOCUS_DOWN));
        });
    }

    private native String receive(AtomicBoolean cancelled);
}
