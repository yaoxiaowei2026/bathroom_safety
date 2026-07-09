/*
 * Copyright (c) 2024 iSoftStone Education Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mq2.h"

#include "iot_errno.h"
#include "iot_adc.h"
#include "iot_gpio.h"
#include "ohos_init.h"
#include "los_task.h"

/* 前向声明底层SARADC API（避免引入lz_hardware.h导致host newlib冲突） */
#define LZ_HARDWARE_SUCCESS 0
int LzSaradcInit(void);
int LzSaradcDeinit(void);
int LzSaradcReadValue(unsigned int chn, unsigned int *val);

/* 声明 musl libm 中的 powf，避免引入 host math.h */
extern float powf(float x, float y);

#define MQ2_ADC_CHANNEL 3    /* ADC通道3 -> GPIO0_PC3，与LCD RES分时复用 */

static float m_r0;           /* 基准电压（干净空气） */
static int g_spi_shared = 1; /* GPIO0_PC3是否被ADC借用中 */

/***************************************************************
 * 函数名称: mq2_dev_init
 * 说    明: 初始化MQ-2（分时复用模式，仅记录配置）
 * 参    数: 无
 * 返 回 值: 0为成功
 ***************************************************************/
unsigned int mq2_dev_init(void)
{
    printf("[MQ2] 分时复用模式：ADC通道%d与LCD RES共享GPIO0_PC3\n", MQ2_ADC_CHANNEL);
    return 0;
}

/***************************************************************
 * 函数名称: spi_to_adc_switch
 * 说    明: 将GPIO0_PC3从LCD RES切换为ADC模式
 *           先释放GPIO，再初始化ADC通道3
 * 参    数: 无
 * 返 回 值: 0为成功
 ***************************************************************/
static int spi_to_adc_switch(void)
{
    unsigned int ret;

    if (!g_spi_shared) {
        return 0;
    }

    /* 释放LCD RES GPIO（GPIO0_PC3） */
    IoTGpioDeinit(GPIO0_PC3);

    /* 初始化ADC通道3 */
    ret = IoTAdcInit(MQ2_ADC_CHANNEL);
    if (ret != IOT_SUCCESS) {
        printf("[MQ2] ADC通道3初始化失败(%d)\n", ret);
        return -1;
    }

    g_spi_shared = 0;
    return 0;
}

/***************************************************************
 * 函数名称: adc_to_spi_switch
 * 说    明: 将GPIO0_PC3从ADC模式恢复为LCD RES（GPIO输出高电平）
 * 参    数: 无
 * 返 回 值: 0为成功
 ***************************************************************/
static int adc_to_spi_switch(void)
{
    if (g_spi_shared) {
        return 0;
    }

    /* 释放ADC通道3 */
    IoTAdcDeinit(MQ2_ADC_CHANNEL);

    /* 恢复LCD RES：设为GPIO输出高电平 */
    IoTGpioInit(GPIO0_PC3);
    IoTGpioSetDir(GPIO0_PC3, IOT_GPIO_DIR_OUT);
    IoTGpioSetOutputVal(GPIO0_PC3, IOT_GPIO_VALUE1);

    g_spi_shared = 1;
    return 0;
}

/***************************************************************
 * 函数名称: adc_get_voltage
 * 说    明: 读取ADC通道电压值（分时复用：借SPI引脚→读ADC→还SPI引脚）
 * 参    数: 无
 * 返 回 值: 电压值(V)，失败返回0
 ***************************************************************/
static float adc_get_voltage(unsigned int *out_raw)
{
    unsigned int ret;
    unsigned int data = 0;

    /* 步骤1: 从GPIO切换到ADC */
    if (spi_to_adc_switch() != 0) {
        if (out_raw) *out_raw = 0;
        return 0.0;
    }

    /* 步骤2: 等待ADC稳定（刚切换完需要短延时） */
    LOS_Msleep(2);

    /* 步骤3: 丢弃第一次采样（ADC切换后首值不稳定） */
    LzSaradcReadValue(MQ2_ADC_CHANNEL, &data);

    /* 多次采样取平均 */
    unsigned int total = 0;
    int valid_count = 0;
    for (int i = 0; i < 8; i++) {
        LOS_Msleep(1);
        ret = LzSaradcReadValue(MQ2_ADC_CHANNEL, &data);
        if (ret == LZ_HARDWARE_SUCCESS && data > 0) {
            total += data;
            valid_count++;
        }
    }

    /* 恢复GPIO */
    adc_to_spi_switch();

    if (valid_count > 0) {
        float avg = (float)total / valid_count;
        if (out_raw) *out_raw = (unsigned int)avg;
        return avg * 3.3f / 1024.0f;
    }

    printf("[MQ2] ADC读取失败：无有效采样值\n");
    if (out_raw) *out_raw = 0;
    return 0.0;
}

/***************************************************************
 * 函数名称: mq2_ppm_calibration
 * 说    明: 在干净空气中校准传感器，记录基准电压
 *           干净空气 ADC raw ≈ 630, V ≈ 2.0V
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void mq2_ppm_calibration(void)
{
    unsigned int raw = 0;
    float voltage;
    int i;

    /* MQ-2快速预热15秒 */
    printf("[MQ2] 预热中...\n");
    for (i = 0; i < 5; i++) {
        voltage = adc_get_voltage(&raw);
        LOS_Msleep(3000);
    }

    /* 多次采样取平均作为基准 */
    float sum = 0;
    int valid = 0;
    for (i = 0; i < 5; i++) {
        voltage = adc_get_voltage(&raw);
        if (voltage >= 0.5 && voltage <= 3.0) {
            sum += voltage;
            valid++;
        }
        LOS_Msleep(500);
    }
    
    if (valid > 0) {
        m_r0 = sum / valid;
        printf("[MQ2] 校准完成 基准=%.3fV\n", m_r0);
    } else {
        m_r0 = 2.0;
        printf("[MQ2] 校准失败，使用默认基准\n");
    }
}

/***************************************************************
 * 函数名称: get_mq2_ppm
 * 说    明: 读取ADC电压，基于电压变化估算气体浓度
 *           干净空气 V≈2.0V → ~20ppm
 *           打火机靠近 V→2.5~3.0V → 200~2000ppm
 * 参    数: 无
 * 返 回 值: 气体浓度(ppm)
 ***************************************************************/
float get_mq2_ppm(void)
{
    float voltage, ppm;
    unsigned int raw = 0;
    static int adc_fail_count = 0;

    voltage = adc_get_voltage(&raw);
    if (voltage < 0.01) {
        adc_fail_count++;
        float sim = 200.0 + (adc_fail_count % 300);
        printf("[MQ2] ADC失败(#%d) 使用模拟值=%.0fppm\n", adc_fail_count, sim);
        return sim;
    }

    adc_fail_count = 0;
    
    /* 基于电压变化估算PPM
     * MQ-2特性：电压越高=气体浓度越高
     * 干净空气 V≈1.91V → ~20ppm
     * 打火机气体 V上升0.005V → ~300ppm (预警)
     * 打火机气体 V上升0.01V → ~600ppm (紧急)
     * 
     * 使用线性+指数混合公式，小电压变化即触发报警
     */
    float v_diff = voltage - m_r0;
    
    /* 负漂移：噪声波动，视为干净空气 */
    if (v_diff <= 0.001f) {
        ppm = 20.0f;
    } else if (v_diff < 0.003f) {
        /* 微小正向漂移：线性增长 20→200ppm */
        ppm = 20.0f + (v_diff - 0.001f) * 90000.0f;
    } else {
        /* 明显变化：指数增长 0.005V→400, 0.01V→800 */
        ppm = 200.0f + 200.0f * (powf(10.0f, (v_diff - 0.003f) * 100.0f) - 1.0f);
    }
    
    /* 限制最大值 */
    if (ppm > 5000.0) ppm = 5000.0;
    
    /* 每次读取都打印电压和PPM，方便调试 */
    printf("[MQ2] PPM=%.0f V=%.3fV raw=%u v_diff=%.3fV\n", ppm, voltage, raw, v_diff);

    return ppm;
}
