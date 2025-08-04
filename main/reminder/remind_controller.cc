#include "remind_controller.h"
#include "settings.h"
#include "reminder/alarm.h"
#include <esp_log.h>
#include <cJSON.h> 
#include <cstring>   // strncpy

#define TAG "RemindController"

RemindController::RemindController() {
  auto &mcp_server = McpServer::GetInstance();
  
  // //获取提醒开关状态
  // mcp_server.AddTool(
  //     "self.reminder.get_state", "Get the enable state of the reminder",
  //     PropertyList(), [this](const PropertyList &properties) -> ReturnValue {
  //       Settings settings("reminder");
  //       int enable = settings.GetInt("enable", 0);
  //       return enable ? "{\"enable\": true}" : "{\"enable\": false}";
  //     });

  // // 启用提醒
  // mcp_server.AddTool("self.reminder.enable", "enable the reminder",
  //                    PropertyList(),
  //                    [this](const PropertyList &properties) -> ReturnValue {
  //                      Settings settings("reminder");
  //                      settings.SetInt("enable", 1);
  //                      return true;
  //                    });

  // // 禁用提醒
  // mcp_server.AddTool("self.reminder.disable", "disable the reminder",
  //                    PropertyList(),
  //                    [this](const PropertyList &properties) -> ReturnValue {
  //                      Settings settings("reminder");
  //                      settings.SetInt("enable", 0);
  //                      return true;
  //                    });

  // 添加提醒
  mcp_server.AddTool(
      "self.reminder.add_remind",
      "Add remind of the reminder. you need know current time. If you can't determine the minutes for the reminder, you need to ask the user\n"
      "year: default current year\n"
      "month: default current month\n"
      "day: default current day\n"
      "hour: default current hour\n"
      "minute: the minutes for the reminder\n"
      "content: default empty\n"
      "repeat: 0=once,1=daily,2=weekly,3=monthly,4=yearly (default 0)\n"
      "type: 0=general reminder, 1=wake-up reminder, 2=bedtime reminder (default 0)",
      PropertyList({Property("year", kPropertyTypeInteger, 0, 0, 2099),  // 0表示默认值
                    Property("month", kPropertyTypeInteger, 0, 0, 12),
                    Property("day", kPropertyTypeInteger, 0, 0, 31),
                    Property("hour", kPropertyTypeInteger, 0, 0, 23),   
                    Property("minute", kPropertyTypeInteger, 0, 59),  
                    Property("content", kPropertyTypeString, "提醒"),
                    Property("repeat", kPropertyTypeInteger, 0, 0, 4),
                    Property("type", kPropertyTypeInteger, 0, 0, 2)}),
      [this](const PropertyList &properties) -> ReturnValue {
        // 获取当前时间
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        
        // 处理带默认值的参数
        int year = properties["year"].value<int>();
        int month = properties["month"].value<int>();
        int day = properties["day"].value<int>();
        int hour = properties["hour"].value<int>();
        int minute = properties["minute"].value<int>();
        std::string content = properties["content"].value<std::string>();
        int repeat = properties["repeat"].value<int>();
        int type = properties["type"].value<int>();


        // 设置默认值（未提供时使用当前时间）
        if(year <= 0) year = tm_info->tm_year + 1900;
        if(month <= 0) month = tm_info->tm_mon + 1; // tm_mon范围0-11
        if(day <= 0) day = tm_info->tm_mday;
        if(hour < 0) hour = tm_info->tm_hour;        // 小时范围0-23
        if(minute < 0) minute = tm_info->tm_min;     // 分钟范围0-59

        // 转换重复类型
        static const RepeatType repeatTypes[] = {
            REPEAT_ONCE, REPEAT_DAILY, REPEAT_WEEKLY, REPEAT_MONTHLY, REPEAT_YEARLY
        };
        RepeatType repeatType = repeatTypes[repeat % 5];

        // 创建提醒事件
        alarm_event_t alarm_event = {
            .year = static_cast<uint16_t>(year),
            .month = static_cast<uint8_t>(month),
            .day = static_cast<uint8_t>(day),
            .hour = static_cast<uint8_t>(hour),
            .minute = static_cast<uint8_t>(minute),
            .repeat = repeatType,
            .type = static_cast<uint8_t>(type),
        };
        strncpy(alarm_event.content, content.c_str(), sizeof(alarm_event.content) - 1);
        alarm_event.content[sizeof(alarm_event.content) - 1] = '\0';  // 确保终止符

        // 添加提醒
        esp_err_t result = alarm_add(&alarm_event);
        
        if(result == ESP_OK) {
            ESP_LOGI(TAG, "Added reminder: %d-%02d-%02d %02d:%02d | %s | Repeat:%d", 
                    year, month, day, hour, minute, content.c_str(), repeat);
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to add reminder: %s", esp_err_to_name(result));
            return false;
        }
      });

  // 查询所有提醒
  mcp_server.AddTool(
      "self.reminder.get_all", "Get all reminders",
      PropertyList(),
      [this](const PropertyList &properties) -> ReturnValue {
        cJSON *root = cJSON_CreateArray();
        uint8_t count = alarm_get_count();
        alarm_event_t *alarms = alarm_get_alarms();
        
        for(int i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", alarms[i].id);
            cJSON_AddNumberToObject(item, "year", alarms[i].year);
            cJSON_AddNumberToObject(item, "month", alarms[i].month);
            cJSON_AddNumberToObject(item, "day", alarms[i].day);
            cJSON_AddNumberToObject(item, "hour", alarms[i].hour);
            cJSON_AddNumberToObject(item, "minute", alarms[i].minute);
            cJSON_AddNumberToObject(item, "repeat", alarms[i].repeat);
            cJSON_AddNumberToObject(item, "type", alarms[i].type);
            cJSON_AddStringToObject(item, "content", alarms[i].content);
            
            // 计算下次触发时间
            struct tm next_tm = *localtime(&alarms[i].next_trigger);
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &next_tm);
            cJSON_AddStringToObject(item, "next_trigger", time_str);
            
            cJSON_AddItemToArray(root, item);
        }
        
        char *json_str = cJSON_PrintUnformatted(root);
        std::string result(json_str);
        free(json_str);
        cJSON_Delete(root);
        
        return result;
      });

  // 删除提醒
  mcp_server.AddTool(
      "self.reminder.remove", "Remove a reminder by ID",
      PropertyList({Property("id", kPropertyTypeInteger, 0)}),
      [this](const PropertyList &properties) -> ReturnValue {
        int id = properties["id"].value<int>();
        uint8_t count = alarm_get_count();
        
        if(id < 0) {
            ESP_LOGE(TAG, "Invalid reminder ID: %d (total: %d)", id, count);
            return false;
        }
        
        esp_err_t result = alarm_remove(id);
        if(result == ESP_OK) {
            ESP_LOGI(TAG, "Removed reminder ID: %d", id);
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to remove reminder ID: %d: %s", id, esp_err_to_name(result));
            return false;
        }
      });

  // 清除所有提醒 
  mcp_server.AddTool(
      "self.reminder.clear_all", "Remove all reminders",
      PropertyList(),
      [this](const PropertyList &properties) -> ReturnValue {
        uint8_t count = alarm_get_count();
        // 直接重置闹钟系统状态
        alarm_clear_all();
        ESP_LOGI(TAG, "Cleared all reminders (%d removed)", count);
        return true;
      });
}
