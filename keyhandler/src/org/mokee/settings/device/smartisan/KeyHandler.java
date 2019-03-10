/*
 * Copyright (C) 2019 The MoKee Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.mokee.settings.device.smartisan;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.ViewConfiguration;
import android.view.WindowManager;

import com.android.internal.os.DeviceKeyHandler;
import com.android.internal.util.ScreenshotHelper;

import mokee.providers.MKSettings;

import org.mokee.internal.util.FileUtils;

public class KeyHandler implements DeviceKeyHandler {

    private static final String TAG = "KeyHandler";

    private static final String FILE_SHORTCUTS = "/proc/keypad/shortcuts";
    private static final String FILE_POWER = "/proc/keypad/power";

    private static final String TRUSTED_SHORTCUTS_DEVICE_NAME = "gpio-keys";
    private static final String TRUSTED_POWER_DEVICE_NAME = "qpnp_pon";

    private final int longPressTimeout = ViewConfiguration.getLongPressTimeout();

    private Context context;
    private PowerManager pm;
    private ScreenshotHelper screenshotHelper;

    private int trustedShortcutsDeviceId = 0;
    private int trustedPowerDeviceId = 0;

    private boolean shortcutsKeyPressed = false;
    private boolean powerKeyPressed = false;

    private int shortcutsScanCode = 0;
    private int shortcutsKeyCode = 0;

    private int powerScanCode = 0;
    private int powerKeyCode = 0;

    private Handler handler = new Handler(Looper.getMainLooper());

    private final Runnable triggerPartialScreenshot = new Runnable() {
        @Override
        public void run() {
            if (shortcutsKeyPressed && powerKeyPressed) {
                takeScreenshot(true);
                injectKey(shortcutsKeyCode, KeyEvent.ACTION_UP, KeyEvent.FLAG_CANCELED);
                injectKey(powerKeyCode, KeyEvent.ACTION_UP, KeyEvent.FLAG_CANCELED);
                shortcutsKeyPressed = false;
                powerKeyPressed = false;
            }
        }
    };

    public KeyHandler(Context context) {
        this.context = context;
        this.pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        this.screenshotHelper = new ScreenshotHelper(context);

        try {
            shortcutsScanCode = Integer.parseInt(FileUtils.readOneLine(FILE_SHORTCUTS));
            powerScanCode = Integer.parseInt(FileUtils.readOneLine(FILE_POWER));
        } catch (NumberFormatException ignored) {
        }
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        if (handleShortcutsKeyEvent(event) || handlePowerKeyEvent(event)) {
            return null;
        } else {
            return event;
        }
    }

    private boolean handleShortcutsKeyEvent(KeyEvent event) {
        if (trustedShortcutsDeviceId == 0) {
            final String deviceName = getDeviceName(event);
            if (TRUSTED_SHORTCUTS_DEVICE_NAME.equals(deviceName)) {
                trustedShortcutsDeviceId = event.getDeviceId();
            } else {
                return false;
            }
        } else {
            if (trustedShortcutsDeviceId != event.getDeviceId()) {
                return false;
            }
        }

        if (event.getScanCode() == shortcutsScanCode) {
            shortcutsKeyCode = event.getKeyCode();
        } else {
            return false;
        }

        switch (event.getAction()) {
            case KeyEvent.ACTION_DOWN:
                if (powerKeyPressed) {
                    injectKey(powerKeyCode, KeyEvent.ACTION_UP, KeyEvent.FLAG_CANCELED);
                    handler.postDelayed(triggerPartialScreenshot, longPressTimeout);
                } else {
                    injectKey(shortcutsKeyCode, KeyEvent.ACTION_DOWN, 0);
                }
                if (pm.isInteractive()) {
                    shortcutsKeyPressed = true;
                }
                break;
            case KeyEvent.ACTION_UP:
                if (powerKeyPressed) {
                    takeScreenshot(false);
                    powerKeyPressed = false;
                } else {
                    injectKey(shortcutsKeyCode, KeyEvent.ACTION_UP, 0);
                }
                handler.removeCallbacks(triggerPartialScreenshot);
                shortcutsKeyPressed = false;
                break;
        }

        return true;
    }

    private boolean handlePowerKeyEvent(KeyEvent event) {
        if (trustedPowerDeviceId == 0) {
            final String deviceName = getDeviceName(event);
            if (TRUSTED_POWER_DEVICE_NAME.equals(deviceName)) {
                trustedPowerDeviceId = event.getDeviceId();
            } else {
                return false;
            }
        } else {
            if (trustedPowerDeviceId != event.getDeviceId()) {
                return false;
            }
        }

        if (event.getScanCode() == powerScanCode) {
            powerKeyCode = event.getKeyCode();
        } else {
            return false;
        }

        switch (event.getAction()) {
            case KeyEvent.ACTION_DOWN:
                if (shortcutsKeyPressed) {
                    injectKey(shortcutsKeyCode, KeyEvent.ACTION_UP, KeyEvent.FLAG_CANCELED);
                    handler.postDelayed(triggerPartialScreenshot, longPressTimeout);
                } else {
                    injectKey(powerKeyCode, KeyEvent.ACTION_DOWN, 0);
                }
                if (pm.isInteractive()) {
                    powerKeyPressed = true;
                }
                break;
            case KeyEvent.ACTION_UP:
                if (shortcutsKeyPressed) {
                    takeScreenshot(false);
                    shortcutsKeyPressed = false;
                } else {
                    injectKey(powerKeyCode, KeyEvent.ACTION_UP, 0);
                }
                handler.removeCallbacks(triggerPartialScreenshot);
                powerKeyPressed = false;
                break;
        }

        return true;
    }

    private String getDeviceName(KeyEvent event) {
        final int deviceId = event.getDeviceId();
        final InputDevice device = InputDevice.getDevice(deviceId);
        return device == null ? null : device.getName();
    }

    private void injectKey(int code, int action, int flags) {
        final long now = SystemClock.uptimeMillis();
        InputManager.getInstance().injectInputEvent(new KeyEvent(
                        now, now, action, code, 0, 0,
                        KeyCharacterMap.VIRTUAL_KEYBOARD,
                        0, flags,
                        InputDevice.SOURCE_KEYBOARD),
                InputManager.INJECT_INPUT_EVENT_MODE_ASYNC);
    }

    private void takeScreenshot(final boolean partial) {
        final int type = partial
                ? WindowManager.TAKE_SCREENSHOT_SELECTED_REGION
                : WindowManager.TAKE_SCREENSHOT_FULLSCREEN;

        screenshotHelper.takeScreenshot(type, true, true, handler);
    }

}
