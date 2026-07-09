#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "lz_hardware.h"
#include "uart.h"
#include "music.h"

#define UART_PORT 2

// 外部变量
extern float g_temp;
extern float g_humi;
extern float g_gas_ppm;
extern volatile SystemState g_current_state;

static volatile int g_emergency_mode = 0;

/**
 * @brief 处理查询指令（暂时禁用，避免异常）
 */
static void handle_query_command(uint8_t cmd)
{
    // 暂时不做任何事
    printf("[语音] 收到指令: 0x%02X (暂不处理)\n", cmd);
}

/**
 * @brief UART2 接收任务（简化版，只测试稳定性）
 */
static void uart_read_task(void *arg)
{
    printf("[UART] 任务启动成功！等待语音查询...\n");
    
    int count = 0;
    
    while (1) {
        count++;
        printf("[UART] 心跳 #%d\n", count);
        LOS_Msleep(5000);
    }
}

void music_set_emergency(int enable)
{
    g_emergency_mode = enable;
    if (enable) {
        printf("[语音] 进入紧急模式\n");
    } else {
        printf("[语音] 退出紧急模式\n");
    }
}

void music_init(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};
    
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)uart_read_task;
    task.uwStackSize = 4096;
    task.pcName = "uart_read_task";
    task.usTaskPrio = 16;
    LOS_TaskCreate(&thread_id, &task);
    
    printf("[MUSIC] 语音模块初始化完成（简化版）\n");
}

void music_poll(void)
{
    // 空函数
}