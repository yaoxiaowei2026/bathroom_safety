#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "iot_config.h"
#include "wifi_connect.h"

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

void mqtt_publish_data(float temp, float humi, float gas)
{
    if (!g_mqtt_connected) {
        printf("[MQTT] 未连接，跳过上报\n");
        return;
    }
    
    g_report_count++;
    
    printf("\n");
    printf("┌─────────────────────────────────────────┐\n");
    printf("│  📤 第 %d 次数据上报到华为云            │\n", g_report_count);
    printf("├─────────────────────────────────────────┤\n");
    printf("│  🌡️  温度: %.1f °C                     │\n", temp);
    printf("│  💧  湿度: %.1f %%                      │\n", humi);
    printf("│  ��  煤气: %.1f ppm                    │\n", gas);
    printf("└─────────────────────────────────────────┘\n");
    printf("\n");
}
