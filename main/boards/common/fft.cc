#include "fft.h"
#include "esp_log.h"
#include "esp_heap_caps.h"   // 内存分配函数
#include <cstring>           // memcpy
#include "dsps_fft2r.h"
#include "dsps_fft4r.h"      // FFT4函数

static const char *TAG = "FFT";

esp_err_t fft_init(fft_config_t *cfg, fft_type_t type, int length) {
    // 参数校验
    if (length <= 0) {
        ESP_LOGE(TAG, "无效的长度: %d", length);
        return ESP_ERR_INVALID_ARG;
    }
    
    if ((type == FFT_RADIX_2 && (length & (length-1)) != 0) || 
        (type == FFT_RADIX_4 && (length % 4 != 0))) {
        ESP_LOGE(TAG, "无效的FFT长度: %d (类型: %d)", length, type);
        return ESP_ERR_INVALID_ARG;
    }

    cfg->type = type;
    cfg->length = length;
    cfg->work_buffer = NULL;
    
    // 分配工作缓冲区
    if (type == FFT_REAL || type == FFT_RADIX_4) {
        size_t buffer_size = 2 * length * sizeof(float);
        
        #if CONFIG_SPIRAM
        // 显式类型转换解决C++类型严格检查问题
        cfg->work_buffer = (float*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        #endif
        
        // 如果SPI RAM分配失败或未配置，使用普通内存
        if (!cfg->work_buffer) {
            cfg->work_buffer = (float*)malloc(buffer_size);  // 添加显式类型转换
        }
        
        if (!cfg->work_buffer) {
            ESP_LOGE(TAG, "无法分配工作缓冲区(%zu字节)", buffer_size);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGD(TAG, "分配的%d字节工作缓冲区", buffer_size);
    }
    
    return ESP_OK;
}


esp_err_t fft_execute(fft_config_t *cfg, float *input, float *output) {
    // 检查参数是否有效
    if (!cfg || !input || !output) {
        ESP_LOGE(TAG, "无效参数");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    // 根据FFT类型执行不同的操作
    switch (cfg->type) {
        case FFT_RADIX_2:
            // 复数FFT直接在输入上操作
            ret = dsps_fft2r_fc32_ansi(input, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "FFT2处理失败: 0x%x", ret);
                return ret;
            }
            
            // 使用通用位反转函数
            ret = dsps_bit_rev2r_fc32(input, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "位反转失败: 0x%x", ret);
                return ret;
            }
            
            // 复制结果到输出
            memcpy(output, input, 2 * cfg->length * sizeof(float));
            break;
            
        case FFT_RADIX_4:
            // 检查工作缓冲区是否已初始化
            if (!cfg->work_buffer) {
                ESP_LOGE(TAG, "工作缓冲区未初始化");
                return ESP_ERR_INVALID_STATE;
            }
            
            // 复制输入到工作缓冲区并执行FFT4
            memcpy(cfg->work_buffer, input, 2 * cfg->length * sizeof(float));
            ret = dsps_fft4r_fc32(cfg->work_buffer, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "FFT4处理失败: 0x%x", ret);
                return ret;
            }
            
            // 位反转并复制到输出
            ret = dsps_bit_rev4r_fc32(output, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "位反转失败: 0x%x", ret);
                return ret;
            }
            
            memcpy(output, cfg->work_buffer, 2 * cfg->length * sizeof(float));
            break;
            
        case FFT_REAL: {
            // 检查工作缓冲区是否已初始化
            if (!cfg->work_buffer) {
                ESP_LOGE(TAG, "工作缓冲区未初始化");
                return ESP_ERR_INVALID_STATE;
            }
            
            // 1. 将实数输入转为复数格式（虚部设为0）
            for (int i = 0; i < cfg->length; i++) {
                cfg->work_buffer[2 * i] = input[i];   // 实部
                cfg->work_buffer[2 * i + 1] = 0.0f;   // 虚部
            }
            
            // 2. 执行FFT和位反转（原地操作）
            ret = dsps_fft2r_fc32(cfg->work_buffer, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "FFT处理失败: 0x%x", ret);
                return ret;
            }
            
            ret = dsps_bit_rev2r_fc32(cfg->work_buffer, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "位反转失败: 0x%x", ret);
                return ret;
            }
            
            // 3. 转换为实数频谱（关键修正 - 只传2个参数）
            // 注意：此操作是原地操作，会覆盖工作缓冲区的内容
            ret = dsps_cplx2real_fc32(cfg->work_buffer, cfg->length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "复数转实数失败: 0x%x", ret);
                return ret;
            }
            
            // 4. 复制结果到输出
            memcpy(output, cfg->work_buffer, 2 * cfg->length * sizeof(float));
            break;
        }
        
        default:
            ESP_LOGE(TAG, "未知FFT类型: %d", cfg->type);
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

void fft_cleanup(fft_config_t *cfg) {
    if (!cfg) return;
    
    if (cfg->work_buffer) {
        free(cfg->work_buffer);
        cfg->work_buffer = NULL;
    }
}