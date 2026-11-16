package com.car.remote;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

public final class MainActivity extends Activity {
    private static final int TEXT_COLOR = Color.rgb(228, 236, 245);
    private static final int BG_COLOR = Color.rgb(11, 20, 31);
    private static final int CARD_COLOR = Color.rgb(18, 29, 44);

    // Mode state
    private enum AppMode {
        STANDBY, AUTO_LINE, RACE, MANUAL
    }

    private BluetoothCarConnection connection;
    private BluetoothAdapter bluetoothAdapter;
    private TextView tvStatus, tvCurrentMode, tvValues, tvForwardSpeedLabel, tvTurnSpeedLabel;
    private Button btnConnect, btnAutoLine, btnRaceMode, btnManual, btnStop;
    private JoystickView joystick;
    private SeekBar forwardSlider, turnSlider;

    private AppMode currentMode = AppMode.STANDBY;
    private float joystickX = 0f, joystickY = 0f;
    private int forwardLimitPct = 70; // 默认直行PWM 70%, 范围 20%~100%
    private int turnLimitPct = 60;    // 默认转弯PWM 60%, 范围 20%~100%

    private int dp(int n) {
        return Math.round(n * getResources().getDisplayMetrics().density);
    }

    private Button createStyledButton(String text, int bgColor, int textColor) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextColor(textColor);
        b.setTextSize(14);
        b.setBackgroundTintList(ColorStateList.valueOf(bgColor));
        b.setAllCaps(false);
        return b;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            // Keep screen awake
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        } catch (Throwable ignored) {}

        try {
            bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        } catch (Throwable ignored) {}

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG_COLOR);
        root.setPadding(dp(14), dp(8), dp(14), dp(8));
        setContentView(root);

        // Safely defer full immersive hide until after view attachment
        root.post(this::applyImmersiveFullscreen);

        // --- TOP BAR: Title, status, connect button ---
        LinearLayout topBar = new LinearLayout(this);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        topBar.setPadding(0, 0, 0, dp(6));

        TextView title = new TextView(this);
        title.setText("循迹遥控小车控制台");
        title.setTextSize(18);
        title.setTextColor(Color.WHITE);
        topBar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        tvCurrentMode = new TextView(this);
        tvCurrentMode.setText("模式: 待机停车");
        tvCurrentMode.setTextColor(Color.rgb(255, 195, 0));
        tvCurrentMode.setTextSize(13);
        tvCurrentMode.setPadding(dp(8), 0, dp(12), 0);
        topBar.addView(tvCurrentMode);

        tvStatus = new TextView(this);
        tvStatus.setText("未连接蓝牙");
        tvStatus.setTextColor(Color.rgb(160, 175, 190));
        tvStatus.setTextSize(13);
        tvStatus.setPadding(0, 0, dp(12), 0);
        topBar.addView(tvStatus);

        btnConnect = createStyledButton("连接蓝牙", Color.rgb(33, 115, 185), Color.WHITE);
        btnConnect.setOnClickListener(v -> handleConnectClick());
        topBar.addView(btnConnect, new LinearLayout.LayoutParams(dp(105), dp(38)));

        root.addView(topBar);

        // --- MAIN BODY: Left 4 buttons, Right joystick & sliders ---
        LinearLayout mainBody = new LinearLayout(this);
        mainBody.setOrientation(LinearLayout.HORIZONTAL);

        // LEFT: 4 Action Buttons
        LinearLayout leftButtons = new LinearLayout(this);
        leftButtons.setOrientation(LinearLayout.VERTICAL);
        leftButtons.setBackgroundColor(CARD_COLOR);
        leftButtons.setPadding(dp(10), dp(8), dp(10), dp(8));

        TextView btnGroupLabel = new TextView(this);
        btnGroupLabel.setText("小车运行模式");
        btnGroupLabel.setTextColor(Color.rgb(180, 195, 210));
        btnGroupLabel.setTextSize(12);
        btnGroupLabel.setPadding(0, 0, 0, dp(6));
        leftButtons.addView(btnGroupLabel);

        // Button 1: 自动寻线模式
        btnAutoLine = createStyledButton("① 自动寻线模式", Color.rgb(30, 75, 115), TEXT_COLOR);
        btnAutoLine.setOnClickListener(v -> selectMode(AppMode.AUTO_LINE));
        LinearLayout.LayoutParams lpBtn1 = new LinearLayout.LayoutParams(dp(205), dp(46));
        lpBtn1.setMargins(0, 0, 0, dp(6));
        leftButtons.addView(btnAutoLine, lpBtn1);

        // Button 2: 比赛模式
        btnRaceMode = createStyledButton("② 比赛模式 (起终点)", Color.rgb(40, 70, 60), TEXT_COLOR);
        btnRaceMode.setOnClickListener(v -> selectMode(AppMode.RACE));
        LinearLayout.LayoutParams lpBtn2 = new LinearLayout.LayoutParams(dp(205), dp(46));
        lpBtn2.setMargins(0, 0, 0, dp(6));
        leftButtons.addView(btnRaceMode, lpBtn2);

        // Button 3: 手动模式
        btnManual = createStyledButton("③ 手动遥控模式", Color.rgb(50, 60, 85), TEXT_COLOR);
        btnManual.setOnClickListener(v -> selectMode(AppMode.MANUAL));
        LinearLayout.LayoutParams lpBtn3 = new LinearLayout.LayoutParams(dp(205), dp(46));
        lpBtn3.setMargins(0, 0, 0, dp(8));
        leftButtons.addView(btnManual, lpBtn3);

        // Button 4: 一键停车 (Red bold button)
        btnStop = createStyledButton("🛑 一键停车 (STOP)", Color.rgb(185, 40, 50), Color.WHITE);
        btnStop.setTextSize(15);
        btnStop.setOnClickListener(v -> selectMode(AppMode.STANDBY));
        LinearLayout.LayoutParams lpBtn4 = new LinearLayout.LayoutParams(dp(205), dp(50));
        leftButtons.addView(btnStop, lpBtn4);

        mainBody.addView(leftButtons, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Spacer
        View divider = new View(this);
        divider.setBackgroundColor(Color.TRANSPARENT);
        mainBody.addView(divider, new LinearLayout.LayoutParams(dp(10), ViewGroup.LayoutParams.MATCH_PARENT));

        // RIGHT: Joystick & PWM Sliders (直行PWM + 转弯PWM)
        LinearLayout rightPanel = new LinearLayout(this);
        rightPanel.setOrientation(LinearLayout.VERTICAL);
        rightPanel.setBackgroundColor(CARD_COLOR);
        rightPanel.setPadding(dp(10), dp(6), dp(10), dp(6));

        joystick = new JoystickView(this, "前后推力 / 左右转向", (x, y) -> {
            joystickX = x;
            joystickY = y;
            updateManualMotion();
        });
        joystick.setEnabled(false);
        rightPanel.addView(joystick, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        // Slider 1: 直行PWM (20% ~ 100%)
        LinearLayout forwardSpeedRow = new LinearLayout(this);
        forwardSpeedRow.setGravity(Gravity.CENTER_VERTICAL);
        tvForwardSpeedLabel = new TextView(this);
        tvForwardSpeedLabel.setText("直行PWM: 70%");
        tvForwardSpeedLabel.setTextColor(TEXT_COLOR);
        tvForwardSpeedLabel.setTextSize(12);
        forwardSpeedRow.addView(tvForwardSpeedLabel, new LinearLayout.LayoutParams(dp(95), ViewGroup.LayoutParams.WRAP_CONTENT));

        forwardSlider = new SeekBar(this);
        forwardSlider.setMax(100);
        forwardSlider.setProgress(forwardLimitPct);
        forwardSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                forwardLimitPct = Math.max(20, Math.min(100, progress));
                tvForwardSpeedLabel.setText("直行PWM: " + forwardLimitPct + "%");
                if (fromUser && connection != null && connection.isConnected()) {
                    connection.sendPwm(forwardLimitPct);
                }
                updateManualMotion();
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        forwardSpeedRow.addView(forwardSlider, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        rightPanel.addView(forwardSpeedRow);

        // Slider 2: 转弯PWM (20% ~ 100%)
        LinearLayout turnSpeedRow = new LinearLayout(this);
        turnSpeedRow.setGravity(Gravity.CENTER_VERTICAL);
        tvTurnSpeedLabel = new TextView(this);
        tvTurnSpeedLabel.setText("转弯PWM: 60%");
        tvTurnSpeedLabel.setTextColor(TEXT_COLOR);
        tvTurnSpeedLabel.setTextSize(12);
        turnSpeedRow.addView(tvTurnSpeedLabel, new LinearLayout.LayoutParams(dp(95), ViewGroup.LayoutParams.WRAP_CONTENT));

        turnSlider = new SeekBar(this);
        turnSlider.setMax(100);
        turnSlider.setProgress(turnLimitPct);
        turnSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                turnLimitPct = Math.max(20, Math.min(100, progress));
                tvTurnSpeedLabel.setText("转弯PWM: " + turnLimitPct + "%");
                if (fromUser && connection != null && connection.isConnected()) {
                    connection.sendTurnPwm(turnLimitPct);
                }
                updateManualMotion();
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        turnSpeedRow.addView(turnSlider, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        rightPanel.addView(turnSpeedRow);

        mainBody.addView(rightPanel, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        root.addView(mainBody, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        // --- BOTTOM STATUS BAR ---
        tvValues = new TextView(this);
        tvValues.setText("就绪 | 请先点击【连接蓝牙】连接 HC-05 模块");
        tvValues.setTextColor(Color.rgb(150, 165, 180));
        tvValues.setTextSize(12);
        tvValues.setGravity(Gravity.CENTER);
        tvValues.setPadding(0, dp(4), 0, 0);
        root.addView(tvValues);

        // Initialize Bluetooth connection service
        connection = new BluetoothCarConnection(new BluetoothCarConnection.Listener() {
            @Override
            public void onStatus(boolean connected, String message) {
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    tvStatus.setText(message);
                    btnConnect.setText(connected ? "断开蓝牙" : "连接蓝牙");
                    btnConnect.setBackgroundTintList(ColorStateList.valueOf(
                            connected ? Color.rgb(160, 45, 55) : Color.rgb(33, 115, 185)));

                    if (!connected) {
                        selectMode(AppMode.STANDBY);
                    }
                });
            }

            @Override
            public void onData(String line) {
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    tvValues.setText("小车回传: " + line);
                });
            }
        });

        checkBluetoothPermissions();
        updateButtonStyles();
    }

    /**
     * Safe Immersive Sticky Fullscreen: Hides both notification/status bar and navigation bar.
     * When swiped from edge, bars appear temporarily and automatically retract.
     * Guaranteed never to throw or crash on any Android version or vendor ROM.
     */
    private void applyImmersiveFullscreen() {
        try {
            Window window = getWindow();
            if (window == null) return;

            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);

            View decorView = window.getDecorView();
            if (decorView == null) return;

            // Universally supported immersive sticky flags (API 19 through Android 15)
            decorView.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
            );

            // Android 11+ (API 30+) insets controller safely invoked
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                WindowInsetsController controller = decorView.getWindowInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    );
                }
            }
        } catch (Throwable ignored) {}
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveFullscreen();
        }
    }

    private void selectMode(AppMode mode) {
        currentMode = mode;
        updateButtonStyles();

        if (connection == null) return;

        switch (mode) {
            case AUTO_LINE:
                connection.sendModeAuto();
                tvCurrentMode.setText("模式: 自动寻线中");
                tvCurrentMode.setTextColor(Color.rgb(40, 200, 140));
                tvValues.setText("自动寻线模式生效中 (点击【一键停车】可随时制动)");
                break;
            case RACE:
                connection.sendModeRace();
                tvCurrentMode.setText("模式: 比赛就绪/运行");
                tvCurrentMode.setTextColor(Color.rgb(255, 165, 0));
                tvValues.setText("比赛模式生效中 (前2秒防弯道误判，第2次全黑到达终点停车)");
                break;
            case MANUAL:
                connection.sendModeManual();
                tvCurrentMode.setText("模式: 手动遥控");
                tvCurrentMode.setTextColor(Color.rgb(50, 185, 245));
                tvValues.setText("手动模式已激活: 拖动摇杆控制移动，松手自动归零");
                break;
            case STANDBY:
            default:
                connection.sendStop();
                tvCurrentMode.setText("模式: 停车待机");
                tvCurrentMode.setTextColor(Color.rgb(255, 75, 75));
                tvValues.setText("小车已紧急停车并断开驱动 (安全待机)");
                break;
        }

        boolean joystickActive = (mode == AppMode.MANUAL) && connection.isConnected();
        joystick.setEnabled(joystickActive);
        if (!joystickActive) {
            joystick.reset();
        }
    }

    private void updateButtonStyles() {
        btnAutoLine.setBackgroundTintList(ColorStateList.valueOf(
                currentMode == AppMode.AUTO_LINE ? Color.rgb(35, 145, 95) : Color.rgb(30, 50, 75)));
        btnRaceMode.setBackgroundTintList(ColorStateList.valueOf(
                currentMode == AppMode.RACE ? Color.rgb(200, 130, 25) : Color.rgb(40, 55, 65)));
        btnManual.setBackgroundTintList(ColorStateList.valueOf(
                currentMode == AppMode.MANUAL ? Color.rgb(30, 125, 180) : Color.rgb(40, 50, 70)));
    }

    private void updateManualMotion() {
        if (currentMode != AppMode.MANUAL || connection == null || !connection.isConnected()) return;

        // joystickY is forward (-1 to +1), joystickX is turn (-1 to +1)
        int f = Math.round(joystickY * forwardLimitPct);
        int t = Math.round(joystickX * turnLimitPct);

        connection.setMotion(f, t);
        tvValues.setText(String.format("手动遥控 | 直行: %d%%  转向: %d%%", f, t));
    }

    private void handleConnectClick() {
        if (connection.isConnected() || connection.isConnecting()) {
            connection.disconnect();
            return;
        }
        if (bluetoothAdapter == null) {
            Toast.makeText(this, "本机不支持蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }
        if (!bluetoothAdapter.isEnabled()) {
            try {
                startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
            } catch (Throwable t) {
                Toast.makeText(this, "请在系统设置中打开蓝牙", Toast.LENGTH_SHORT).show();
            }
            return;
        }

        showPairedDevicesDialog();
    }

    private void showPairedDevicesDialog() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_DENIED) {
                Toast.makeText(this, "需要蓝牙连接权限", Toast.LENGTH_SHORT).show();
                requestPermissions(new String[]{
                        Manifest.permission.BLUETOOTH_CONNECT,
                        Manifest.permission.BLUETOOTH_SCAN
                }, 101);
                return;
            }
        }

        Set<BluetoothDevice> pairedDevices;
        try {
            pairedDevices = bluetoothAdapter.getBondedDevices();
        } catch (SecurityException se) {
            Toast.makeText(this, "获取已配对设备权限不足: " + se.getMessage(), Toast.LENGTH_SHORT).show();
            return;
        }

        if (pairedDevices == null || pairedDevices.isEmpty()) {
            new AlertDialog.Builder(this)
                .setTitle("未找到已配对蓝牙设备")
                .setMessage("请先在手机系统【设置 -> 蓝牙】中与小车的 HC-05 蓝牙模块完成配对 (通常配对码为 1234 或 0000)，然后再打开本软件。")
                .setPositiveButton("前往系统设置", (d, w) -> {
                    try {
                        startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
                    } catch (Throwable ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
            return;
        }

        List<BluetoothDevice> deviceList = new ArrayList<>(pairedDevices);
        String[] deviceNames = new String[deviceList.size()];
        for (int i = 0; i < deviceList.size(); i++) {
            BluetoothDevice d = deviceList.get(i);
            String name = BluetoothCarConnection.getSafeDeviceName(d);
            deviceNames[i] = name + "\n" + d.getAddress();
        }

        new AlertDialog.Builder(this)
            .setTitle("请选择小车蓝牙模块 (HC-05)")
            .setItems(deviceNames, (dialog, which) -> {
                BluetoothDevice selected = deviceList.get(which);
                connection.connect(selected);
            })
            .setNegativeButton("取消", null)
            .show();
    }

    private void checkBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED ||
                    checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{
                            Manifest.permission.BLUETOOTH_CONNECT,
                            Manifest.permission.BLUETOOTH_SCAN
                    }, 101);
                }
            } catch (Throwable ignored) {}
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveFullscreen();
    }

    @Override
    protected void onPause() {
        // 重要：应用挂后台或息屏时不自动停车！小车保持当前模式（自动寻线/比赛模式）继续跑！
        // 只有在用户主动点击【🛑 一键停车】按键时才停车。
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (connection != null) {
            connection.close();
        }
        super.onDestroy();
    }
}
