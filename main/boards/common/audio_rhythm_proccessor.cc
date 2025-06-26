#include "audio_rhythm_proccessor.h"
#include <cmath>
#include <algorithm>

// 常量配置
constexpr int HISTORY_SIZE = 15;       // 300ms 历史缓冲
constexpr float BASE_THRESHOLD = 1.5f; // 基础阈值系数
constexpr int BASS_BAND_START = 2;    // 低频起始频段 (≈50Hz)
constexpr int BASS_BAND_END = 10;      // 低频结束频段 (≈150Hz)

void AudioRhythmProcessor::SetSensitivity(float sensitivity) {
    sensitivity_ = std::clamp(sensitivity, 0.5f, 2.0f);
}

void AudioRhythmProcessor::Start() {
    if (!running_) {
        running_ = true;
        // 初始化 FFT（1024点实数变换）
        fft_init(&fft_config_, FFT_REAL, 1024);
        energy_history_.resize(HISTORY_SIZE, 0.0f);
    }
}

void AudioRhythmProcessor::Stop() {
    if (running_) {
        running_ = false;
        fft_cleanup(&fft_config_); // 释放 FFT 资源
    }
}

void AudioRhythmProcessor::Process(const int16_t* samples, size_t sample_count) {
    if (!running_ || !callback_ || sample_count == 0) return;

    // 1. 执行 FFT 变换
    std::vector<float> input(sample_count);
    for (size_t i = 0; i < sample_count; ++i) {
        input[i] = samples[i] / 32768.0f; // 归一化到 [-1, 1]
    }

    std::vector<float> spectrum(fft_config_.length / 2);
    {
        std::lock_guard<std::mutex> lock(fft_mutex_);
        fft_execute(&fft_config_, input.data(), spectrum.data());
    }

    // 2. 提取低频能量 (50-150Hz)
    float bass_energy = 0.0f;
    for (int i = BASS_BAND_START; i <= BASS_BAND_END; ++i) {
        bass_energy += spectrum[i];
    }
    bass_energy /= (BASS_BAND_END - BASS_BAND_START + 1);

    // 3. 更新能量历史环形缓冲区
    energy_history_[history_index_] = bass_energy;
    history_index_ = (history_index_ + 1) % HISTORY_SIZE;

    // 4. 动态阈值节拍检测
    float avg_energy = 0.0f;
    for (float e : energy_history_) avg_energy += e;
    avg_energy /= HISTORY_SIZE;

    float dynamic_threshold = avg_energy * BASE_THRESHOLD * sensitivity_;
    if (bass_energy > dynamic_threshold) {
        int volume = static_cast<int>(bass_energy * 200); // 映射到 0-200
        callback_(volume); // 触发节拍回调
    }
}