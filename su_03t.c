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

#include "su_03t.h"

#include "los_task.h"
#include "ohos_init.h"

#include "iot_errno.h"
#include "iot_uart.h"

/* 避免引入host stdio.h/string.h导致newlib冲突 */
extern int printf(const char *fmt, ...);

/* UART2_M1: TX=GPIO0_PB3, RX=GPIO0_PB2 (匹配E53开发板原理图SU03T接口) */
#define UART_HANDLE EUART2_M1

/* 存储最新传感器数据，供语音查询时使用 */
static double g_voice_temp = 25.0;
static double g_voice_humi = 55.0;
static double g_voice_gas = 200.0;
static int g_voice_state = 0; /* 0=正常, 1=预警, 2=紧急 */

static int g_uart_ready = 0;

/***************************************************************
 * 函数名称: su03t_send_frame
 * 说    明: 发送SU-03T自定义协议帧
 *           格式: AA 55 [msg_id] [data_len字节数据] 55 AA
 ***************************************************************/
static void su03t_send_frame(uint8_t msg_id, const uint8_t *data, int len)
{
    if (!g_uart_ready) return;

    uint8_t buf[64];
    int pos = 0;

    buf[pos++] = 0xAA;
    buf[pos++] = 0x55;
    buf[pos++] = msg_id;

    for (int i = 0; i < len && pos < 60; i++) {
        buf[pos++] = data[i];
    }

    buf[pos++] = 0x55;
    buf[pos++] = 0xAA;

    printf("[VOICE] TX id=%02X len=%d: %02X%02X%02X...%02X%02X\n",
           msg_id, pos, buf[0], buf[1], buf[2], buf[pos-2], buf[pos-1]);

    IoTUartWrite(UART_HANDLE, buf, pos);
}

/***************************************************************
 * 函数名称: su03t_process_request
 * 说    明: 处理SU-03T发来的请求帧（一问一答模式）
 *           当你对模块说"当前温度"→模块通过UART_TX发送请求帧
 *           →RK2206收到后回复传感器数据→模块播报
 * 参    数: buf - 接收到的数据, len - 数据长度
 ***************************************************************/
static void su03t_process_request(const uint8_t *buf, int len)
{
    /*
     * SU-03T请求格式（来自smartpi.cn配置的getTemp/getHumi等行为）:
     *   getTemp   → UART1_TX发送: 03 01
     *   getHumi   → UART1_TX发送: 03 02
     *   getgas    → UART1_TX发送: 03 03
     *   getzhuangtai→UART1_TX发送: 03 04
     *
     * 格式: [0x03] [msg_id], 共2字节，无帧头
     */

    if (len < 2) return;

    /* 遍历缓冲区，查找所有 03 XX 格式的请求 */
    for (int i = 0; i < len - 1; i++) {
        if (buf[i] != 0x03) continue;

        uint8_t msg_id = buf[i + 1];
        printf("[VOICE] RX request id=%02X\n", msg_id);

        switch (msg_id) {
            case 0x01: /* 温度 */
                su03t_send_frame(0x01, (uint8_t *)&g_voice_temp, sizeof(double));
                return;
            case 0x02: /* 湿度 */
                su03t_send_frame(0x02, (uint8_t *)&g_voice_humi, sizeof(double));
                return;
            case 0x03: /* 煤气 */
                su03t_send_frame(0x03, (uint8_t *)&g_voice_gas, sizeof(double));
                return;
            case 0x04: { /* 状态: 根据值用不同msg_id (smartpi.cn不支持同msg_id多行为) */
                /*
                 * 正常→msg_id=0x05, 预警→msg_id=0x06, 紧急→msg_id=0x07
                 * smartpi.cn分别创建3个串口输入行为,各监听对应msg_id, 数据类型选"不处理"
                 */
                int state_val = g_voice_state;
                uint8_t state_msg_id;
                switch (state_val) {
                    case 1:  state_msg_id = 0x06; break; /* 预警 */
                    case 2:  state_msg_id = 0x07; break; /* 紧急 */
                    default: state_msg_id = 0x05; break; /* 正常 */
                }
                /* 发一个空数据帧, SU-03T只靠msg_id区分状态 */
                su03t_send_frame(state_msg_id, NULL, 0);
                return;
            }
            default:
                printf("[VOICE] 未知请求 id=%02X\n", msg_id);
                break;
        }
    }
}

/***************************************************************
 * 函数名称: su_03t_thread
 * 说    明: 语音模块处理线程
 *           持续监听UART RX，收到请求帧后回复传感器数据
 ***************************************************************/
static void su_03t_thread(void *arg)
{
    IotUartAttribute attr;
    unsigned int ret;
    uint8_t rx_buf[64];
    int rx_total = 0;

    IoTUartDeinit(UART_HANDLE);

    attr.baudRate = 115200;
    attr.dataBits = IOT_UART_DATA_BIT_8;
    attr.pad = IOT_FLOW_CTRL_NONE;
    attr.parity = IOT_UART_PARITY_NONE;
    attr.rxBlock = IOT_UART_BLOCK_STATE_NONE_BLOCK;
    attr.stopBits = IOT_UART_STOP_BIT_1;
    attr.txBlock = IOT_UART_BLOCK_STATE_NONE_BLOCK;

    ret = IoTUartInit(UART_HANDLE, &attr);
    if (ret != IOT_SUCCESS) {
        printf("[VOICE] UART2初始化失败(%d)\n", ret);
        return;
    }

    g_uart_ready = 1;
    printf("[VOICE] UART2就绪 (RX=GPIO0_PB2, TX=GPIO0_PB3, 115200)\n");
    printf("[VOICE] 等待语音指令: 当前温度/当前湿度/当前煤气/当前状态\n");

    /* 持续监听UART RX，收到请求帧后回复传感器数据 */
    while (1) {
        int rd = IoTUartRead(UART_HANDLE, rx_buf + rx_total, sizeof(rx_buf) - rx_total - 1);
        if (rd > 0) {
            rx_total += rd;
            rx_buf[rx_total] = '\0';

            printf("[VOICE] RX raw(%d): ", rx_total);
            for (int i = 0; i < rx_total && i < 20; i++) {
                printf("%02X ", rx_buf[i]);
            }
            printf("\n");

            /* 尝试解析请求帧 */
            su03t_process_request(rx_buf, rx_total);

            /* 清空缓冲，等待下一次请求 */
            rx_total = 0;
        }

        LOS_Msleep(50);
    }
}

/***************************************************************
 * 函数名称: su03t_update_sensors
 * 说    明: 更新传感器数据缓存（由monitor_task定期调用）
 ***************************************************************/
void su03t_update_sensors(double temp, double humi, double gas, int state)
{
    g_voice_temp = temp;
    g_voice_humi = humi;
    g_voice_gas = gas;
    g_voice_state = state;
}

/***************************************************************
 * 函数名称: su03t_is_ready
 ***************************************************************/
int su03t_is_ready(void)
{
    return g_uart_ready;
}

/***************************************************************
 * 函数名称: su03t_init
 ***************************************************************/
void su03t_init(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)su_03t_thread;
    task.uwStackSize = 4096;
    task.pcName = "su-03t thread";
    task.usTaskPrio = 24;
    unsigned int ret = LOS_TaskCreate(&thread_id, &task);
    if (ret != LOS_OK) {
        printf("[VOICE] 创建语音任务失败 ret:0x%x\n", ret);
    }
}
