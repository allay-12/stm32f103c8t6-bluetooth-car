#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* All motor commands are duty per mille, not measured wheel speed. */
/* Confirmed on this car: black = HIGH / LED off, background = LOW / LED on. */
#define CAR_LEFT_BLACK_LEVEL           1U
#define CAR_RIGHT_BLACK_LEVEL          1U
/* Four sensors, from left to right: PB6, PB0, PB1, PB7. */
#define CAR_LEFT_OUTER_BLACK_LEVEL     1U
#define CAR_RIGHT_OUTER_BLACK_LEVEL    1U
/* 1: detected means black; 0: detected means background / output LED on. */
#define CAR_TRACK_DETECT_BLACK        1U
/* 1: power-on starts in standby (stopped), waiting for phone app command.
 * 0: auto-run after startup delay (legacy mode without phone). */
#ifndef CAR_POWERON_STANDBY
#define CAR_POWERON_STANDBY           1U
#endif

/* 0: auto-run after startup delay; 1: first ALL starts, second ALL finishes. */
#ifndef CAR_RACE_MARKERS_ENABLED
#define CAR_RACE_MARKERS_ENABLED      0U
#endif
#define CAR_MARKER_CONFIRM_MS         30U
#define CAR_MARKER_RELEASE_MS         150U
/* 启动起跑后至少行驶2000ms（2秒），才允许再次识别终点线全黑，防止起跑初期的弯道误判 */
#ifndef CAR_MARKER_FINISH_LOCKOUT_MS
#define CAR_MARKER_FINISH_LOCKOUT_MS  2000U
#endif
/* 寻线差速转弯参数：改变左右轮速度差实现平滑转向，禁止一正一反（双轮均正转前进） */
#define CAR_TURN_SLOW_DUTY            250
#define CAR_TURN_FAST_DUTY            740
#define CAR_SHARP_SLOW_DUTY           100
#define CAR_SHARP_FAST_DUTY           900
/* Bench test only, with wheels lifted: 0=automatic, 1=left forward,
 * 2=right forward, 3=both forward. Runs once, then stays stopped until reset.
 * Sensors do not gate motor operation in this explicit test mode. */
#ifndef CAR_MOTOR_TEST_MODE
#define CAR_MOTOR_TEST_MODE            0							//0����ѭ����1ת���֣�2ת����
#endif
#define CAR_MOTOR_TEST_DUTY            450						//PWMռ�ձȣ�Խ���ٶ�Խ��
#define CAR_MOTOR_TEST_RUN_MS          1500U
#define CAR_MOTOR_TEST_RAMP_DOWN_MS    150U
#define CAR_LEFT_FORWARD_IN1           1U
#define CAR_RIGHT_FORWARD_IN1          1U
#define CAR_LEFT_TRIM_PERMILLE         1000U
#define CAR_RIGHT_TRIM_PERMILLE        1000U

#define CAR_LINE_SAMPLE_MS             2U
#define CAR_LINE_STABLE_SAMPLES        4U
#define CAR_CONTROL_MS                 5U
#define CAR_CONTROL_MAX_GAP_MS         50U
#define CAR_STARTUP_MS                 2000U
/* Nonzero motion targets >= 40%; ramps may pass below this during transitions. */
#define CAR_MIN_RUN_DUTY               100
#define CAR_CRUISE_DUTY                700
#define CAR_CROSSING_DUTY              400
#define CAR_MAX_DUTY                   1000
#define CAR_ACCEL_STEP                 8
#define CAR_DECEL_STEP                 16
/* Faster response during turns and while equalizing wheel duties after a turn. */
#define CAR_TURN_ACCEL_STEP            25
#define CAR_TURN_DECEL_STEP            50
#define CAR_REVERSE_DEADTIME_MS        60U

/* 0: line following only; 1: ultrasonic ranging and obstacle bypass. */
#ifndef CAR_ULTRASONIC_ENABLED
// #define CAR_ULTRASONIC_ENABLED     1U
#define CAR_ULTRASONIC_ENABLED        0U
#endif

#define CAR_RANGE_INTERVAL_MS          65U
#define CAR_RANGE_TIMEOUT_MS           30U
#define CAR_RANGE_STALE_MS             250U
#define CAR_RANGE_READY_SAMPLES        3U
#define CAR_RANGE_CONFIRM_SAMPLES      2U
#define CAR_RANGE_MIN_VALID_MM         40U
#define CAR_RANGE_MAX_MM               4000U
#define CAR_RANGE_STOP_MM              300U
#define CAR_RANGE_RELEASE_MM           400U
#define CAR_RANGE_SLOW_MM              500U
/* HC-SR04 silence can mean out of range OR disconnected. See tuning notes.
 * Set to 0 for strict stop-on-no-echo operation. A stuck-high echo is a fault. */
#ifndef CAR_NO_ECHO_IS_CLEAR
#define CAR_NO_ECHO_IS_CLEAR           1U
#endif

/* 避障状态机参数配置：
 * 1. 检测到前方约30cm（300mm内连续2帧确认）障碍物，停车缓冲200ms消减冲力
 * 2. 差速左转避障1.2秒（1200ms，左轮200/右轮800，双轮正转）
 * 3. 直走0.5秒（500ms，双轮500正转）错开障碍物侧边
 * 4. 差速右转弯寻找黑线（左轮700/右轮200，双轮正转）
 * 5. 回线时三路或四路黑线触发原地左转；中间探头对准后恢复循迹和终点检测
 * 6. 左转后四路都离开黑线时，低速直行最多0.5秒找线
 */
#define CAR_BYPASS_STOP_MS             200U
#define CAR_BYPASS_TURN_MS             1200U
#define CAR_BYPASS_OFFSET_MS           500U
#define CAR_BYPASS_SEEK_LIMIT_MS       5000U
#define CAR_BYPASS_SEEK_IGNORE_MS      600U
#define CAR_BYPASS_LINE_CONFIRM_MS     20U
#define CAR_BYPASS_TOTAL_LIMIT_MS      15000U
#define CAR_BYPASS_REPEAT_GUARD_MS     1500U

/* 差速转向PWM（禁止反转，双轮均为正转前进） */
#define CAR_BYPASS_TURN_SLOW_DUTY      200
#define CAR_BYPASS_TURN_FAST_DUTY      800
#define CAR_BYPASS_DRIVE_DUTY          500
#define CAR_BYPASS_SEEK_SLOW_DUTY      200
#define CAR_BYPASS_SEEK_FAST_DUTY      700
/* 左轮反转、右轮正转；两侧停车等待换向保护后再同时起转。 */
#define CAR_BYPASS_ALIGN_DUTY           350
#define CAR_BYPASS_ALIGN_MIN_MS         120U
#define CAR_BYPASS_ALIGN_CONFIRM_MS     40U
#define CAR_BYPASS_ALIGN_LIMIT_MS       1500U
#define CAR_BYPASS_SEARCH_FORWARD_DUTY  350
#define CAR_BYPASS_SEARCH_FORWARD_MS    500U

/* 兼容性宏定义 */
#define CAR_BYPASS_TURN_DUTY           CAR_BYPASS_TURN_FAST_DUTY
#define CAR_BYPASS_PASS_MS             CAR_BYPASS_OFFSET_MS
#define CAR_BYPASS_SEEK_DUTY           CAR_BYPASS_SEEK_FAST_DUTY
#define CAR_BYPASS_CENTER_DUTY         CAR_BYPASS_DRIVE_DUTY
#define CAR_BYPASS_CENTER_MS           300U
#define CAR_BYPASS_RAMP_DOWN_MS        100U
#define CAR_BYPASS_SETTLE_MS           100U

#if CAR_RANGE_RELEASE_MM <= CAR_RANGE_STOP_MM
#error Range release threshold must exceed stop threshold
#endif
#if CAR_RANGE_INTERVAL_MS <= CAR_RANGE_TIMEOUT_MS
#error Ultrasonic measurement interval must exceed its timeout
#endif
#if CAR_MIN_RUN_DUTY <= 0 || CAR_MIN_RUN_DUTY > CAR_CRUISE_DUTY
#error Invalid minimum running duty
#endif
#if CAR_MAX_DUTY > 1000 || CAR_MAX_DUTY < CAR_CRUISE_DUTY
#error Invalid maximum motor duty
#endif
#if CAR_LINE_STABLE_SAMPLES < 1 || CAR_RANGE_READY_SAMPLES < 1
#error Sensor qualification counts must be positive
#endif
#if CAR_MOTOR_TEST_MODE > 3 || CAR_MOTOR_TEST_DUTY > CAR_MAX_DUTY || CAR_MOTOR_TEST_DUTY <= 0
#error Invalid motor bench test configuration
#endif
#if CAR_MOTOR_TEST_RUN_MS <= CAR_MOTOR_TEST_RAMP_DOWN_MS
#error Motor bench test needs time to move before ramping down
#endif

#endif
