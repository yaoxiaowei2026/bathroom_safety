#ifndef MUSIC_H
#define MUSIC_H

// SystemState 枚举（只在这里定义一次）
typedef enum {
    STATE_NORMAL = 0,
    STATE_WARNING = 1,
    STATE_EMERGENCY = 2
} SystemState;

// 外部变量声明
extern float g_temp;
extern float g_humi;
extern float g_gas_ppm;
extern volatile SystemState g_current_state;

void music_init(void);
void music_poll(void);
void music_set_emergency(int enable);

#endif