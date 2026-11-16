#include "car_control.h"
#include "car_config.h"
#include <string.h>

static int16_t clamp(int32_t value, int16_t low, int16_t high)
{
    if (value < low) return low;
    if (value > high) return high;
    return (int16_t)value;
}

static int16_t magnitude(int16_t value)
{
    return value < 0 ? (int16_t)-value : value;
}

/* Floor motion targets before ramping, never turn a stop into movement. */
static int16_t running_target(int16_t target)
{
    if (target == 0) return 0;
    if (target > 0) return clamp(target, CAR_MIN_RUN_DUTY, CAR_MAX_DUTY);
    return (int16_t)-clamp(-(int32_t)target, CAR_MIN_RUN_DUTY, CAR_MAX_DUTY);
}

static void debounce(CarDebounce *input, uint8_t value)
{
    value = value != 0U;
    if (input->candidate != value) {
        input->candidate = value;
        input->count = 1U;
    } else if (input->count < CAR_LINE_STABLE_SAMPLES) {
        ++input->count;
    }
    if (input->count >= CAR_LINE_STABLE_SAMPLES) input->stable = value;
}

static void enter(CarControl *car, CarState state, uint32_t now)
{
    car->state = state;
    car->state_since_ms = now;
    car->confirm_active = 0U;
    car->steer_active = 0U;
    car->missing_active = 0U;
    car->error_q8 = 0;
}

static void stop_wheel(CarWheelRamp *wheel, uint32_t now)
{
    if (wheel->value != 0) wheel->zero_since_ms = now;
    wheel->value = 0;
}

/* Reverse through zero, then dwell before applying the opposite polarity. */
static void ramp(CarWheelRamp *wheel, int16_t target, uint32_t now, uint8_t turning)
{
    int8_t sign = target > 0 ? 1 : (target < 0 ? -1 : 0);
    int16_t step;
    int16_t next;
    if (wheel->value == 0 && sign != 0 && wheel->last_sign != 0 &&
        sign != wheel->last_sign &&
        (uint32_t)(now - wheel->zero_since_ms) < CAR_REVERSE_DEADTIME_MS) return;
    if ((wheel->value > 0 && target < 0) || (wheel->value < 0 && target > 0)) {
        target = 0;
    }
    if (turning)
        step = magnitude(target) > magnitude(wheel->value) ? CAR_TURN_ACCEL_STEP : CAR_TURN_DECEL_STEP;
    else
        step = magnitude(target) > magnitude(wheel->value) ? CAR_ACCEL_STEP : CAR_DECEL_STEP;
    next = wheel->value;
    if (next < target) next = clamp((int32_t)next + step, next, target);
    else if (next > target) next = clamp((int32_t)next - step, target, next);
    if (next == 0 && wheel->value != 0) wheel->zero_since_ms = now;
    if (next != 0) wheel->last_sign = next > 0 ? 1 : -1;
    wheel->value = next;
}

static uint8_t confirmed(CarControl *car, uint8_t condition, uint32_t duration, uint32_t now)
{
    if (!condition) {
        car->confirm_active = 0U;
        return 0U;
    }
    if (!car->confirm_active) {
        car->confirm_active = 1U;
        car->confirm_since_ms = now;
    }
    return (uint32_t)(now - car->confirm_since_ms) >= duration;
}

static uint8_t in_bypass(CarState state)
{
    return (state >= CAR_OBSTACLE_STOP && state <= CAR_BYPASS_SETTLE) ||
           state == CAR_BYPASS_SEARCH_FORWARD;
}

void CarControl_Init(CarControl *car, uint32_t now_ms)
{
    memset(car, 0, sizeof(*car));
#if CAR_POWERON_STANDBY
    car->state = CAR_STANDBY;
#else
    car->state = CAR_STARTUP;
#endif
    car->race_mode = CAR_RACE_MARKERS_ENABLED != 0U;
    car->marker_armed = car->race_mode;
    if (!CAR_ULTRASONIC_ENABLED) car->range_status = CAR_RANGE_DISABLED;
    car->boot_ms = now_ms;
    car->state_since_ms = now_ms;
    car->last_step_ms = now_ms;
    car->left_wheel.zero_since_ms = now_ms;
    car->right_wheel.zero_since_ms = now_ms;
    car->last_line_direction = 1;
}

void CarControl_SampleLine4(CarControl *car, uint8_t outer_left, uint8_t left,
                            uint8_t right, uint8_t outer_right)
{
    debounce(&car->left_outer_line, outer_left);
    debounce(&car->left_line, left);
    debounce(&car->right_line, right);
    debounce(&car->right_outer_line, outer_right);
    car->line_mask = (uint8_t)((car->left_outer_line.stable << 3) |
                     (car->left_line.stable << 2) | (car->right_line.stable << 1) |
                     car->right_outer_line.stable);
    if (car->line_sample_count < CAR_LINE_STABLE_SAMPLES) ++car->line_sample_count;
}

uint8_t CarControl_DistanceFromPulse(uint16_t pulse_us, uint16_t *distance_mm)
{
    if (pulse_us < 100U || pulse_us > 25000U) return 0U;
    /* Round-trip flight at nominal 343m/s, integer arithmetic in millimeters. */
    *distance_mm = (uint16_t)(((uint32_t)pulse_us * 343U + 1000U) / 2000U);
    if (*distance_mm > CAR_RANGE_MAX_MM) *distance_mm = CAR_RANGE_MAX_MM;
    return 1U;
}

void CarControl_ObserveRange(CarControl *car, CarRangeStatus status,
                             uint16_t distance_mm, uint32_t now_ms)
{
#if CAR_ULTRASONIC_ENABLED
    uint8_t valid_sample = 0U;
    car->range_status = status;
    if (status == CAR_RANGE_VALID) {
        if (distance_mm >= CAR_RANGE_MIN_VALID_MM && distance_mm <= CAR_RANGE_MAX_MM) {
            valid_sample = 1U;
            car->range_mm = distance_mm;
            car->range_since_ms = now_ms;
        }
    } else if (status == CAR_RANGE_NO_ECHO && CAR_NO_ECHO_IS_CLEAR) {
        valid_sample = 1U;
        distance_mm = CAR_RANGE_MAX_MM;
        car->range_mm = CAR_RANGE_MAX_MM;
        car->range_since_ms = now_ms;
    }
    if (!valid_sample) {
        car->range_obstacle_count = 0U;
        return;
    }
    if (car->range_good_count < CAR_RANGE_READY_SAMPLES) ++car->range_good_count;
    if (distance_mm <= CAR_RANGE_STOP_MM) {
        if (car->range_obstacle_count < CAR_RANGE_CONFIRM_SAMPLES) ++car->range_obstacle_count;
        car->range_clear_count = 0U;
    } else if (distance_mm >= CAR_RANGE_RELEASE_MM) {
        if (car->range_clear_count < CAR_RANGE_READY_SAMPLES) ++car->range_clear_count;
        car->range_obstacle_count = 0U;
    } else {
        car->range_obstacle_count = 0U;
        car->range_clear_count = 0U;
    }
#else
    (void)car; (void)status; (void)distance_mm; (void)now_ms;
#endif
}

void CarControl_Trip(CarControl *car, uint32_t now_ms)
{
    enter(car, CAR_CONTROL_FAULT, now_ms);
    car->target_left = 0;
    car->target_right = 0;
    stop_wheel(&car->left_wheel, now_ms);
    stop_wheel(&car->right_wheel, now_ms);
}

/* A marker is one qualified ALL-on episode, not one control-loop sample.
 * Require continuous ALL-on and a qualified departure before the next event. */
static void update_marker(CarControl *car, uint32_t now)
{
    if (car->line_mask != 15U) {
        car->marker_timing = 0U;
        if (!car->marker_armed) {
            if (!car->marker_release_timing) {
                car->marker_release_timing = 1U;
                car->marker_release_since_ms = now;
            }
            if ((uint32_t)(now - car->marker_release_since_ms) >= CAR_MARKER_RELEASE_MS) {
                if (car->marker_count == 0U ||
                    (uint32_t)(now - car->race_start_ms) >= CAR_MARKER_FINISH_LOCKOUT_MS) {
                    car->marker_armed = 1U;
                }
            }
        }
        return;
    }
    car->marker_release_timing = 0U;
    if (!car->marker_armed) return;
    if (!car->marker_timing) {
        car->marker_timing = 1U;
        car->marker_since_ms = now;
    }
    if ((uint32_t)(now - car->marker_since_ms) < CAR_MARKER_CONFIRM_MS) return;
    car->marker_armed = 0U;
    car->marker_timing = 0U;
    if (car->marker_count < 2U) ++car->marker_count;
    if (car->marker_count == 1U) {
        car->race_start_ms = now;
        enter(car, CAR_FOLLOW, now);
    } else {
        enter(car, CAR_FINISHED, now);
    }
}

static void follow(CarControl *car, uint32_t now)
{
    uint8_t lo = car->left_outer_line.stable;
    uint8_t li = car->left_line.stable;
    uint8_t ri = car->right_line.stable;
    uint8_t ro = car->right_outer_line.stable;
    uint8_t left_pair = lo && li;
    uint8_t right_pair = ri && ro;
    int8_t direction = 0;
    uint8_t sharp = 0U;
    int16_t fast, slow;
    /* User rule: trigger on the left => steer LEFT; trigger on the right => steer RIGHT.
     * Symmetric inputs (0000/0110/1001/1111) command straight travel. */
    if (left_pair != right_pair) {
        direction = left_pair ? -1 : 1;
        sharp = 1U;
    } else if (lo != ro) {
        /* Leftmost or rightmost detector alone commands sharp turn to prevent running off track. */
        direction = lo ? -1 : 1;
        sharp = 1U;
    } else if (li != ri) {
        direction = li ? -1 : 1;
    }
    if (direction == 0) {
        car->target_left = car->target_right = CAR_CRUISE_DUTY;
    } else {
        fast = sharp ? CAR_SHARP_FAST_DUTY : CAR_TURN_FAST_DUTY;
        slow = sharp ? CAR_SHARP_SLOW_DUTY : CAR_TURN_SLOW_DUTY;
        car->target_left = direction > 0 ? fast : slow;
        car->target_right = direction > 0 ? slow : fast;
    }
    if (CAR_ULTRASONIC_ENABLED && car->range_mm < CAR_RANGE_SLOW_MM &&
        (uint32_t)(now - car->range_since_ms) <= CAR_RANGE_STALE_MS) {
        car->target_left = clamp(car->target_left, -CAR_CROSSING_DUTY, CAR_CROSSING_DUTY);
        car->target_right = clamp(car->target_right, -CAR_CROSSING_DUTY, CAR_CROSSING_DUTY);
    }
}

static void start_bypass_align(CarControl *car, uint32_t now)
{
    enter(car, CAR_BYPASS_ALIGN, now);
    car->target_left = car->target_right = 0;
    stop_wheel(&car->left_wheel, now);
    stop_wheel(&car->right_wheel, now);
}

static void finish_bypass(CarControl *car, uint32_t now)
{
    car->bypass_completed = 1U;
    car->bypass_end_ms = now;
    enter(car, CAR_FOLLOW, now);
    follow(car, now);
}

void CarControl_Step(CarControl *car, uint32_t now)
{
    uint32_t elapsed = (uint32_t)(now - car->last_step_ms);
    uint32_t phase_ms;
    uint8_t close;
    uint8_t bypass_due;
    uint8_t moving_forward;
    uint8_t turning;
    uint8_t wide_line;
    if (elapsed < CAR_CONTROL_MS) return;
    if (elapsed > CAR_CONTROL_MAX_GAP_MS) CarControl_Trip(car, now);
    car->last_step_ms = now;
    car->target_left = 0;
    car->target_right = 0;
    if (car->state == CAR_CONTROL_FAULT || car->state == CAR_BLOCKED ||
        car->state == CAR_FINISHED || car->state == CAR_STANDBY) goto apply;

    if (car->state == CAR_MANUAL) {
        if ((uint32_t)(now - car->manual_since_ms) <= 500U) {
            car->target_left = car->manual_left;
            car->target_right = car->manual_right;
        }
        goto apply;
    }

    if (CAR_MOTOR_TEST_MODE != 0U) {
        /* Explicit finite bench test: no line/range input can redirect a wheel
         * or launch an obstacle bypass. Scheduler faults still stop the car. */
        phase_ms = (uint32_t)(now - car->boot_ms);
        if (car->state == CAR_MOTOR_TEST_DONE) goto apply;
        if (phase_ms < CAR_STARTUP_MS) goto apply;
        phase_ms -= CAR_STARTUP_MS;
        if (phase_ms >= CAR_MOTOR_TEST_RUN_MS) {
            enter(car, CAR_MOTOR_TEST_DONE, now);
        } else {
            if (car->state != CAR_MOTOR_TEST) enter(car, CAR_MOTOR_TEST, now);
            if (phase_ms < CAR_MOTOR_TEST_RUN_MS - CAR_MOTOR_TEST_RAMP_DOWN_MS) {
                if (CAR_MOTOR_TEST_MODE == 1U || CAR_MOTOR_TEST_MODE == 3U)
                    car->target_left = CAR_MOTOR_TEST_DUTY;
                if (CAR_MOTOR_TEST_MODE == 2U || CAR_MOTOR_TEST_MODE == 3U)
                    car->target_right = CAR_MOTOR_TEST_DUTY;
            }
        }
        goto apply;
    }

    if (car->state == CAR_STARTUP &&
        (uint32_t)(now - car->boot_ms) < CAR_STARTUP_MS) goto apply;
    /* 超声波滤波确认障碍物（经过连续多帧有效测距确认） */
    close = CAR_ULTRASONIC_ENABLED && (car->range_obstacle_count >= CAR_RANGE_CONFIRM_SAMPLES);
    bypass_due = close && (!car->bypass_completed ||
        (uint32_t)(now - car->bypass_end_ms) >= CAR_BYPASS_REPEAT_GUARD_MS);

    if (car->state == CAR_STARTUP || (car->state == CAR_FOLLOW && !bypass_due)) {
        if (car->left_line.count < CAR_LINE_STABLE_SAMPLES ||
            car->right_line.count < CAR_LINE_STABLE_SAMPLES ||
            car->left_outer_line.count < CAR_LINE_STABLE_SAMPLES ||
            car->right_outer_line.count < CAR_LINE_STABLE_SAMPLES) {
            /* A candidate glitch breaks marker confirmation. */
            car->marker_timing = 0U;
            car->marker_release_timing = 0U;
        } else if (car->race_mode || CAR_RACE_MARKERS_ENABLED) {
            update_marker(car, now);
        } else if (car->state == CAR_STARTUP) {
            enter(car, CAR_FOLLOW, now);
        }
    }
    if (car->state == CAR_STARTUP || car->state == CAR_FINISHED) goto apply;

    if (car->state == CAR_FOLLOW && bypass_due) {
        /* Rearm only after a qualified departure once line following resumes. */
        car->marker_armed = 0U;
        car->marker_timing = 0U;
        car->marker_release_timing = 0U;
        car->bypass_since_ms = now;
        enter(car, CAR_OBSTACLE_STOP, now);
        goto apply;
    }

    if (in_bypass(car->state)) {
        /* 避障总超时保护 */
        if ((uint32_t)(now - car->bypass_since_ms) > CAR_BYPASS_TOTAL_LIMIT_MS) {
            enter(car, CAR_BLOCKED, now);
            goto apply;
        }
        /* 绕障直行过程中如果正面近距离又遇障碍物，安全停车 */
        moving_forward = car->state == CAR_BYPASS_OFFSET || car->state == CAR_BYPASS_PASS ||
                         car->state == CAR_BYPASS_SEEK || car->state == CAR_BYPASS_CENTER ||
                         car->state == CAR_BYPASS_SEARCH_FORWARD;
        if (moving_forward && close) {
            enter(car, CAR_BLOCKED, now);
            goto apply;
        }
    }

    phase_ms = (uint32_t)(now - car->state_since_ms);
    wide_line = car->left_outer_line.stable + car->left_line.stable +
                car->right_line.stable + car->right_outer_line.stable >= 3;
    switch (car->state) {
    case CAR_MANUAL:
        if ((uint32_t)(now - car->manual_since_ms) <= 500U) {
            car->target_left = car->manual_left;
            car->target_right = car->manual_right;
        }
        break;
    case CAR_FOLLOW:
        follow(car, now);
        break;
    case CAR_OBSTACLE_STOP:
        if (phase_ms >= CAR_BYPASS_STOP_MS) {
            if (car->range_clear_count >= CAR_RANGE_READY_SAMPLES) enter(car, CAR_FOLLOW, now);
            else enter(car, CAR_BYPASS_TURN_OUT, now);
        }
        break;
    case CAR_BYPASS_TURN_OUT:
        /* 差速左转避障（右轮快、左轮慢，均为正转向前） */
        car->target_left = CAR_BYPASS_TURN_SLOW_DUTY;
        car->target_right = CAR_BYPASS_TURN_FAST_DUTY;
        if (phase_ms >= CAR_BYPASS_TURN_MS) {
            enter(car, CAR_BYPASS_OFFSET_MS > 0U ? CAR_BYPASS_OFFSET : CAR_BYPASS_SEEK, now);
        }
        break;
    case CAR_BYPASS_OFFSET:
    case CAR_BYPASS_PASS:
#if CAR_BYPASS_OFFSET_MS > 0
        car->target_left = CAR_BYPASS_DRIVE_DUTY;
        car->target_right = CAR_BYPASS_DRIVE_DUTY;
        if (phase_ms >= CAR_BYPASS_OFFSET_MS) {
            enter(car, CAR_BYPASS_SEEK, now);
        }
#else
        enter(car, CAR_BYPASS_SEEK, now);
#endif
        break;
    case CAR_BYPASS_SEEK:
        /* 步骤3：差速右转弯找回黑线（左轮快、右轮慢，均为正转向前） */
        car->target_left = CAR_BYPASS_SEEK_FAST_DUTY;
        car->target_right = CAR_BYPASS_SEEK_SLOW_DUTY;
        /* 三路或四路全黑时尽快结束右转，即使仍处于普通找线屏蔽期。 */
        if (confirmed(car, wide_line || (phase_ms >= CAR_BYPASS_SEEK_IGNORE_MS &&
                      car->line_mask != 0U), CAR_BYPASS_LINE_CONFIRM_MS, now)) {
            if (wide_line) {
                start_bypass_align(car, now);
            } else {
                enter(car, CAR_BYPASS_CENTER, now);
                follow(car, now);
            }
        } else if (phase_ms >= CAR_BYPASS_SEEK_LIMIT_MS) {
            enter(car, CAR_BLOCKED, now);
        }
        break;
    case CAR_BYPASS_CENTER:
        /* 首个探头碰线后，给随后压线的其他探头留出检测时间。 */
        if (wide_line) {
            start_bypass_align(car, now);
        } else if (phase_ms >= CAR_BYPASS_CENTER_MS) {
            finish_bypass(car, now);
        } else {
            follow(car, now);
        }
        break;
    case CAR_BYPASS_ALIGN:
        /* 最短左转时间后，两侧探头均离线时停转；全白时由直行阶段找线。 */
        if (phase_ms >= CAR_BYPASS_ALIGN_LIMIT_MS) {
            enter(car, CAR_BLOCKED, now);
        } else if (phase_ms >= CAR_REVERSE_DEADTIME_MS + CAR_BYPASS_ALIGN_MIN_MS &&
                   confirmed(car, (car->line_mask & 9U) == 0U, CAR_BYPASS_ALIGN_CONFIRM_MS, now)) {
            enter(car, CAR_BYPASS_SETTLE, now);
        } else if (phase_ms >= CAR_REVERSE_DEADTIME_MS) {
            car->target_left = -CAR_BYPASS_ALIGN_DUTY;
            car->target_right = CAR_BYPASS_ALIGN_DUTY;
        }
        break;
    case CAR_BYPASS_SETTLE:
        if (phase_ms >= CAR_BYPASS_SETTLE_MS) {
            if (wide_line) enter(car, CAR_BLOCKED, now);
            else if (car->line_mask == 0U) enter(car, CAR_BYPASS_SEARCH_FORWARD, now);
            else finish_bypass(car, now);
        }
        break;
    case CAR_BYPASS_SEARCH_FORWARD:
        if (confirmed(car, car->line_mask != 0U, CAR_BYPASS_LINE_CONFIRM_MS, now)) {
            if (wide_line) start_bypass_align(car, now);
            else finish_bypass(car, now);
        } else if (phase_ms >= CAR_BYPASS_SEARCH_FORWARD_MS) {
            enter(car, CAR_BLOCKED, now);
        } else {
            car->target_left = car->target_right = CAR_BYPASS_SEARCH_FORWARD_DUTY;
        }
        break;
    default:
        break;
    }

apply:
    if (car->state == CAR_STARTUP || car->state == CAR_OBSTACLE_STOP ||
        car->state == CAR_BYPASS_SETTLE || car->state == CAR_LINE_LOST ||
        car->state == CAR_RANGE_FAULT || car->state == CAR_BLOCKED ||
        car->state == CAR_CONTROL_FAULT || car->state == CAR_MOTOR_TEST_DONE ||
        car->state == CAR_FINISHED || car->state == CAR_STANDBY) {
        car->target_left = car->target_right = 0;
        stop_wheel(&car->left_wheel, now);
        stop_wheel(&car->right_wheel, now);
    } else if (car->state == CAR_MANUAL) {
        ramp(&car->left_wheel, car->target_left, now, 1U);
        ramp(&car->right_wheel, car->target_right, now, 1U);
    } else {
        car->target_left = running_target(car->target_left);
        car->target_right = running_target(car->target_right);
        turning = ((car->state == CAR_FOLLOW || in_bypass(car->state)) &&
                   car->target_left != car->target_right) ||
                  ((car->state == CAR_FOLLOW || car->state == CAR_BYPASS_CENTER) &&
                   car->left_wheel.value != car->right_wheel.value);
        ramp(&car->left_wheel, car->target_left, now, turning);
        ramp(&car->right_wheel, car->target_right, now, turning);
    }
}

void CarControl_StartAuto(CarControl *car, uint32_t now)
{
    car->race_mode = 0U;
    car->marker_armed = 0U;
    car->marker_count = 0U;
    car->last_step_ms = now;
    enter(car, CAR_FOLLOW, now);
}

void CarControl_StartRace(CarControl *car, uint32_t now)
{
    car->race_mode = 1U;
    car->marker_count = 0U;
    car->marker_armed = 1U;
    car->marker_timing = 0U;
    car->marker_release_timing = 0U;
    car->last_step_ms = now;
    enter(car, CAR_STARTUP, now);
}

void CarControl_StartManual(CarControl *car, uint32_t now)
{
    car->manual_left = 0;
    car->manual_right = 0;
    car->target_left = 0;
    car->target_right = 0;
    car->manual_since_ms = now;
    car->last_step_ms = now;
    enter(car, CAR_MANUAL, now);
}

void CarControl_SetManual(CarControl *car, int16_t forward_pct, int16_t turn_pct, uint32_t now)
{
    int32_t left = ((int32_t)forward_pct + turn_pct) * CAR_MAX_DUTY / 100;
    int32_t right = ((int32_t)forward_pct - turn_pct) * CAR_MAX_DUTY / 100;
    car->manual_left = clamp(left, -CAR_MAX_DUTY, CAR_MAX_DUTY);
    car->manual_right = clamp(right, -CAR_MAX_DUTY, CAR_MAX_DUTY);
    car->manual_since_ms = now;
    if (car->state == CAR_MANUAL) {
        car->target_left = car->manual_left;
        car->target_right = car->manual_right;
    }
}

void CarControl_Stop(CarControl *car, uint32_t now)
{
    enter(car, CAR_STANDBY, now);
    car->target_left = 0;
    car->target_right = 0;
    stop_wheel(&car->left_wheel, now);
    stop_wheel(&car->right_wheel, now);
}
