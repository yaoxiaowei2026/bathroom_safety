#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

void mqtt_init(void);
void mqtt_publish_data(float temp, float humi, float gas);

#endif
