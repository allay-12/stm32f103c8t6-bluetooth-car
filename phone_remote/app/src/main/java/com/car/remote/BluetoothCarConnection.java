package com.car.remote;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Manages Bluetooth Classic (SPP) connection to HC-05 car module.
 * Runs sending on a single-thread executor to prevent backlog.
 */
public final class BluetoothCarConnection implements AutoCloseable {
    // Standard SPP UUID for HC-05 / Serial Port Profile
    public static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    public interface Listener {
        void onStatus(boolean connected, String message);
        void onData(String line);
    }

    private static final class Motion {
        final int forward, turn;
        Motion(int forward, int turn) {
            this.forward = forward;
            this.turn = turn;
        }
    }

    private static final class Session {
        final BluetoothSocket socket;
        final OutputStream output;
        final int token;
        volatile long lastWriteNanos = System.nanoTime();
        Session(BluetoothSocket socket, int token) throws IOException {
            this.socket = socket;
            this.output = socket.getOutputStream();
            this.token = token;
        }
    }

    private final Listener listener;
    private final AtomicInteger generation = new AtomicInteger();
    private final Object lock = new Object();
    private final ScheduledExecutorService writer = Executors.newSingleThreadScheduledExecutor();
    private volatile Motion motion = new Motion(0, 0);
    private volatile Session session;
    private volatile boolean connecting;
    private volatile boolean closed;
    private volatile boolean manualMode = false;

    public BluetoothCarConnection(Listener listener) {
        this.listener = listener;
        // In manual mode, periodically stream joystick positions (50ms)
        writer.scheduleWithFixedDelay(this::sendManualMotionStream, 0, 50, TimeUnit.MILLISECONDS);
    }

    public boolean isConnected() { return session != null; }
    public boolean isConnecting() { return connecting; }

    public void setManualMode(boolean active) {
        this.manualMode = active;
        if (!active) {
            motion = new Motion(0, 0);
        }
    }

    public void setMotion(int forward, int turn) {
        motion = new Motion(clamp(forward), clamp(turn));
    }

    private static int clamp(int v) { return Math.max(-100, Math.min(100, v)); }

    public static String getSafeDeviceName(BluetoothDevice device) {
        if (device == null) return "未知设备";
        try {
            String name = device.getName();
            return (name != null && !name.isEmpty()) ? name : device.getAddress();
        } catch (SecurityException ignored) {
            return device.getAddress() != null ? device.getAddress() : "HC-05";
        }
    }

    public void connect(BluetoothDevice device) {
        disconnect();
        if (closed || device == null) return;
        final int token;
        synchronized (lock) {
            token = generation.get();
            connecting = true;
        }
        listener.onStatus(false, "正在连接 " + getSafeDeviceName(device) + "...");
        writer.execute(() -> {
            BluetoothSocket socket = null;
            try {
                if (generation.get() != token) return;
                // Cancel discovery before connecting to speed up connection
                BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
                if (adapter != null && adapter.isDiscovering()) {
                    try { adapter.cancelDiscovery(); } catch (SecurityException ignored) {}
                }

                socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
                socket.connect();

                Session ready = new Session(socket, token);
                synchronized (lock) {
                    if (generation.get() != token) {
                        closeSocket(socket);
                        return;
                    }
                    connecting = false;
                    session = ready;
                }
                listener.onStatus(true, "已连接: " + getSafeDeviceName(device));
                Thread reader = new Thread(() -> readUntilClosed(ready), "bt-car-reader");
                reader.setDaemon(true);
                reader.start();
            } catch (Exception error) {
                closeSocket(socket);
                synchronized (lock) {
                    if (generation.get() != token) return;
                    connecting = false;
                }
                listener.onStatus(false, "连接失败: " + error.getMessage());
            }
        });
    }

    public void sendCommand(String cmd) {
        Session current = session;
        if (current != null && !closed) {
            writer.execute(() -> {
                if (session == current) write(current, cmd.endsWith("\n") ? cmd : (cmd + "\n"));
            });
        }
    }

    public void sendPwm(int pct) {
        sendCommand("SET_PWM=" + (pct * 10) + "\n");
    }

    public void sendTurnPwm(int pct) {
        sendCommand("SET_TURN_PWM=" + (pct * 10) + "\n");
    }

    public void sendModeAuto() {
        setManualMode(false);
        sendCommand("MODE,AUTO\n");
    }

    public void sendModeRace() {
        setManualMode(false);
        sendCommand("MODE,RACE\n");
    }

    public void sendModeManual() {
        setManualMode(true);
        sendCommand("MODE,MANUAL\n");
    }

    public void sendStop() {
        setManualMode(false);
        motion = new Motion(0, 0);
        sendCommand("STOP\n");
    }

    public void disconnect() {
        synchronized (lock) {
            generation.incrementAndGet();
            motion = new Motion(0, 0);
            connecting = false;
            manualMode = false;
            Session old = session;
            session = null;
            if (old != null) closeSocket(old.socket);
        }
        listener.onStatus(false, "未连接");
    }

    private void sendManualMotionStream() {
        Session current = session;
        if (current == null || !manualMode) return;
        Motion m = motion;
        write(current, "MANUAL," + m.forward + "," + m.turn + "\n");
    }

    private void write(Session current, String frame) {
        try {
            current.output.write(frame.getBytes(StandardCharsets.US_ASCII));
            current.output.flush();
            current.lastWriteNanos = System.nanoTime();
        } catch (IOException error) {
            fail(current, "蓝牙通信已断开");
        }
    }

    private void readUntilClosed(Session current) {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(current.socket.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while (session == current && (line = reader.readLine()) != null) {
                if (!line.isEmpty()) {
                    listener.onData(line);
                }
            }
        } catch (IOException ignored) {
        } finally {
            fail(current, "小车蓝牙已断开");
        }
    }

    private void fail(Session current, String message) {
        synchronized (lock) {
            if (session != current) return;
            session = null;
            generation.incrementAndGet();
            motion = new Motion(0, 0);
            manualMode = false;
            closeSocket(current.socket);
        }
        listener.onStatus(false, message);
    }

    private static void closeSocket(BluetoothSocket socket) {
        if (socket != null) {
            try { socket.close(); } catch (IOException ignored) { }
        }
    }

    @Override public void close() {
        closed = true;
        disconnect();
        writer.shutdownNow();
    }
}
