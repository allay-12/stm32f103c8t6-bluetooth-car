#ifndef CAR_APP_H
#define CAR_APP_H

#include "car_control.h"

/* Inspect this variable in Keil Watch; no blocking printf or Bluetooth needed. */
typedef struct {
    uint32_t uptime_ms;
    CarState state;
    CarRangeStatus range_status;
    uint16_t distance_mm;
    uint16_t echo_us;
    uint16_t max_poll_gap_ms;
    int16_t target_left;
    int16_t target_right;
    int16_t duty_left;
    int16_t duty_right;
    uint8_t raw_left_level;
    uint8_t raw_right_level;
    uint8_t left_black;
    uint8_t right_black;
    uint8_t raw_left_outer_level;
    uint8_t raw_right_outer_level;
    uint8_t left_outer_black;
    uint8_t right_outer_black;
    uint8_t line_mask;
    uint8_t marker_count;
    uint8_t marker_armed;
} CarDebug;

extern volatile CarDebug g_car_debug;
void CarApp_Init(void);
void CarApp_Poll(void);
/* No HAL calls: also safe in fault/exception handlers. */
void CarApp_EmergencyStop(void);
void CarApp_ProcessCommand(const char *cmd);

#endif
