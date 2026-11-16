package com.car.remote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/**
 * Custom 2D Joystick view for steering and throttle.
 * x: -1.0 (left) to +1.0 (right)
 * y: -1.0 (backward) to +1.0 (forward)
 */
public final class JoystickView extends View {
    public interface Listener { void onMove(float x, float y); }

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final String label;
    private final Listener listener;
    private float x = 0f, y = 0f;
    private int pointer = -1;

    public JoystickView(Context context, String label, Listener listener) {
        super(context);
        this.label = label;
        this.listener = listener;
        setContentDescription(label);
    }

    private float radius() {
        return Math.max(1, Math.min(getWidth(), getHeight()) * 0.36f);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float cx = getWidth() / 2f;
        float cy = getHeight() / 2f + 8;
        float r = radius();

        // Base circle
        paint.setColor(Color.rgb(20, 32, 48));
        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(cx, cy, r, paint);

        // Border ring
        paint.setColor(isEnabled() ? Color.rgb(55, 95, 130) : Color.rgb(40, 50, 60));
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        canvas.drawCircle(cx, cy, r, paint);

        // Crosshairs
        paint.setColor(isEnabled() ? Color.rgb(40, 70, 95) : Color.rgb(30, 40, 50));
        paint.setStrokeWidth(2);
        canvas.drawLine(cx - r, cy, cx + r, cy, paint);
        canvas.drawLine(cx, cy - r, cx, cy + r, paint);

        // Joystick knob
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(isEnabled() ? Color.rgb(35, 195, 170) : Color.rgb(80, 90, 100));
        canvas.drawCircle(cx + x * r, cy - y * r, r * 0.28f, paint);

        // Inner knob accent
        paint.setColor(isEnabled() ? Color.rgb(180, 245, 235) : Color.rgb(130, 140, 150));
        canvas.drawCircle(cx + x * r, cy - y * r, r * 0.10f, paint);

        // Label
        paint.setColor(isEnabled() ? Color.rgb(210, 225, 240) : Color.rgb(120, 130, 140));
        paint.setTextSize(14 * getResources().getDisplayMetrics().scaledDensity);
        paint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(label, cx, 24 * getResources().getDisplayMetrics().density, paint);
    }

    public void reset() {
        pointer = -1;
        x = 0f;
        y = 0f;
        listener.onMove(0f, 0f);
        invalidate();
    }

    private void updateTouch(MotionEvent event, int index) {
        float r = radius();
        float nx = (event.getX(index) - getWidth() / 2f) / r;
        float ny = -(event.getY(index) - (getHeight() / 2f + 8)) / r;

        float len = (float) Math.sqrt(nx * nx + ny * ny);
        if (len > 1.0f) {
            nx /= len;
            ny /= len;
        }
        if (len < 0.06f) {
            nx = 0f;
            ny = 0f;
        }
        x = nx;
        y = ny;
        listener.onMove(x, y);
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!isEnabled()) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            pointer = event.getPointerId(0);
            getParent().requestDisallowInterceptTouchEvent(true);
            updateTouch(event, 0);
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            int idx = event.findPointerIndex(pointer);
            if (idx >= 0) updateTouch(event, idx);
            return true;
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL ||
            (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == pointer)) {
            reset();
            getParent().requestDisallowInterceptTouchEvent(false);
            if (action == MotionEvent.ACTION_UP) performClick();
            return true;
        }
        return true;
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }
}
