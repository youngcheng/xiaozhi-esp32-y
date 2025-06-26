#ifndef LAMP_CONTROLLER_H
#define LAMP_CONTROLLER_H

#include "lamp_controller.h"

#include <esp_log.h>

#include "settings.h"

#define TAG "LampController"

int LampController::LevelToBrightness(int level) const {
    if (level < 0) level = 0;
    if (level > 8) level = 8;
    return (1 << level) - 1;  // 2^n - 1
}

int LampController::BrightnessToLevel(int brightness) const {
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    unsigned int value = static_cast<unsigned int>(brightness) + 1;
    int total_bits = sizeof(value) * 8;  // 动态获取位宽（如 32/64）
    int level = (total_bits - 1) - __builtin_clz(value);
    return level;  // log2(brightness + 1)
}

StripColor LampController::RGBToColor(int red, int green, int blue) {
    if (red < 0) red = 0;
    if (red > 255) red = 255;
    if (green < 0) green = 0;
    if (green > 255) green = 255;
    if (blue < 0) blue = 0;
    if (blue > 255) blue = 255;
    return {static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue)};
}

LampController::LampController(gpio_num_t gpio_num) : gpio_num_(gpio_num) {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << gpio_num_),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    gpio_set_level(gpio_num_, 0);

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool("self.lamp.get_state", "Get the power state of the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           return power_ ? "{\"power\": true}" : "{\"power\": false}";
                       });

    mcp_server.AddTool("self.lamp.turn_on", "Turn on the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           power_ = true;
                           gpio_set_level(gpio_num_, 1);
                           return true;
                       });

    mcp_server.AddTool("self.lamp.turn_off", "Turn off the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           power_ = false;
                           gpio_set_level(gpio_num_, 0);
                           return true;
                       });
}

LampController::LampController(gpio_num_t gpio_num, LampCircularStrip* lampStrip)
    : gpio_num_(gpio_num), lampStrip_(lampStrip) {
    // 从设置中读取亮度等级
    Settings settings("lamp_strip");
    int brightness = settings.GetInt("brightness", 128); 
    lampStrip_->SetBrightness(brightness, 4);

    int red = settings.GetInt("red", 255);
    int green = settings.GetInt("green", 255);
    int blue = settings.GetInt("blue", 255);
    
    EffectParams params = {.base_color = RGBToColor(red, green, blue)};
    lampStrip_->SetEffect(EFFECT_STATIC, params);

    // 初始化音频节奏处理器回调
    // audioRhythmProcessor_.SetCallback([this](int volume) {
    //     if (lampStrip_->GetEffectType() == EFFECT_MUSIC) {
    //         lampStrip_->UpdateMusicEffect(volume);
    //     }
    // });

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool("self.lamp.get_state", "Get the power state of the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           return lampStrip_->GetPower() ? "{\"power\": true}" : "{\"power\": false}";
                       });

    mcp_server.AddTool("self.lamp.turn_on", "Turn on the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           ESP_LOGI(TAG, "开灯....");
                           gpio_set_level(gpio_num_, 1);
                           lampStrip_->SetPower(true);
                           return true;
                       });

    mcp_server.AddTool("self.lamp.turn_off", "Turn off the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           lampStrip_->SetPower(false);
                           gpio_set_level(gpio_num_, 0);
                           return true;
                       });

    mcp_server.AddTool("self.lamp.set_brightness", "Set the brightness level of the lamp (0-8)",
                       PropertyList({Property("level", kPropertyTypeInteger, 0, 8)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int level = properties["level"].value<int>();
                           ESP_LOGI(TAG, "Set the lamp brightness level to %d", level);
                           brightness_level_ = level;
                           int brightness = LevelToBrightness(brightness_level_);
                           // 保存设置
                           Settings settings("lamp_strip", true);
                           settings.SetInt("brightness", brightness);
                           // 设置亮度
                           lampStrip_->SetBrightness(brightness, 4);
                           return true;
                       });

    mcp_server.AddTool("self.lamp.get_brightness", "Get the brightness level of the lamp", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           Settings settings("lamp_strip");
                           int brightness = settings.GetInt("brightness", 128);
                           brightness_level_ = BrightnessToLevel(brightness);
                           cJSON* root = cJSON_CreateObject();
                           cJSON_AddNumberToObject(root, "level", brightness_level_);
                           auto json_str = cJSON_PrintUnformatted(root);
                           std::string response(json_str);
                           cJSON_free(json_str);
                           cJSON_Delete(root);
                           ESP_LOGI(TAG, "Get the lamp brightness level: %s", response.c_str());
                           return response;
                       });

    mcp_server.AddTool(
        "self.lamp.set_color",
        "Set the color of the lamp by RGB value (0-255, 0-255, 0-255). When changing colors, the three default colors "
        "are Warm Color (255,120,5), WarmWhite Color (255,180,50), and White Color(255,255,255)",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255), Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            ESP_LOGI(TAG, "Set led strip all color to %d, %d, %d", red, green, blue);

            EffectType effect = EFFECT_STATIC;
            EffectParams params = {.base_color = RGBToColor(red, green, blue)};
            lampStrip_->SetEffect(effect, params);

            // 保存设置
            Settings settings("lamp_strip", true);
            settings.SetInt("red", red);
            settings.SetInt("green", green);
            settings.SetInt("blue", blue);
            return true;
        });

    mcp_server.AddTool("self.lamp.get_color", "Get the current color of the lamp via RGB value (0-255, 0-255, 0-255)",
                       PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                           Settings settings("lamp_strip");
                           int red = settings.GetInt("red", 255);
                           int green = settings.GetInt("green", 255);
                           int blue = settings.GetInt("blue", 255);

                           cJSON* root = cJSON_CreateObject();
                           cJSON_AddNumberToObject(root, "red", red);
                           cJSON_AddNumberToObject(root, "green", green);
                           cJSON_AddNumberToObject(root, "blue", blue);
                           auto json_str = cJSON_PrintUnformatted(root);
                           std::string response(json_str);
                           cJSON_free(json_str);
                           cJSON_Delete(root);

                           ESP_LOGI(TAG, "Get the lamp color via RGB value: %s", response.c_str());
                           return response;
                       });

    mcp_server.AddTool(
        "self.lamp.set_single_color", "Set the color of a single led.",
        PropertyList({Property("index", kPropertyTypeInteger, 0, 11), Property("red", kPropertyTypeInteger, 0, 255),
                      Property("green", kPropertyTypeInteger, 0, 255), Property("blue", kPropertyTypeInteger, 0, 255)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int index = properties["index"].value<int>();
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            ESP_LOGI(TAG, "Set led strip single color %d to %d, %d, %d", index, red, green, blue);

            EffectType effect = EFFECT_STATIC_SINGLE;
            EffectParams params = {.base_color = RGBToColor(red, green, blue), .index = index};
            lampStrip_->SetEffect(effect, params);
            return true;
        });

    mcp_server.AddTool(
        "self.lamp.blink", "Blink the lamp. (闪烁). interval default 200ms",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255), Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255),
                      Property("interval", kPropertyTypeInteger, 30, 1000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            int interval = properties["interval"].value<int>();
            ESP_LOGI(TAG, "Blink lamp with color %d, %d, %d, interval %dms", red, green, blue, interval);
           
            EffectType effect = EFFECT_BLINK;
            EffectParams params = {.base_color = RGBToColor(red, green, blue), .interval = interval};
            lampStrip_->SetEffect(effect, params);
            return true;
        });

    mcp_server.AddTool(
        "self.lamp.scroll", "Scroll the lamp. (跑马灯). interval default 30ms. length default 5",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255), Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255), Property("length", kPropertyTypeInteger, 1, 7),
                      Property("interval", kPropertyTypeInteger, 5, 1000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            int interval = properties["interval"].value<int>();
            int length = properties["length"].value<int>();
            ESP_LOGI(TAG, "Scroll lamp with color %d, %d, %d, length %d, interval %dms", red, green, blue, length,
                     interval);

            EffectType effect = EFFECT_SCROLL;
            EffectParams params = {.base_color = RGBToColor(red, green, blue), .interval = interval, .length = length};
            lampStrip_->SetEffect(effect, params);
            return true;
        });

    mcp_server.AddTool("self.lamp.set_music_mode", " Enable or disable lamp music mode.",
        PropertyList({Property("enable", kPropertyTypeBoolean)}),
        [this](const PropertyList& properties) {
            bool enable = properties["enable"].value<bool>();
            EnableMusicMode(enable, lampStrip_);
            return true;
        });

}

void LampController::EnableMusicMode(bool enable, LampCircularStrip* lampStrip) {
    if (enable) {
        lampStrip_->SetEffect(EFFECT_MUSIC);
        //audioRhythmProcessor_.Start();
    } else {
        //audioRhythmProcessor_.Stop();
        lampStrip_->SetEffect(EFFECT_STATIC); // 切换回静态效果
        lampStrip_->RefreshEffect();
    }
}

void LampController::ProcessAudioData(const uint8_t* data, size_t length) {
    // 调用audioRhythmProcessor_的Process函数，处理音频数据
    //audioRhythmProcessor_.Process(data, length);
}

#endif  // LAMP_CONTROLLER_H