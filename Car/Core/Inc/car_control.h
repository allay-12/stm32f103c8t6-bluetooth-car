#ifndef CAR_CONTROL_H
#define CAR_CONTROL_H

#include <stdint.h>

typedef enum {
    CAR_STARTUP = 0,
    CAR_FOLLOW,
    CAR_OBSTACLE_STOP,
    CAR_BYPASS_TURN_OUT,
    CAR_BYPASS_OFFSET,
    CAR_BYPASS_FACE_FORWARD,
    CAR_BYPASS_PASS,
    CAR_BYPASS_FACE_LINE,
    CAR_BYPASS_SEEK,
    CAR_BYPASS_CENTER,
    CAR_BYPASS_ALIGN,
    CAR_BYPASS_SETTLE,
    CAR_LINE_LOST,
    CAR_RANGE_FAULT,
    CAR_BLOCKED,
    CAR_CONTROL_FAULT,
    CAR_MOTOR_TEST,
    CAR_MOTOR_TEST_DONE,
    CAR_FINISHED,
    CAR_STANDBY,
    CAR_MANUAL,
    CAR_BYPASS_SEARCH_FORWARD
} CarState;

typedef enum {
    CAR_RANGE_NONE = 0,
    CAR_RANGE_VALID,
    CAR_RANGE_NO_ECHO,
    CAR_RANGE_BAD_ECHO,
    CAR_RANGE_DISABLED
} CarRangeStatus;

typedef struct {
    uint8_t candidate;
    uint8_t count;
    uint8_t stable;
} CarDebounce;

typedef struct {
    int16_t value;
    int8_t last_sign;
    uint32_t zero_since_ms;
} CarWheelRamp;

typedef struct {
    CarState state;
    CarRangeStatus range_status;
    CarDebounce left_line;
    CarDebounce right_line;
    CarDebounce left_outer_line;
    CarDebounce right_outer_line;
    CarWheelRamp left_wheel;
    CarWheelRamp right_wheel;
    uint32_t boot_ms;
    uint32_t state_since_ms;
    uint32_t last_step_ms;
    uint32_t range_since_ms;
    uint32_t bypass_since_ms;
    uint32_t bypass_end_ms;
    uint32_t steer_since_ms;
    uint32_t missing_since_ms;
    uint32_t confirm_since_ms;
    uint32_t race_start_ms;
    uint32_t marker_since_ms;
    uint32_t marker_release_since_ms;
    uint32_t manual_since_ms;
    int16_t manual_left;
    int16_t manual_right;
    uint8_t marker_timing;
    uint8_t marker_release_timing;
    uint8_t marker_armed;
    uint8_t marker_count;
    uint8_t race_mode;
    uint8_t line_mask;
    uint16_t range_mm;
    int16_t error_q8;
    int16_t target_left;
    int16_t target_right;
    int8_t last_line_direction;
    uint8_t range_good_count;
    uint8_t range_clear_count;
    uint8_t range_obstacle_count;
    uint8_t line_sample_count;
    uint8_t steer_active;
    uint8_t missing_active;
    uint8_t confirm_active;
    uint8_t bypass_completed;
} CarControl;

void CarControl_Init(CarControl *car, uint32_t now_ms);
/* Normalized detection (1=triggered). Order: outer-left, inner-left,
 * inner-right, outer-right. The 4-bit mask uses this order, MSB first. */
void CarControl_SampleLine4(CarControl *car, uint8_t outer_left, uint8_t left,
                            uint8_t right, uint8_t outer_right);
void CarControl_ObserveRange(CarControl *car, CarRangeStatus status,
                             uint16_t distance_mm, uint32_t now_ms);
void CarControl_Step(CarControl *car, uint32_t now_ms);
void CarControl_Trip(CarControl *car, uint32_t now_ms);
uint8_t CarControl_DistanceFromPulse(uint16_t pulse_us, uint16_t *distance_mm);

/* Bluetooth and Remote Control Interface */
void CarControl_StartAuto(CarControl *car, uint32_t now);
void CarControl_StartRace(CarControl *car, uint32_t now);
void CarControl_StartManual(CarControl *car, uint32_t now);
void CarControl_SetManual(CarControl *car, int16_t forward_pct, int16_t turn_pct, uint32_t now);
void CarControl_Stop(CarControl *car, uint32_t now);

#endif
