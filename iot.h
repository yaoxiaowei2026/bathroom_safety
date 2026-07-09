#ifndef _IOT_H_
#define _IOT_H_

void mqtt_init(void);
unsigned int mqtt_is_connected(void);
void send_msg_to_mqtt(void);
int wait_message(void);

#endif
