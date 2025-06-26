#ifndef _LAMP_CIRCULAR_STRIP_H_
#define _LAMP_CIRCULAR_STRIP_H_

#include <atomic>
#include <mutex>

#include "circular_strip.h"

// 灯光效果类型
typedef enum {
    EFFECT_STATIC = 0,   // 静态光
    EFFECT_BLINK,    // 闪烁
    EFFECT_SCROLL,   // 滚动
    EFFECT_STATIC_SINGLE, // 单色
    EFFECT_MUSIC     // 音乐节奏
} EffectType;

struct EffectParams {
    StripColor base_color;
    int interval = 200; // 默认200ms
    int length = 5;     // 滚动效果默认长度
    int index = 0;     // 当前索引
};


class LampCircularStrip : public CircularStrip {
   public:
    LampCircularStrip(gpio_num_t gpio, uint8_t max_leds);

    // 状态设置接口
    void SetPower(bool power);
    bool GetPower();
    void SetBrightness(uint8_t default_brightness, uint8_t low_brightness);
    void SetEffect(EffectType effect);
    void SetEffect(EffectType effect, EffectParams params);
    void RefreshEffect();
    // 音乐效果更新
    void UpdateMusicEffect(int volume);
    
   private:
    bool power_ = false;
    EffectType current_effect_ = EFFECT_STATIC;
    EffectParams effect_params_ =  {.base_color = {255, 255, 255}, .interval = 200, .length = 5, .index = 0}; // 当前效果参数
    mutable std::mutex state_mutex_;  // 状态修改锁
    StripColor ApplyBrightness(StripColor color, int brightness) const;
};

#endif  // _LAMP_CIRCULAR_STRIP_H_
