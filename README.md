# STM32F103C8T6 蓝牙循迹小车

STM32 通过 HC-05 接收安卓手机的控制指令，用 TB6612FNG 驱动左右两侧电机。小车支持四路红外循迹、起终点比赛和摇杆遥控，也可以接入 HC-SR04 启用超声波绕障。

## 硬件接线

车头朝前时，左右以车身方向为准。表中的 `PA0`、`PB12` 等是 GPIO 名称，按开发板丝印连接。接线时先断电，所有模块和电源共地。

| 模块 | 模块端子 | STM32 引脚 | 用途 |
| --- | --- | --- | --- |
| HC-05 | RXD | PA9 | USART1_TX，主控发给蓝牙模块 |
| HC-05 | TXD | PA10 | USART1_RX，主控接收指令 |
| 红外循迹：左外 | DO | PB6 | 数字输入 |
| 红外循迹：左内 | DO | PB0 | 数字输入 |
| 红外循迹：右内 | DO | PB1 | 数字输入 |
| 红外循迹：右外 | DO | PB7 | 数字输入 |
| TB6612FNG | PWMA | PA6 | TIM3_CH1，左侧 PWM |
| TB6612FNG | AIN1 / AIN2 | PB12 / PB13 | 左侧电机方向 |
| TB6612FNG | PWMB | PA7 | TIM3_CH2，右侧 PWM |
| TB6612FNG | BIN1 / BIN2 | PB14 / PB15 | 右侧电机方向 |
| TB6612FNG | STBY | PB5 | 高电平使能，低电平待机 |
| HC-SR04（可选） | TRIG | PA1 | 测距触发输出 |
| HC-SR04（可选） | ECHO | PA0 | **经分压**接 TIM2_CH1 |
| ST-Link | SWDIO / SWCLK | PA13 / PA14 | 固件下载与调试，另接 GND |

- **电机驱动：**TB6612FNG 的 `VCC` 接 3.3 V，`VM` 接符合电机额定电压的电源。A 通道的 `AO1/AO2` 接左侧电机，B 通道的 `BO1/BO2` 接右侧电机。四轮版本每侧两台电机并联，先确认该侧的合计启动及堵转电流在驱动模块承受范围内。电机电源不要接到开发板的 3.3 V 引脚。参考 [TB6612FNG 数据手册](https://toshiba.semicon-storage.com/info/TB6612FNG_datasheet_en_20141001.pdf?did=10660&prodName=TB6612FNG)。
- **蓝牙：**TXD/RXD 交叉接线，UART 使用 3.3 V 逻辑，数据模式设为 **9600 波特、8N1、无流控**。`VCC` 按实际 HC-05 底板规格供电；仅在底板明确支持 5 V 输入时接 5 V。使用经典蓝牙 SPP 透传模式。
- **红外：**只接 `DO`，`AO` 不接。模块支持 3.3 V 供电时优先使用 3.3 V；若 `DO` 输出为 5 V，先做电平转换。固件默认黑线为高电平、背景为低电平，极性不同可修改 [car_config.h](Car/Core/Inc/car_config.h)。
- **超声波：**HC-SR04 接 5 V 供电。`ECHO` 串联 10 kΩ 后接 `PA0`，再从 `PA0` 接 15 kΩ 到 GND，将 5 V 回波降到约 3 V。不要把 5 V 的 `ECHO` 直接接到 `PA0`。当前 `CAR_ULTRASONIC_ENABLED=0`，不接超声波也可使用循迹和遥控。参考 [STM32F103C8 数据手册](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)。

## 编译与下载

1. **固件：**用 Keil µVision 5 打开 [Car.uvprojx](Car/MDK-ARM/Car.uvprojx)。工程使用 ARM Compiler 5.06，若提示缺少设备包，安装 `Keil.STM32F1xx_DFP`。按 `F7` 编译。首次下载时在 `Options for Target → Debug` 中选择 ST-Link，接口选 SW；若提示缺少 Flash 算法，在 `Settings → Flash Download` 中添加与 STM32F103C8 对应的片内 Flash 算法。接上 ST-Link 后按 `F8` 下载；Keil 使用生成的 `Car/MDK-ARM/Car/Car.axf`。
2. **安卓 App：**用 Android Studio 打开 [phone_remote](phone_remote/)，配置 JDK 17 和 Android SDK 35。在该目录运行 `./gradlew assembleDebug`（Windows PowerShell 为 `.\gradlew.bat assembleDebug`），将 `app/build/outputs/apk/debug/app-debug.apk` 安装到 Android 8.0 或更高版本的手机。仓库提供源码，不附安装包。
3. **修改引脚：**用 STM32CubeMX 打开 [Car.ioc](Car/Car.ioc)。工程基于 STM32Cube FW_F1 V1.8.6；若重生成时找不到原有固件包，请在 CubeMX 中重新选择本机安装的包。重生成后，确认 Keil 工程仍包含 `car_app.c` 和 `car_control.c`。

## 手机操作

1. 首次上电前先架空车轮。固件默认上电停车，等待手机指令。
2. 在 Android 系统的蓝牙设置里配对 HC-05，再打开「循迹遥控小车」App。按提示授予蓝牙权限，点击「连接蓝牙」，从已配对设备中选择 HC-05。该 App 使用经典蓝牙 SPP。
3. 连接后先选「手动遥控模式」，轻推摇杆向前，确认左右车轮都向前转。同侧某台电机方向相反时，断电后交换该电机的两根线。架空验证「一键停车」后，再下地尝试其他模式。

| App 按钮 | 小车行为 |
| --- | --- |
| 自动寻线模式 | 立即开始按四路红外输入差速循迹。四路均未检测到黑线时会继续直行。 |
| 比赛模式（起终点） | 等待第一条能同时覆盖四个探头的横向黑线；四路全黑确认后起跑。驶离起点且起跑满 2 秒后，第二次确认四路全黑即停车。再次选择比赛模式可开始下一轮。 |
| 手动遥控模式 | 拖动摇杆前进、后退或转向，松手归零。固件约 500 ms 未收到摇杆指令时将目标速度清零。 |
| 一键停车（STOP） | 撤销电机驱动并回到待机；车轮可能因惯性继续转动一小段。 |

**结束运行前先按 STOP，再断开蓝牙。** 自动寻线和比赛模式在蓝牙断开后可继续运行；手动模式的指令超时保护不适用于这两种模式。

App 的直行和转弯滑块会调整手动摇杆的输出幅度。当前固件没有解析 App 发送的 `SET_PWM`、`SET_TURN_PWM` 指令；需要调整自动循迹速度时，修改 [car_config.h](Car/Core/Inc/car_config.h) 中的 `CAR_CRUISE_DUTY`、`CAR_TURN_*_DUTY` 或 `CAR_SHARP_*_DUTY` 后重新烧录。固件占空比以千分比表示，例如 `700` 表示 70%。

### 用串口直接控制

也可以用支持经典蓝牙 SPP 的串口终端发送 ASCII 指令，每条以换行结束：`MODE,AUTO`、`MODE,RACE`、`MODE,MANUAL`、`STOP`。手动模式下以低于 500 ms 的间隔重复发送 `MANUAL,<前后>,<转向>`，两字段各用 `-100` 到 `100`；例如 `MANUAL,25,0` 表示向前直行，`MANUAL,0,0` 表示摇杆归零。

## 调试与测试

- **验证循迹输入：**在 Keil Watch 查看 `g_car_debug.line_mask`，用黑线依次触发左外、左内、右内、右外探头，对应位图应为 `0x08`、`0x04`、`0x02`、`0x01`。比赛模式下，`marker_count` 从 `0` 变 `1` 表示起跑，变 `2` 表示结束。
- **启用避障：**按上表接好 HC-SR04，再将 [car_config.h](Car/Core/Inc/car_config.h) 中的 `CAR_ULTRASONIC_ENABLED` 改为 `1U`，编译并下载。循迹时，约 30 cm 范围内连续两次有效测距会触发绕障。小车先停，再向左绕行，随后向右找线；场地、车速和电池电压会影响绕行距离，需要实车调试。
- **运行主机测试：**安装 GCC 并将其加入 `PATH`，在仓库根目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_control.ps1`。测试验证循迹、比赛标志、电机输出和绕障逻辑，不依赖实车。

固件入口在 [main.c](Car/Core/Src/main.c)，GPIO、PWM 和串口指令由 [car_app.c](Car/Core/Src/car_app.c) 接入，运行模式与车轮输出由 [car_control.c](Car/Core/Src/car_control.c) 控制。HAL 和 CMSIS 依赖及其许可文件位于 [Car/Drivers](Car/Drivers/)。
