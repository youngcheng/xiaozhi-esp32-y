#ifndef _AUDIO_RHYTHM_PROCESSOR_H_
#define _AUDIO_RHYTHM_PROCESSOR_H_

#include <functional>
#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include "fft.h"  // 集成 ESP-DSP FFT 库

class AudioRhythmProcessor {
public:
    using VolumeCallback = std::function<void(int)>;
    
    // 新增灵敏度调节接口
    void SetSensitivity(float sensitivity);
    void SetCallback(VolumeCallback cb) { callback_ = cb; }
    void Start();
    void Stop();
    void Process(const int16_t* data, size_t sample_count);  // 使用 int16_t 输入

private:
    VolumeCallback callback_;
    std::atomic<bool> running_{false};
    float sensitivity_ = 1.3f;  // 默认灵敏度

    // FFT 配置及状态
    fft_config_t fft_config_;
    std::vector<float> energy_history_;  // 能量环形缓冲区
    size_t history_index_ = 0;
    std::mutex fft_mutex_;  // FFT 操作线程锁
};

#endif // _AUDIO_RHYTHM_PROCESSOR_H_