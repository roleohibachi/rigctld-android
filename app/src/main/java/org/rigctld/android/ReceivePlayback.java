// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRouting;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.SystemClock;
import java.nio.ByteBuffer;
import java.util.function.Consumer;

final class ReceivePlayback implements AutoCloseable {
    private static final int SAMPLE_RATE = 48000;
    private static final int BLOCK_FRAMES = 480;
    private final AudioTrack track;
    private final Consumer<String> update;
    private final AudioRouting.OnRoutingChangedListener routingListener = routing -> checkRouteChange();
    private final byte[] silence = new byte[BLOCK_FRAMES * 2];
    private final long routeDeadline = SystemClock.elapsedRealtime() + 2000;
    private int primedBytes;
    private boolean playing;
    private boolean stopped;

    ReceivePlayback(AudioManager manager, Handler handler, Consumer<String> update) {
        this.update = update;
        AudioDeviceInfo speaker = null;
        for (AudioDeviceInfo device : manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
                speaker = device;
                break;
            }
        }
        if (speaker == null) throw new IllegalStateException("Phone speaker is unavailable");
        int minimum = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT);
        if (minimum <= 0) throw new IllegalStateException("PCM16 playback is unavailable");
        track = new AudioTrack.Builder()
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .setAllowedCapturePolicy(AudioAttributes.ALLOW_CAPTURE_BY_ALL).build())
            .setAudioFormat(new AudioFormat.Builder().setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
            .setBufferSizeInBytes(Math.max(minimum, BLOCK_FRAMES * 4))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build();
        try {
            if (!track.setPreferredDevice(speaker)) throw new IllegalStateException("Could not select phone speaker");
            track.setBufferSizeInFrames(BLOCK_FRAMES * 2);
            track.setStartThresholdInFrames(BLOCK_FRAMES);
            track.addOnRoutingChangedListener(routingListener, handler);
            track.play();
        } catch (RuntimeException error) {
            close();
            throw error;
        }
    }

    // The native buffer belongs to one USB transfer and is only valid during this call.
    synchronized void write(ByteBuffer pcm, int bytes) {
        if (stopped) return;
        try {
            writeSamples(pcm, bytes);
        } catch (RuntimeException failure) {
            stopWithMessage("Receive playback stopped · " + failure.getMessage());
        }
    }

    private void writeSamples(ByteBuffer pcm, int bytes) {
        AudioDeviceInfo routed = track.getRoutedDevice();
        if (primedBytes < silence.length || !isSpeaker(routed)) {
            if (playing || SystemClock.elapsedRealtime() >= routeDeadline) {
                stopForRoute(routed);
            } else {
                int written = track.write(silence, 0, Math.min(bytes, silence.length), AudioTrack.WRITE_NON_BLOCKING);
                if (written >= 0) primedBytes += written;
                else stopWithMessage("Receive playback stopped · AudioTrack error " + written);
            }
            return;
        }
        if (!playing) {
            playing = true;
            update.accept("Receive playback ready · phone speaker");
        }
        pcm.position(0);
        int written = track.write(pcm, bytes, AudioTrack.WRITE_NON_BLOCKING);
        if (written < 0) stopWithMessage("Receive playback stopped · AudioTrack error " + written);
    }

    private synchronized void checkRouteChange() {
        if (!stopped && playing) {
            AudioDeviceInfo routed = track.getRoutedDevice();
            if (!isSpeaker(routed)) stopForRoute(routed);
        }
    }

    private void stopForRoute(AudioDeviceInfo routed) {
        stopWithMessage(routed == null ? "Receive playback stopped · speaker route unverified"
            : "Receive playback stopped · output changed to " + routed.getProductName());
    }

    private void stopWithMessage(String message) {
        close();
        update.accept(message);
    }

    private static boolean isSpeaker(AudioDeviceInfo device) {
        return device != null && (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
            || device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE);
    }

    @Override public synchronized void close() {
        if (stopped) return;
        stopped = true;
        try {
            track.removeOnRoutingChangedListener(routingListener);
            if (track.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) track.pause();
            track.flush();
        } finally {
            track.release();
        }
    }
}
