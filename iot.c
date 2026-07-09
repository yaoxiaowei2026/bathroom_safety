#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"
#include "cJSON.h"
#include "cmsis_os2.h"
#include "los_task.h"
#include "iot.h"

// ============ 华为云配置（修改这里）============
#define HOST_ADDR        "f7871c9560.st1.iotda-device.cn-south-1.myhuaweicloud.com"
#define DEVICE_ID        "6a467d58c00ccb6d4b625038_bathroom_01"
#define MQTT_PASSWORD    "你的设备密钥"

extern float g_temp;
extern float g_humi;
extern float g_gas_ppm;
extern int g_current_state;
extern volatile int g_led_enabled;

#define MAX_BUFFER_LENGTH 1024

static Network network;
static MQTTClient client;
static unsigned char sendBuf[MAX_BUFFER_LENGTH];
static unsigned char readBuf[MAX_BUFFER_LENGTH];
static unsigned int mqttConnectFlag = 0;

void mqtt_message_arrived(MessageData *data) {
    printf("[MQTT] 收到命令: %.*s\n", 
           data->message->payloadlen, data->message->payload);
    
    cJSON *root = cJSON_ParseWithLength(data->message->payload, data->message->payloadlen);
    if (root != NULL) {
        cJSON *cmd_name = cJSON_GetObjectItem(root, "command_name");
        if (cmd_name != NULL) {
            char *cmd_str = cJSON_GetStringValue(cmd_name);
            if (!strcmp(cmd_str, "light_control")) {
                cJSON *para = cJSON_GetObjectItem(root, "paras");
                cJSON *onoff = cJSON_GetObjectItem(para, "onoff");
                if (onoff != NULL) {
                    char *val = cJSON_GetStringValue(onoff);
                    if (!strcmp(val, "ON")) {
                        g_led_enabled = 1;
                        printf("[MQTT] 远程开灯\n");
                    } else {
                        g_led_enabled = 0;
                        printf("[MQTT] 远程关灯\n");
                    }
                }
            }
        }
        cJSON_Delete(root);
    }
}

int wait_message(void) {
    if (mqttConnectFlag == 0) {
        return 0;
    }
    int rec = MQTTYield(&client, 5000);
    if (rec != 0) {
        mqttConnectFlag = 0;
        return 0;
    }
    return 1;
}

void mqtt_init(void) {
    int rc;
    
    printf("[MQTT] 开始连接华为云...\n");
    printf("[MQTT] 服务器: %s\n", HOST_ADDR);
    printf("[MQTT] 设备ID: %s\n", DEVICE_ID);
    
    NetworkInit(&network);
    
    rc = NetworkConnect(&network, HOST_ADDR, 1883);
    if (rc != 0) {
        printf("[MQTT] 网络连接失败: %d\n", rc);
        return;
    }
    printf("[MQTT] 网络连接成功\n");
    
    MQTTClientInit(&client, &network, 3000, sendBuf, sizeof(sendBuf), readBuf, sizeof(readBuf));
    
    MQTTString clientId = MQTTString_initializer;
    clientId.cstring = (char*)DEVICE_ID;
    
    MQTTString userName = MQTTString_initializer;
    userName.cstring = (char*)DEVICE_ID;
    
    MQTTString password = MQTTString_initializer;
    password.cstring = (char*)MQTT_PASSWORD;
    
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID = clientId;
    data.username = userName;
    data.password = password;
    data.willFlag = 0;
    data.MQTTVersion = 4;
    data.keepAliveInterval = 60;
    data.cleansession = 1;
    
    rc = MQTTConnect(&client, &data);
    if (rc != 0) {
        printf("[MQTT] MQTT连接失败: %d\n", rc);
        return;
    }
    printf("[MQTT] MQTT连接成功\n");
    
    char sub_topic[128];
    sprintf(sub_topic, "$oc/devices/%s/sys/commands/+", DEVICE_ID);
    rc = MQTTSubscribe(&client, sub_topic, 0, mqtt_message_arrived);
    if (rc != 0) {
        printf("[MQTT] 订阅失败: %d\n", rc);
        return;
    }
    printf("[MQTT] 订阅成功: %s\n", sub_topic);
    
    mqttConnectFlag = 1;
    printf("[MQTT] ✅ 华为云连接成功！\n");
}

unsigned int mqtt_is_connected(void) {
    return mqttConnectFlag;
}

void send_msg_to_mqtt(void) {
    int rc;
    MQTTMessage message;
    char payload[512] = {0};
    char topic[128] = {0};
    
    if (mqttConnectFlag == 0) {
        return;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    
    cJSON *services = cJSON_AddArrayToObject(root, "services");
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "service_id", "bathroom");
    
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddNumberToObject(properties, "temperature", g_temp);
    cJSON_AddNumberToObject(properties, "humidity", g_humi);
    cJSON_AddNumberToObject(properties, "gas", g_gas_ppm);
    
    const char *state_str[] = {"normal", "warning", "emergency"};
    cJSON_AddStringToObject(properties, "status", state_str[g_current_state]);
    
    cJSON_AddItemToObject(item, "properties", properties);
    cJSON_AddItemToArray(services, item);
    
    char *json_str = cJSON_PrintUnformatted(root);
    strcpy(payload, json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    sprintf(topic, "$oc/devices/%s/sys/properties/report", DEVICE_ID);
    
    message.qos = 0;
    message.retained = 0;
    message.payload = payload;
    message.payloadlen = strlen(payload);
    
    rc = MQTTPublish(&client, topic, &message);
    if (rc != 0) {
        printf("[MQTT] 上报失败: %d\n", rc);
        mqttConnectFlag = 0;
        return;
    }
    
    printf("[MQTT] ✅ 上报: 温度=%.1f, 湿度=%.1f, 煤气=%.1f, 状态=%s\n", 
           g_temp, g_humi, g_gas_ppm, state_str[g_current_state]);
}
