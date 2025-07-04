#include "alarm.h"
#include "esp_log.h"
#include <cstring>
#include "application.h"
#include "driver/rtc_io.h"
#include <time.h>
#include <sys/time.h>

#define NVS_NAMESPACE "alarm"

static alarm_event_t alarms[MAX_ALARMS];
static uint8_t alarm_count = 0;
static uint16_t next_id = 0;  // ID计数器
static const char* TAG = "Alarm";

// 日期校验（含闰年判断）
bool is_valid_date(alarm_event_t *ev) {
    if (ev->year < 1970 || ev->year > 2100) return false;
    if (ev->month < 1 || ev->month > 12) return false;
    if (ev->day < 1 || ev->day > 31) return false;
    
    if (ev->month == 2) {
        int feb_days = (ev->year % 4 == 0 && (ev->year % 100 != 0 || ev->year % 400 == 0)) ? 29 : 28;
        if (ev->day > feb_days) return false;
    } else if (ev->month == 4 || ev->month == 6 || ev->month == 9 || ev->month == 11) {
        if (ev->day > 30) return false;
    }
    return true;
}

// 计算下次触发时间
static time_t calculate_next_trigger(alarm_event_t *ev) {
    struct tm next_tm;
    time_t now = time(NULL);
    localtime_r(&ev->next_trigger, &next_tm);
    
    switch (ev->repeat) {
        case REPEAT_DAILY:
            next_tm.tm_mday++;
            break;
        case REPEAT_WEEKLY:
            next_tm.tm_mday += 7;
            break;
        case REPEAT_MONTHLY:
            next_tm.tm_mon++;
            if (next_tm.tm_mon > 11) {
                next_tm.tm_mon = 0;
                next_tm.tm_year++;
            }
            break;
        case REPEAT_YEARLY:
            next_tm.tm_year++;
            break;
        default: return 0; // 单次闹钟不更新
    }
    
    time_t next = mktime(&next_tm);
    return (next > now) ? next : 0; // 确保是未来的时间
}

// 初始化RTC和闹钟
void alarm_init() {
    // 从NVS加载闹钟
    alarm_load_from_nvs();
}

// 添加闹钟
esp_err_t alarm_add(alarm_event_t *event) {
    if (alarm_count >= MAX_ALARMS) return ESP_ERR_NO_MEM;
    if (!is_valid_date(event)) return ESP_ERR_INVALID_ARG; // 日期校验

    // 分配唯一ID
    event->id = next_id++;
    if (next_id == UINT16_MAX) next_id = 0; // 达到最大值回绕处理

    // 设置初始触发时间
    struct tm trigger_tm = {
        .tm_sec = 0,        // 秒（必须首位）
        .tm_min = event->minute,
        .tm_hour = event->hour,
        .tm_mday = event->day,     // 必须在 tm_mon 前
        .tm_mon = event->month - 1,
        .tm_year = event->year - 1900
    };
    
    time_t now = time(NULL);
    event->next_trigger = mktime(&trigger_tm);
    
    // 如果闹钟时间是过去的，调整到下个周期
    if (event->next_trigger < now && event->repeat != REPEAT_ONCE) {
        event->next_trigger = calculate_next_trigger(event);
        if (event->next_trigger < now) {
            ESP_LOGE(TAG, "Failed to set future trigger time");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    alarms[alarm_count++] = *event;
    alarm_save_to_nvs();
    alarm_update_next_wakeup();
    return ESP_OK;
}

// 删除闹钟（通过ID）
esp_err_t alarm_remove(uint16_t id) {
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].id == id) {
            // 向前移动数组
            memmove(&alarms[i], &alarms[i+1], (alarm_count - i - 1) * sizeof(alarm_event_t));
            alarm_count--;
            alarm_save_to_nvs();
            alarm_update_next_wakeup();
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

// 清除所有提醒
void alarm_clear_all() {
    // 1. 清空内存数据
    alarm_count = 0;
    memset(alarms, 0, sizeof(alarms)); 

    // 2. 物理擦除NVRAM
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    // 3. 同步清理RTC闹钟
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    ESP_LOGI(TAG, "All alarms cleared");
}

void handle_alarm_task(alarm_event_t *alarm) {
    ESP_LOGI(TAG, "Alarm triggered: %s", alarm->content);

    //如果能连接到服务器
    //将提醒内容发给服务器，让大模型生成优化后的提醒内容，返回给客户端
    auto& app = Application::GetInstance();
    app.ToggleChatState();
    
    std::string payload = "{\"type\":";
    payload += std::to_string(alarm->type) + ",\"content\":\"";
    payload += alarm->content;
    payload += "\"}";
    
    ESP_LOGI(TAG, "Payload: %s", payload.c_str());
    app.SendReminderMessage(payload);

    //如果不能连接到服务器，本地默认铃声提醒
}

// 检查并触发闹钟
void alarm_check_trigger() {
    time_t now = time(NULL);
    bool needs_update = false;
    
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].next_trigger <= now) {
            handle_alarm_task(&alarms[i]);
            
            // 更新下次触发时间
            time_t next = calculate_next_trigger(&alarms[i]);
            if (next > 0) {
                alarms[i].next_trigger = next;
            } else {
                alarm_remove(alarms[i].id);
                i--;  // 调整索引
            }
            needs_update = true;
        }
    }
    
    if (needs_update) {
        alarm_save_to_nvs();
        alarm_update_next_wakeup();
    }
}

// 更新下一次唤醒时间
void alarm_update_next_wakeup() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    
    time_t min_trigger = 0;
    time_t now = time(NULL);
    
    // 找到最近的闹钟
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].next_trigger > now && 
            (min_trigger == 0 || alarms[i].next_trigger < min_trigger)) {
            min_trigger = alarms[i].next_trigger;
        }
    }
    
    // 设置唤醒时间
    if (min_trigger > 0) {
        time_t delta = min_trigger - now;
        ESP_LOGI(TAG, "Setting wakeup in %lld seconds", delta);
        esp_sleep_enable_timer_wakeup(delta * 1000000);
    } else {
        ESP_LOGI(TAG, "No upcoming alarms");
    }
}

// 获取闹钟数量
uint8_t alarm_get_count() {
    return alarm_count;
}

alarm_event_t* alarm_get_alarms() {
    return alarms;
}

// 保存闹钟到NVS
void alarm_save_to_nvs() {
    nvs_handle_t handle;
    esp_err_t err;
    
    if ((err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    
    // 保存闹钟数组
    if ((err = nvs_set_blob(handle, "alarms", alarms, sizeof(alarm_event_t) * alarm_count)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alarms: %s", esp_err_to_name(err));
    }
    
    // 保存ID计数器
    if ((err = nvs_set_u16(handle, "next_id", next_id)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save next_id: %s", esp_err_to_name(err));
    }
    
    // 保存闹钟数量
    if ((err = nvs_set_u8(handle, "alarm_count", alarm_count)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alarm_count: %s", esp_err_to_name(err));
    }
    
    nvs_commit(handle);
    nvs_close(handle);
}

// 从NVS加载闹钟
void alarm_load_from_nvs() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        // 加载闹钟数量
        uint8_t count = 0;
        if (nvs_get_u8(handle, "alarm_count", &count) == ESP_OK) {
            alarm_count = count;
        }
        
        // 加载ID计数器
        uint16_t id_counter = 0;
        if (nvs_get_u16(handle, "next_id", &id_counter) == ESP_OK) {
            next_id = id_counter;
        }
        
        // 加载闹钟数据
        size_t length = sizeof(alarm_event_t) * MAX_ALARMS;
        if (nvs_get_blob(handle, "alarms", alarms, &length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to load alarms from NVS");
            alarm_count = 0;
            next_id = 0;
        }
        nvs_close(handle);
    } else {
        ESP_LOGW(TAG, "No alarm data found in NVS");
    }
}