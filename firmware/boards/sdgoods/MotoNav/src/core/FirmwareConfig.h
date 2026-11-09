#pragma once
// 版本、诊断选项及路口灯状态，保持原先位于 Arduino.h 之前的顺序。
#define FW_VERSION "v7.28.0-sdgoods"
// v7.25 诊断开关（二分测试）：置 1 关闭 HID 媒体服务(0x1812) 与 GENERIC_HID 广播/外观，
// 用于验证 iOS 上 "带 HID 外观的设备被系统当成 HID 外设接管 GATT，导致 App 无法发现/订阅
// Nordic UART 服务" 的假设。若关闭后 App 立刻能控制圆屏，即坐实 HID 抢占是根因。
// 验证完成后改回 0 即可恢复 HID 音乐控制。
#define MOTONAV_NO_HID 1
#include "../../TrafficLightLab.h"
TrafficLightLab trafficLightLab;
TrafficLightFrame trafficLightFrame;
uint32_t trafficLightPreviewUntil=0;
#define FPS_SELFTEST 0
#define FPS_SERIAL_DIAGNOSTICS 1

// 调试时想在串口监视器里看到每条收到的文字指令，把 0 改成 1
#define DEBUG_SERIAL 0

// 导航中超过这么久没收到任何消息（含心跳），就提示数据中断
#define STALE_MS 6000

// 三阶段的分界距离（米），可按需要调整
#define STAGE_FAR_M  500   // 超过这个距离：远处预告画面
#define STAGE_NEAR_M 100   // 小于等于这个距离：橙色警示
