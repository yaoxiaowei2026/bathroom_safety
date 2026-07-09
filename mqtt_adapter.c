#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "iot.h"

extern float g_temp;
extern float g_humi;
extern float g_gas_ppm;
extern int g_current_state;

static int g_mqtt_connected = 0;
static int g_report_count = 0;

void mqtt_init(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  🌐 华为云 IoT 平台对接模块\n");
    printf("========================================\n");
    printf("  ✅ 华为云连接成功！（模拟模式）\n");
    printf("========================================\n\n");
    
    g_mqtt_connected = 1;
}

unsigned int mqtt_is_connected(void)
{
    return g_mqtt_connected;
}

void send_msg_to_mqtt(void)
{
    if (!g_mqtt_connected) {
        return;
    }
    
    g_report_count++;
    
    const char *state_str[] = {"normal", "warning", "emergency"};
    
    printf("\n");
    printf("┌─────────────────────────────────────────┐\n");
    printf("│  📤 第 %d 次数据上报到华为云            │\n", g_report_count);
    printf("├─────────────────────────────────────────┤\n");
    printf("│  🌡️  温度: %.1f °C                     │\n", g_temp);
    printf("│  💧  湿度: %.1f %%                      │\n", g_humi);
    printf("│  🔥  煤气: %.1f ppm                    │\n", g_gas_ppm);
    printf("│  📊  状态: %s                         │\n", state_str[g_current_state]);
    printf("└─────────────────────────────────────────┘\n");
    printf("\n");
}

int wait_message(void)
{
    LOS_Msleep(100);
    return 1;
}
