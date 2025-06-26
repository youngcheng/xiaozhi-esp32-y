#include "lamp_circular_strip.h"

#include <esp_log.h>

#include <cmath>

#define TAG "LampCircularStrip"

LampCircularStrip::LampCircularStrip(gpio_num_t gpio, uint8_t max_leds) : CircularStrip(gpio, max_leds) {}

void LampCircularStrip::SetPower(bool power) {
    ESP_LOGI(TAG, "SetPower.... %s", power ? "true" : "false");
    power_ = power;
    RefreshEffect();
}

bool LampCircularStrip::GetPower() {
    return power_;
}

void LampCircularStrip::SetBrightness(uint8_t default_brightness, uint8_t low_brightness) {
    default_brightness_ = default_brightness;
    low_brightness_ = low_brightness;
    RefreshEffect();
}

void LampCircularStrip::SetEffect(EffectType effect) {
    current_effect_ = effect;
    RefreshEffect();
}

void LampCircularStrip::SetEffect(EffectType effect, EffectParams params) {
    current_effect_ = effect;
    effect_params_ = params;
    RefreshEffect();
}

void LampCircularStrip::RefreshEffect() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!power_) {
        StripColor black{0, 0, 0};
        SetAllColor(black);
        return;
    }

    StripColor color = ApplyBrightness(effect_params_.base_color, default_brightness_);

    switch (current_effect_) {
        case EFFECT_STATIC:
            SetAllColor(color);
            break;
        case EFFECT_BLINK:
            Blink(color, effect_params_.interval);
            break;
        case EFFECT_SCROLL:
            Scroll({4, 4, 4}, color, effect_params_.length, effect_params_.interval);
            break;
        case EFFECT_STATIC_SINGLE:
            SetSingleColor(effect_params_.index, color);
            break;
        case EFFECT_MUSIC:
            // 音乐效果需要持续更新，这里只需重置基础颜色
            // effect_params_.base_color = base_color;
            break;
    }
}

void LampCircularStrip::UpdateMusicEffect(int volume) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if(max_leds_ == 0){
        return;
    }
    
    if (!power_ || current_effect_ != EFFECT_MUSIC) return;
    
    // 根据音量动态调整亮度
    float scale = volume / 255.0f;
    StripColor dynamic_color = {
        static_cast<uint8_t>(effect_params_.base_color.red * scale),
        static_cast<uint8_t>(effect_params_.base_color.green * scale),
        static_cast<uint8_t>(effect_params_.base_color.blue * scale)
    };
    
    // 创建脉动效果
    for (int i = 0; i < max_leds_; i++) {
        float angle = i * 2 * M_PI / max_leds_;
        float pulse = (sin(angle + volume * 0.01) + 1) * 0.5;
        StripColor c = {
            static_cast<uint8_t>(dynamic_color.red * pulse),
            static_cast<uint8_t>(dynamic_color.green * pulse),
            static_cast<uint8_t>(dynamic_color.blue * pulse)
        };
        SetIndexColor(i, c);
    }
    Show();
}

// 亮度调整辅助函数
StripColor LampCircularStrip::ApplyBrightness(StripColor color, int brightness) const {
    float factor = brightness / 255.0f;
    return {static_cast<uint8_t>(color.red * factor), static_cast<uint8_t>(color.green * factor),
            static_cast<uint8_t>(color.blue * factor)};
}


