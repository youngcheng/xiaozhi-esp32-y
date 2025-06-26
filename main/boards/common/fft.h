#ifndef _FFT_H_
#define _FFT_H_

#include "esp_dsp.h"
#include "dsp_err.h"


#ifdef __cplusplus
extern "C" {
#endif


// FFT 类型枚举
typedef enum {
    FFT_RADIX_2,    // Radix-2 复数 FFT
    FFT_RADIX_4,    // Radix-4 复数 FFT (ESP32-S3 优化)
    FFT_REAL,       // 实数输入 FFT
} fft_type_t;

// FFT 配置结构体
typedef struct {
    fft_type_t type;       // FFT 类型
    int length;            // FFT 点数 (必须为 2^N 或 4^N)
    float *work_buffer;    // 工作缓冲区 (按需分配)
} fft_config_t;

/**
 * @brief 初始化 FFT 配置
 * 
 * @param cfg   配置结构体指针
 * @param type  FFT 类型
 * @param length FFT 点数
 * @return esp_err_t 
 *   - ESP_OK: 成功
 *   - ESP_ERR_DSP_INVALID_PARAM: 参数错误
 */
esp_err_t fft_init(fft_config_t *cfg, fft_type_t type, int length);

/**
 * @brief 执行 FFT 变换
 * 
 * @param cfg   配置结构体指针
 * @param input 输入数据 (复数格式: [real0, imag0, real1, imag1...])
 * @param output 输出频谱 (与输入同址或独立存储)
 * @return esp_err_t 
 */
esp_err_t fft_execute(fft_config_t *cfg, float *input, float *output);

/**
 * @brief 释放 FFT 资源
 * 
 * @param cfg 配置结构体指针
 */
void fft_cleanup(fft_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // _FFT_H_