#ifndef __LAMP_CONTROLLER_H__
#define __LAMP_CONTROLLER_H__

#include "board.h"
#include "led/lamp_circular_strip.h"
#include "mcp_server.h"

class LampController {
   private:
    gpio_num_t gpio_num_;
    LampCircularStrip* lampStrip_ = nullptr;
    bool power_ = false;
    int brightness_level_;  // 亮度等级 (0-8)
    //AudioRhythmProcessor* audioRhythmProcessor_ = nullptr;

    int LevelToBrightness(int level) const;
    int BrightnessToLevel(int brightness) const;
    StripColor RGBToColor(int red, int green, int blue);


   public:
    explicit LampController(gpio_num_t gpio_num);
    explicit LampController(gpio_num_t gpio_num, LampCircularStrip* lampStrip);
    // 新增音乐控制方法
    //void EnableMusicMode(bool enable);
    void EnableMusicMode(bool enable, LampCircularStrip* lampStrip);
    void ProcessAudioData(const uint8_t* data, size_t length);
};

#endif  // __LAMP_CONTROLLER_H__
