#include <stdio.h>
#include <string.h>
#include "ohos_init.h"
#include "los_task.h"
#include "lz_hardware.h"
#include "wifi_device.h"
#include "wifi_hotspot.h"
#include "iot_config.h"

static int g_wifi_sta_id = -1;

void wifi_connect(void)
{
    WifiDeviceConfig config = {0};
    int ret;

    printf("[WiFi] 开始连接...\n");

    ret = EnableWifi();
    if (ret != 0) {
        printf("[WiFi] 启用WiFi失败: %d\n", ret);
        return;
    }

    strcpy(config.ssid, CONFIG_WIFI_SSID);
    strcpy(config.preSharedKey, CONFIG_WIFI_PWD);
    config.securityType = WIFI_SEC_TYPE_PSK;

    ret = AddDeviceConfig(&config, &g_wifi_sta_id);
    if (ret != 0) {
        printf("[WiFi] 添加配置失败: %d\n", ret);
        return;
    }

    ret = ConnectTo(g_wifi_sta_id);
    if (ret != 0) {
        printf("[WiFi] 连接失败: %d\n", ret);
        return;
    }

    printf("[WiFi] 连接成功！SSID: %s\n", CONFIG_WIFI_SSID);
}
