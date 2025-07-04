#pragma once
#include <time.h>
#include "esp_sleep.h"          // 深度睡眠与唤醒API
#include "nvs_flash.h"
#include "nvs.h"

#define MAX_ALARMS 10


// 闹钟重复类型
typedef enum {
    REPEAT_ONCE = 0,  // 单次提醒（精确到年月日）
    REPEAT_DAILY,     // 每日重复
    REPEAT_WEEKLY,    // 每周重复（如每周一）
    REPEAT_MONTHLY,   // 每月重复（如每月5号）
    REPEAT_YEARLY     // 每年重复（如每年生日）
} RepeatType;

//提醒类型
typedef enum {
    REMIND_GENERAL = 0,  // 普通提醒
    REMIND_WAKEUP,       // 起床闹钟提醒
    REMIND_BEDTIME       // 睡觉提醒
} RemindType; 

//提醒方式
// typedef enum {
//     METHOD_ANNOUNCEMENT = 0,     // 语音播报
//     METHOD_RINGTONE,             // 铃声
//     METHOD_MUSIC                 // 音乐
// } RemindMethod;

// 闹钟结构体（支持日期）
typedef struct {
    uint16_t id;          // 闹钟唯一ID（从0开始）
    uint16_t year;        // 年份（2025起）
    uint8_t month;        // 月份（1-12）
    uint8_t day;          // 日期（1-31）
    uint8_t hour;         // 小时（0-23）
    uint8_t minute;       // 分钟（0-59）
    RepeatType repeat;    // 重复类型
    uint8_t type;         // 提醒类型
    char content[32];     // 事件内容（如“开会”）
    time_t next_trigger;  // 下次触发时间戳（由系统计算）
} alarm_event_t;

// 闹钟管理函数
void alarm_init();
esp_err_t alarm_add(alarm_event_t *event);
esp_err_t alarm_remove(uint16_t id);
uint8_t alarm_get_count();
alarm_event_t* alarm_get_alarms();
void alarm_clear_all();
void alarm_update_next_wakeup();
void alarm_save_to_nvs();
void alarm_load_from_nvs();
void alarm_check_trigger();  // 在主循环中每秒调用
