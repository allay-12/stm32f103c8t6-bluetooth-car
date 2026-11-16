#include "car_control.h"
#include "car_config.h"
#include <assert.h>
#include <stdio.h>

static uint32_t clock_ms;
static void tick4(CarControl *car, uint8_t mask, CarRangeStatus status, uint16_t mm)
{
    ++clock_ms;
    if (clock_ms % CAR_LINE_SAMPLE_MS == 0U)
        CarControl_SampleLine4(car, (mask >> 3) & 1U, (mask >> 2) & 1U,
                             (mask >> 1) & 1U, mask & 1U);
    if (clock_ms % CAR_RANGE_INTERVAL_MS == 0U)
        CarControl_ObserveRange(car, status, mm, clock_ms);
    CarControl_Step(car, clock_ms);
    assert(car->target_left == 0 || car->target_left >= CAR_MIN_RUN_DUTY || car->target_left <= -CAR_MIN_RUN_DUTY);
    assert(car->target_right == 0 || car->target_right >= CAR_MIN_RUN_DUTY || car->target_right <= -CAR_MIN_RUN_DUTY);
    assert(car->target_left >= -CAR_MAX_DUTY && car->target_left <= CAR_MAX_DUTY);
    assert(car->target_right >= -CAR_MAX_DUTY && car->target_right <= CAR_MAX_DUTY);
}
static void run4(CarControl *car, uint32_t duration, uint8_t mask)
{
    uint32_t i;
    for (i = 0U; i < duration; ++i) tick4(car, mask, CAR_RANGE_VALID, 1000U);
}
static void tick(CarControl *car, uint8_t left, uint8_t right, CarRangeStatus status, uint16_t mm)
{
    tick4(car, (uint8_t)((left << 2) | (right << 1)), status, mm);
}
static void run(CarControl *car, uint32_t duration, uint8_t left, uint8_t right, CarRangeStatus status, uint16_t mm)
{
    uint32_t i;
    for (i = 0U; i < duration; ++i) tick(car, left, right, status, mm);
}
static void start_at(CarControl *car, uint32_t now)
{
    clock_ms = now;
    CarControl_Init(car, now);
    run4(car, CAR_STARTUP_MS + CAR_MARKER_CONFIRM_MS + 100U, 15U);
    assert(car->state == CAR_FOLLOW && car->marker_count == 1U);
}
static void warm(CarControl *car)
{
    start_at(car, 0U);
    run4(car, CAR_MARKER_FINISH_LOCKOUT_MS + 100U, 6U);
    assert(car->marker_armed && car->left_wheel.value == CAR_CRUISE_DUTY);
}
static void close_obstacle(CarControl *car)
{
    CarControl_ObserveRange(car, CAR_RANGE_VALID, 150U, clock_ms);
    CarControl_ObserveRange(car, CAR_RANGE_VALID, 150U, clock_ms + CAR_RANGE_INTERVAL_MS);
    run(car, CAR_CONTROL_MS, 1U, 1U, CAR_RANGE_VALID, 150U);
    assert(car->state == CAR_OBSTACLE_STOP);
    assert(car->left_wheel.value == 0 && car->right_wheel.value == 0);
}
static void begin_bypass(CarControl *car)
{
    close_obstacle(car);
    run(car, CAR_BYPASS_STOP_MS + 20U, 1U, 1U, CAR_RANGE_VALID, 150U);
    assert(car->state == CAR_BYPASS_TURN_OUT);
}
static void test_four_probe_truth_table(void)
{
    /* Order LO LI RI RO. Positive direction = physical right turn. */
    const int8_t expected[15] = {0,2,1,2,-1,2,0,2,-2,0,-2,2,-2,-2,-2};
    uint8_t mask;
    CarControl car;
    for (mask = 0U; mask < 15U; ++mask) {
        int16_t fast, slow;
        warm(&car);
        run4(&car, 600U, mask);
        assert(car.state == CAR_FOLLOW && car.line_mask == mask);
        fast = expected[mask] == 2 || expected[mask] == -2 ? CAR_SHARP_FAST_DUTY : CAR_TURN_FAST_DUTY;
        slow = expected[mask] == 2 || expected[mask] == -2 ? CAR_SHARP_SLOW_DUTY : CAR_TURN_SLOW_DUTY;
        if (!expected[mask]) {
            assert(car.target_left == CAR_CRUISE_DUTY && car.target_right == CAR_CRUISE_DUTY);
        } else {
            assert(car.target_left == (expected[mask] > 0 ? fast : slow));
            assert(car.target_right == (expected[mask] > 0 ? slow : fast));
        }
        assert(car.left_wheel.value == car.target_left && car.right_wheel.value == car.target_right);
    }
}
static void test_reference_turn_response(void)
{
    CarControl car;
    uint32_t i, zero_since = 0U;
    int16_t previous;
    warm(&car);
    run4(&car, 100U, 4U);
    /* Reference Turn_Left: inner wheel stopped, outer wheel at the configured duty. */
    assert(car.target_left == CAR_TURN_SLOW_DUTY && car.target_right == CAR_TURN_FAST_DUTY);
    assert(car.left_wheel.value == CAR_TURN_SLOW_DUTY && car.right_wheel.value == CAR_TURN_FAST_DUTY);
    warm(&car);
    previous = car.left_wheel.value;
    for (i = 0U; i < 260U; ++i) {
        tick4(&car, 12U, CAR_RANGE_VALID, 1000U);
        if (car.left_wheel.value == 0 && previous > 0) zero_since = clock_ms;
        if (car.left_wheel.value < 0) {
            assert(zero_since != 0U);
            assert((uint32_t)(clock_ms - zero_since) >= CAR_REVERSE_DEADTIME_MS);
        }
        previous = car.left_wheel.value;
    }
    assert(car.left_wheel.value == CAR_SHARP_SLOW_DUTY && car.right_wheel.value == CAR_SHARP_FAST_DUTY);
    run4(&car, 260U, 3U);
    assert(car.left_wheel.value == CAR_SHARP_FAST_DUTY && car.right_wheel.value == CAR_SHARP_SLOW_DUTY);
}
static void test_straightens_after_sharp_turn(void)
{
    CarControl car;
    warm(&car);
    run4(&car, 260U, 12U);
    assert(car.left_wheel.value == CAR_SHARP_SLOW_DUTY);
    /* Allow up to 160 ms for sensor debounce and the ramp back to cruise. */
    run4(&car, 160U, 6U);
    assert(car.left_wheel.value == CAR_CRUISE_DUTY);
    assert(car.right_wheel.value == CAR_CRUISE_DUTY);
}
static void test_start_finish_and_marker_noise(void)
{
    CarControl car;
    unsigned i;
    clock_ms = 0U;
    CarControl_Init(&car, clock_ms);
    run4(&car, CAR_STARTUP_MS + 1000U, 0U);
    assert(car.state == CAR_STARTUP && car.marker_count == 0U && car.left_wheel.value == 0);
    run4(&car, 1000U, 6U);
    assert(car.state == CAR_STARTUP);
    /* Noise never qualifies as a start. */
    for (i = 0; i < 30U; ++i) { run4(&car, 10U, 15U); run4(&car, 10U, 0U); }
    assert(car.marker_count == 0U);
    run4(&car, CAR_MARKER_CONFIRM_MS + 50U, 15U);
    assert(car.state == CAR_FOLLOW && car.marker_count == 1U);
    run4(&car, 2000U, 15U);
    assert(car.state == CAR_FOLLOW && car.marker_count == 1U);
    /* Short dropouts on the same physical marker do not rearm it. */
    for (i = 0; i < 10U; ++i) { run4(&car, 50U, 6U); run4(&car, 80U, 15U); }
    assert(car.state == CAR_FOLLOW && car.marker_count == 1U);
    run4(&car, CAR_MARKER_RELEASE_MS + 50U, 0U);
    assert(car.marker_armed);
    run4(&car, 15U, 15U); run4(&car, 30U, 0U);
    assert(car.marker_count == 1U);
    run4(&car, CAR_MARKER_CONFIRM_MS + 50U, 15U);
    assert(car.state == CAR_FINISHED && car.marker_count == 2U);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
    run4(&car, 2000U, 0U); run4(&car, 2000U, 15U);
    assert(car.state == CAR_FINISHED && car.marker_count == 2U);
    assert(car.target_left == 0 && car.target_right == 0);
    start_at(&car, UINT32_MAX - 500U);
    run4(&car, CAR_MARKER_FINISH_LOCKOUT_MS + 50U, 6U);
    run4(&car, CAR_MARKER_CONFIRM_MS + 50U, 15U);
    assert(car.state == CAR_FINISHED);
}
static void test_smoothing_and_fault_stop(void)
{
    CarControl car;
    int16_t previous;
    unsigned i;
    start_at(&car, 0U);
    for (i=0; i<600U; ++i) {
        previous = car.left_wheel.value;
        tick4(&car, 6U, CAR_RANGE_VALID, 1000U);
        assert(car.left_wheel.value >= previous);
        assert(car.left_wheel.value - previous <= CAR_ACCEL_STEP);
    }
    /* No detection remains straight indefinitely, per user specification. */
    run4(&car, 5000U, 0U);
    assert(car.state == CAR_FOLLOW && car.left_wheel.value == CAR_CRUISE_DUTY);
    clock_ms += CAR_CONTROL_MAX_GAP_MS + 1U;
    CarControl_Step(&car, clock_ms);
    assert(car.state == CAR_CONTROL_FAULT && car.left_wheel.value == 0);
    run4(&car, 1000U, 15U);
    assert(car.state == CAR_CONTROL_FAULT);
}
static void test_obstacle_hysteresis(void)
{
    CarControl car;
    /* Test single-sample glitch rejection: 1 sample does not trip */
    warm(&car);
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 150U, clock_ms);
    run(&car, CAR_CONTROL_MS, 1U, 1U, CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_FOLLOW);

    /* Test noise pulse < 40mm rejection */
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 20U, clock_ms);
    run(&car, CAR_CONTROL_MS, 1U, 1U, CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_FOLLOW);

    /* 2 valid samples confirm obstacle */
    warm(&car);
    close_obstacle(&car);
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, CAR_RANGE_STOP_MM + 20U, clock_ms);
    run(&car, 100U, 1U, 1U,
        CAR_RANGE_VALID, CAR_RANGE_STOP_MM + 20U);
    assert(car.state == CAR_OBSTACLE_STOP);
    /* One far reading is insufficient to cancel a bypass already being planned. */
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 1000U, clock_ms);
    assert(car.range_clear_count == 1U);
    run(&car, 30U, 1U, 1U, CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_OBSTACLE_STOP);

    warm(&car);
    close_obstacle(&car);
    run(&car, CAR_BYPASS_STOP_MS + 80U, 1U, 1U,
        CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_FOLLOW);
}

static void test_complete_bypass_and_reverse_dwell(void)
{
    CarControl car;
    uint32_t visited = 0U;
    uint32_t i;
    warm(&car);
    begin_bypass(&car);
    for (i = 0; i < CAR_BYPASS_TOTAL_LIMIT_MS; ++i) {
        uint8_t seeing_line = car.state == CAR_BYPASS_CENTER ||
            (car.state == CAR_BYPASS_SEEK && (uint32_t)(clock_ms - car.state_since_ms) > 700U);
        int16_t l, r;
        /* While turning left or offsetting, car is off the line (0,0). When seeking, detects line (1,0) */
        tick(&car, seeing_line ? 1U : 0U,
             0U, CAR_RANGE_VALID, 1000U);
        visited |= 1UL << car.state;
        l = car.left_wheel.value;
        r = car.right_wheel.value;
        /* All bypass duties are forward differential (>= 0), no reverse */
        assert(l >= 0 && l <= CAR_MAX_DUTY);
        assert(r >= 0 && r <= CAR_MAX_DUTY);
        if (car.bypass_completed && car.state == CAR_FOLLOW) break;
    }
    assert(i < CAR_BYPASS_TOTAL_LIMIT_MS);
    assert(visited & (1UL << CAR_BYPASS_TURN_OUT));
    assert(visited & (1UL << CAR_BYPASS_OFFSET));
    assert(visited & (1UL << CAR_BYPASS_SEEK));
    assert(car.state == CAR_FOLLOW);
    /* Cooldown guard prevents immediate re-trigger on same obstacle */
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 150U, clock_ms);
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 150U, clock_ms + CAR_RANGE_INTERVAL_MS);
    run(&car, CAR_CONTROL_MS, 1U, 1U, CAR_RANGE_VALID, 150U);
    assert(car.state == CAR_FOLLOW);
}

static void test_failed_bypass(void)
{
    CarControl car;
    warm(&car);
    begin_bypass(&car);
    /* Neither sensor ever sees black: a timeout must stop, not drive forever. */
    run(&car, CAR_BYPASS_TOTAL_LIMIT_MS + 100U, 0U, 0U, CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_BLOCKED);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
    run(&car, 1000U, 1U, 1U, CAR_RANGE_VALID, 1000U);
    assert(car.state == CAR_BLOCKED);

    warm(&car);
    begin_bypass(&car);
    while (car.state != CAR_BYPASS_SEEK) tick(&car, 0U, 0U, CAR_RANGE_VALID, 1000U);
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 150U, clock_ms);
    CarControl_ObserveRange(&car, CAR_RANGE_VALID, 150U, clock_ms + CAR_RANGE_INTERVAL_MS);
    run(&car, CAR_CONTROL_MS, 0U, 0U, CAR_RANGE_VALID, 150U);
    assert(car.state == CAR_BLOCKED);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
}

static void test_bypass_aligns_on_all_black(void)
{
    CarControl car;
    uint32_t i;
    warm(&car);
    close_obstacle(&car);
    for (i = 0U; i < CAR_MARKER_CONFIRM_MS + 20U; ++i)
        tick4(&car, 15U, CAR_RANGE_VALID, 150U);
    assert(car.state == CAR_OBSTACLE_STOP && car.marker_count == 1U);
    run(&car, CAR_BYPASS_STOP_MS, 1U, 1U, CAR_RANGE_VALID, 150U);
    assert(car.state == CAR_BYPASS_TURN_OUT);

    run4(&car, CAR_BYPASS_TURN_MS + CAR_BYPASS_OFFSET_MS + CAR_BYPASS_SEEK_IGNORE_MS, 0U);
    assert(car.state == CAR_BYPASS_SEEK);
    assert(car.left_wheel.value == CAR_BYPASS_SEEK_FAST_DUTY);
    assert(car.right_wheel.value == CAR_BYPASS_SEEK_SLOW_DUTY);

    run4(&car, CAR_MARKER_CONFIRM_MS + 20U, 15U);
    assert(car.state == CAR_BYPASS_ALIGN);
    assert(car.marker_count == 1U && !car.marker_armed);
    assert(clock_ms - car.state_since_ms < CAR_REVERSE_DEADTIME_MS);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
    run4(&car, CAR_REVERSE_DEADTIME_MS + CAR_BYPASS_ALIGN_MIN_MS, 15U);
    assert(car.state == CAR_BYPASS_ALIGN);
    assert(car.left_wheel.value == -CAR_BYPASS_ALIGN_DUTY);
    assert(car.right_wheel.value == CAR_BYPASS_ALIGN_DUTY);

    run4(&car, 80U, 8U);
    assert(car.state == CAR_BYPASS_ALIGN);
    run4(&car, CAR_BYPASS_ALIGN_CONFIRM_MS / 2U, 6U);
    assert(car.state == CAR_BYPASS_ALIGN);
    run4(&car, CAR_BYPASS_ALIGN_CONFIRM_MS + 20U, 6U);
    assert(car.state == CAR_BYPASS_SETTLE);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
    run4(&car, CAR_BYPASS_SETTLE_MS, 6U);
    assert(car.state == CAR_FOLLOW && car.bypass_completed);
    assert(car.marker_count == 1U && !car.marker_armed);

    run4(&car, CAR_MARKER_RELEASE_MS + 20U, 6U);
    assert(car.marker_armed);
    run4(&car, CAR_MARKER_CONFIRM_MS + 20U, 15U);
    assert(car.state == CAR_FINISHED && car.marker_count == 2U);
}

static void test_bypass_aligns_on_three_black_after_first_contact(void)
{
    CarControl car;
    warm(&car);
    begin_bypass(&car);
    run4(&car, CAR_BYPASS_TURN_MS + CAR_BYPASS_OFFSET_MS + CAR_BYPASS_SEEK_IGNORE_MS + 20U, 0U);
    assert(car.state == CAR_BYPASS_SEEK);
    run4(&car, CAR_BYPASS_LINE_CONFIRM_MS + 20U, 4U);
    assert(car.state == CAR_BYPASS_CENTER);
    run4(&car, 30U, 14U);
    assert(car.state == CAR_BYPASS_ALIGN);
    run4(&car, CAR_BYPASS_ALIGN_LIMIT_MS, 14U);
    assert(car.state == CAR_BLOCKED);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
}

static void begin_bypass_align(CarControl *car)
{
    warm(car);
    begin_bypass(car);
    run4(car, CAR_BYPASS_TURN_MS + CAR_BYPASS_OFFSET_MS, 0U);
    assert(car->state == CAR_BYPASS_SEEK);
    assert(clock_ms - car->state_since_ms + 50U < CAR_BYPASS_SEEK_IGNORE_MS);
    run4(car, 50U, 7U);
    assert(car->state == CAR_BYPASS_ALIGN);
}

static void test_bypass_searches_forward_if_alignment_loses_line(void)
{
    CarControl car;
    begin_bypass_align(&car);
    run4(&car, CAR_REVERSE_DEADTIME_MS + CAR_BYPASS_ALIGN_MIN_MS + CAR_BYPASS_ALIGN_CONFIRM_MS, 0U);
    assert(car.state == CAR_BYPASS_SETTLE);
    run4(&car, CAR_BYPASS_SETTLE_MS + 120U, 0U);
    assert(car.state == CAR_BYPASS_SEARCH_FORWARD);
    assert(car.target_left == CAR_BYPASS_SEARCH_FORWARD_DUTY);
    assert(car.target_right == CAR_BYPASS_SEARCH_FORWARD_DUTY);
    assert(car.left_wheel.value > 0 && car.left_wheel.value == car.right_wheel.value);
    assert(car.marker_count == 1U && !car.marker_armed);
    run4(&car, CAR_BYPASS_LINE_CONFIRM_MS + 20U, 6U);
    assert(car.state == CAR_FOLLOW && car.bypass_completed);
    assert(car.marker_count == 1U && !car.marker_armed);
}

static void test_bypass_forward_search_times_out_if_line_stays_missing(void)
{
    CarControl car;
    begin_bypass_align(&car);
    run4(&car, CAR_REVERSE_DEADTIME_MS + CAR_BYPASS_ALIGN_MIN_MS, 7U);
    run4(&car, CAR_BYPASS_ALIGN_CONFIRM_MS + 20U, 6U);
    assert(car.state == CAR_BYPASS_SETTLE);
    run4(&car, CAR_BYPASS_SETTLE_MS + 120U, 0U);
    assert(car.state == CAR_BYPASS_SEARCH_FORWARD);
    assert(car.left_wheel.value > 0 && car.left_wheel.value == car.right_wheel.value);
    run4(&car, CAR_BYPASS_SEARCH_FORWARD_MS - (clock_ms - car.state_since_ms) - CAR_CONTROL_MS, 0U);
    assert(car.state == CAR_BYPASS_SEARCH_FORWARD);
    run4(&car, CAR_CONTROL_MS, 0U);
    assert(car.state == CAR_BLOCKED);
    assert(car.left_wheel.value == 0 && car.right_wheel.value == 0);
}

static void test_pulse_conversion(void)
{
    uint16_t mm = 0U;
    assert(!CarControl_DistanceFromPulse(0U, &mm));
    assert(!CarControl_DistanceFromPulse(99U, &mm));
    assert(CarControl_DistanceFromPulse(116U, &mm) && mm == 20U);
    assert(CarControl_DistanceFromPulse(1000U, &mm) && mm == 172U);
    assert(CarControl_DistanceFromPulse(25000U, &mm) && mm == CAR_RANGE_MAX_MM);
    assert(!CarControl_DistanceFromPulse(25001U, &mm));
}


static void test_remote_commands(void)
{
    CarControl car;
    CarControl_Init(&car, 0U);
    CarControl_StartManual(&car, 100U);
    assert(car.state == CAR_MANUAL);
    CarControl_SetManual(&car, 50, 0, 100U);
    CarControl_Step(&car, 105U);
    assert(car.target_left > 0 && car.target_right > 0);
    CarControl_Stop(&car, 110U);
    assert(car.state == CAR_STANDBY);
    CarControl_Step(&car, 115U);
    assert(car.target_left == 0 && car.target_right == 0);
    CarControl_StartAuto(&car, 120U);
    assert(car.state == CAR_FOLLOW && car.race_mode == 0U);
    CarControl_StartRace(&car, 130U);
    assert(car.state == CAR_STARTUP && car.race_mode == 1U);
}

int main(void)
{
    test_remote_commands();
    test_reference_turn_response();
    test_four_probe_truth_table();
    test_start_finish_and_marker_noise();
    test_smoothing_and_fault_stop();
    test_pulse_conversion();
    if (CAR_ULTRASONIC_ENABLED) {
        test_obstacle_hysteresis();
        test_complete_bypass_and_reverse_dwell();
        test_failed_bypass();
        test_bypass_aligns_on_all_black();
        test_bypass_aligns_on_three_black_after_first_contact();
        test_bypass_searches_forward_if_alignment_loses_line();
        test_bypass_forward_search_times_out_if_line_stays_missing();
    }
    test_straightens_after_sharp_turn();
    printf("PASS: four-probe table, start/finish/rearm/noise/wrap, ramps; ultrasonic=%u\n", (unsigned)CAR_ULTRASONIC_ENABLED);
    return 0;
}
