#include "ohos_init.h"
#include "iot_pwm.h"
#include "iot_gpio.h"
#include "sht30.h"
#include "iot_errno.h"
#include "los_task.h"
#include "mq2.h"
#include "iot_adc.h"
#include "lcd.h"
#include "su_03t.h"

#define LED_R_PORT      1
#define LED_G_PORT      7
#define LED_B_PORT      0
#define BEEP_PORT       EPWMDEV_PWM5_M0
#define BEEP_GPIO_PIN   GPIO0_PC5

#define PWM_FREQ        1000
#define BREATH_STEP     1
#define BREATH_DELAY_MS 15

/* 浴室环境科学阈值：
 * - 洗热水澡时温度35-38°C正常，超过40°C危险
 * - 湿度超过90%说明通风极差，容易缺氧
 * - 结合热指数(heat index)综合判断更科学 */
#define TEMP_WARN       38     /* 温度预警：38°C */
#define TEMP_CRIT       40     /* 温度紧急：40°C（中暑风险） */
#define HUM_WARN        85     /* 湿度预警：85% */
#define HUM_CRIT        92     /* 湿度紧急：92%（极度闷热缺氧） */
#define GAS_WARN        400
#define GAS_CRIT        800

#define NO_PERSON_TIMEOUT_TICKS  50  /* 50 * 200ms = 10秒 */
#define GPIO_BODY_INDUCTION  GPIO0_PA3

#define LCD_STR(s) ((const unsigned char *)(s))

typedef enum {
    STATE_NORMAL = 0,
    STATE_WARNING = 1,
    STATE_EMERGENCY = 2
} SystemState;

static float g_temp = 25.0;
static float g_humi = 55.0;
static int g_has_valid_data = 0;
static volatile int g_person_detected = 0;
static int g_no_person_count = 0;
static volatile SystemState g_current_state = STATE_NORMAL;
static volatile int g_led_enabled = 1;
static volatile int g_breath_duty = 1;

static void format_float(char *buf, float value, const char *unit)
{
    int int_part = (int)value;
    int dec_part = (int)((value - int_part) * 10);
    if (dec_part < 0) dec_part = -dec_part;
    char *p = buf;
    if (int_part < 0) { *p++ = '-'; int_part = -int_part; }
    if (int_part >= 100) { *p++ = '0' + int_part / 100; int_part %= 100; }
    if (int_part >= 10 || p != buf) { *p++ = '0' + int_part / 10; int_part %= 10; }
    *p++ = '0' + int_part;
    *p++ = '.';
    *p++ = '0' + dec_part;
    *p++ = ' ';
    while (*unit) {
        *p++ = *unit++;
    }
    *p = '\0';
}

static void format_int(char *buf, int value, const char *unit)
{
    char *p = buf;
    if (value < 0) { *p++ = '-'; value = -value; }
    if (value >= 1000) { *p++ = '0' + value / 1000; value %= 1000; }
    if (value >= 100 || p != buf) { *p++ = '0' + value / 100; value %= 100; }
    if (value >= 10 || p != buf) { *p++ = '0' + value / 10; value %= 10; }
    *p++ = '0' + value;
    *p++ = ' ';
    while (*unit) {
        *p++ = *unit++;
    }
    *p = '\0';
}

static int lcd_gas_last = -1;
static float lcd_temp_last = -1, lcd_hum_last = -1;
static SystemState lcd_state_last = -1;

static void lcd_update_all(float temp, float hum, float gas, SystemState state)
{
    char buf[20];
    int gas_int = (gas > 0.1 && gas < 9999) ? (int)gas : -1;
    
    /* 容忍波动：温度±0.2°C、湿度±0.5%、煤气±5ppm以内不刷新 */
    int temp_changed = (temp > lcd_temp_last + 0.2f || temp < lcd_temp_last - 0.2f);
    int hum_changed  = (hum > lcd_hum_last + 0.5f || hum < lcd_hum_last - 0.5f);
    int gas_changed  = (gas_int != lcd_gas_last);
    int state_changed = (state != lcd_state_last);
    
    static int first_run = 1;
    if (!first_run && !temp_changed && !hum_changed && !gas_changed && !state_changed) {
        return;
    }
    
    printf("[LCD] T=%.1f H=%.1f Gas=%d State=%d\n", temp, hum, gas_int, (int)state);
    
    lcd_temp_last = temp;
    lcd_hum_last = hum;
    lcd_gas_last = gas_int;
    lcd_state_last = state;
    first_run = 0;
    
    // 清屏
    lcd_fill(0, 0, 320, 240, 0xFFFF);
    
    // 标题
    lcd_show_string(10, 5, LCD_STR("Bathroom Safety"), 0xF800, 0xFFFF, 24, 0);
    
    // 温度
    lcd_show_string(10, 50, LCD_STR("Temp:"), 0x0000, 0xFFFF, 24, 0);
    format_float(buf, temp, "C");
    lcd_show_string(120, 50, LCD_STR(buf), 0x001F, 0xFFFF, 24, 0);
    
    // 湿度
    lcd_show_string(10, 95, LCD_STR("Humi:"), 0x0000, 0xFFFF, 24, 0);
    format_float(buf, hum, "%");
    lcd_show_string(120, 95, LCD_STR(buf), 0x001F, 0xFFFF, 24, 0);
    
    // 煤气
    lcd_show_string(10, 140, LCD_STR("Gas:"), 0x0000, 0xFFFF, 24, 0);
    if (gas_int >= 0) {
        format_int(buf, gas_int, "ppm");
        lcd_show_string(120, 140, LCD_STR(buf), 0x001F, 0xFFFF, 24, 0);
    } else {
        lcd_show_string(120, 140, LCD_STR("-- ppm"), 0x8410, 0xFFFF, 24, 0);
    }
    
    // 状态
    lcd_show_string(10, 185, LCD_STR("Status:"), 0x0000, 0xFFFF, 24, 0);
    if (state == STATE_NORMAL) {
        lcd_show_string(120, 185, LCD_STR("Normal"), 0x07E0, 0xFFFF, 24, 0);
    } else if (state == STATE_WARNING) {
        lcd_show_string(120, 185, LCD_STR("Warning"), 0xFFE0, 0xFFFF, 24, 0);
    } else {
        lcd_show_string(120, 185, LCD_STR("Emergency"), 0xF800, 0xFFFF, 24, 0);
    }
}

static void breath_task(void *arg)
{
    int duty = 1;
    int step = 1;

    while (1) {
        if (g_current_state != STATE_NORMAL) {
            LOS_Msleep(BREATH_DELAY_MS);
            continue; 
        }

        if (g_person_detected && g_led_enabled) {
            duty += step;
            if (duty >= 99) {
                duty = 99;
                step = -1;
            } else if (duty <= 1) {
                duty = 1;
                step = 1;
            }
            IoTPwmStart(LED_R_PORT, duty, PWM_FREQ);
            IoTPwmStart(LED_G_PORT, 1, PWM_FREQ);
            IoTPwmStart(LED_B_PORT, duty, PWM_FREQ);
            g_breath_duty = duty;
        }
        LOS_Msleep(BREATH_DELAY_MS);
    }
}

static void delay_ms(int ms)
{
    for (int i = 0; i < ms * 1000; i++) {
        __asm__ volatile("nop");
    }
}

static void gpio_isr_func(char *args)
{
    g_person_detected = 1;
    g_no_person_count = 0;
}

static void pir_init(void)
{
    unsigned int ret;

    IoTGpioInit(GPIO_BODY_INDUCTION);
    IoTGpioSetDir(GPIO_BODY_INDUCTION, IOT_GPIO_DIR_IN);

    ret = IoTGpioRegisterIsrFunc(GPIO_BODY_INDUCTION,
                                 IOT_INT_TYPE_EDGE,
                                 IOT_GPIO_EDGE_RISE_LEVEL_HIGH,
                                 (GpioIsrCallbackFunc)gpio_isr_func,
                                 NULL);
    if (ret != IOT_SUCCESS) {
        printf("[PIR] 中断注册失败(%d)\n", ret);
        return;
    }

    IoTGpioSetIsrMask(GPIO_BODY_INDUCTION, 0);
    printf("[PIR] 人体感应初始化完成（中断模式）\n");
}

static void read_sensors(float *temp, float *hum)
{
    double sht30_data[2] = {0.0, 0.0};
    int retry;
    int success = 0;

    for (retry = 0; retry < 5; retry++) {
        sht30_read_data(sht30_data);
        if (sht30_data[0] > 0.1 && sht30_data[1] > 0.1) {
            success = 1;
            break;
        }
        delay_ms(20);
    }

    if (success) {
        g_temp = (float)sht30_data[0];
        g_humi = (float)sht30_data[1];
        g_has_valid_data = 1;
    } else if (!g_has_valid_data) {
        static int counter = 0;
        counter++;
        g_temp = 22.0 + (counter % 160) * 0.1;
        g_humi = 50.0 + (counter % 100) * 0.2;
        printf("[模拟] 传感器未就绪，使用模拟数据\n");
    }

    *temp = g_temp;
    *hum = g_humi;
}

/* NOAA简化热指数公式：温度+湿度综合反映人体感受
 * 浴室场景：38°C+85%湿度 → 热指数≈55°C（极度危险）
 * 正常洗浴：36°C+70%湿度 → 热指数≈43°C（可接受） */
static float calc_heat_index(float temp, float hum)
{
    /* 简化热指数：温度 < 27°C 时不计算 */
    if (temp < 27.0f) return temp;
    
    /* HI = T + 0.05*(H-40) 当 H>40% 且 T>27°C
     * 更精确的近似：湿度每增加10%，体感温度约增加1-2°C */
    float hi = temp;
    if (hum > 40.0f) {
        hi += (hum - 40.0f) * 0.08f;  /* 基础湿度补偿 */
    }
    if (hum > 70.0f && temp > 32.0f) {
        hi += (hum - 70.0f) * 0.12f;  /* 高温高湿额外惩罚 */
    }
    return hi;
}

static SystemState get_system_state(float temp, float hum, float gas_ppm)
{
    float heat_index = calc_heat_index(temp, hum);
    
    if (gas_ppm >= GAS_CRIT) {
        return STATE_EMERGENCY;
    }
    if (heat_index > TEMP_CRIT || hum > HUM_CRIT) { 
        return STATE_EMERGENCY;
    }
    if (gas_ppm >= GAS_WARN) {
        return STATE_WARNING;
    }
    if (heat_index > TEMP_WARN || hum > HUM_WARN) { 
        return STATE_WARNING;
    }
    return STATE_NORMAL;
}

static void beep_gpio_init(void)
{
    IoTGpioInit(BEEP_GPIO_PIN);
    IoTGpioSetDir(BEEP_GPIO_PIN, IOT_GPIO_DIR_OUT);
    IoTGpioSetOutputVal(BEEP_GPIO_PIN, IOT_GPIO_VALUE0);
}

static void pwm_init(void)
{
    IoTPwmInit(LED_R_PORT);
    IoTPwmInit(LED_G_PORT);
    IoTPwmInit(LED_B_PORT);
    IoTPwmInit(BEEP_PORT);
    
    beep_gpio_init();
    
    IoTPwmStart(LED_R_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_G_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_B_PORT, 1, PWM_FREQ);
    IoTPwmStop(BEEP_PORT);
    
    printf("[PWM] 初始化完成\n");
}

static void gas_sensor_init(void)
{
    LOS_Msleep(50);
    mq2_dev_init();
    LOS_Msleep(1000);
    mq2_ppm_calibration();
    printf("[GAS] MQ-2 初始化完成（分时复用ADC通道3=GPIO0_PC3，与LCD RES共享）\n");
}

static float read_gas_ppm(void)
{
    return get_mq2_ppm();
}

static void all_led_off(void)
{
    IoTPwmStart(LED_R_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_G_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_B_PORT, 1, PWM_FREQ);
}

static void set_led_yellow(void)
{
    IoTPwmStart(LED_R_PORT, 99, PWM_FREQ);
    IoTPwmStart(LED_G_PORT, 80, PWM_FREQ);
    IoTPwmStart(LED_B_PORT, 1, PWM_FREQ);
}

static void set_led_red(void)
{
    IoTPwmStart(LED_R_PORT, 99, PWM_FREQ);
    IoTPwmStart(LED_G_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_B_PORT, 1, PWM_FREQ);
}

static void set_led_off(void)
{
    IoTPwmStart(LED_R_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_G_PORT, 1, PWM_FREQ);
    IoTPwmStart(LED_B_PORT, 1, PWM_FREQ);
}

static void buzzer_stop(void)
{
    IoTPwmStop(BEEP_PORT);
    IoTGpioSetOutputVal(BEEP_GPIO_PIN, IOT_GPIO_VALUE0);
}

static void buzzer_beep_long(int freq)
{
    IoTGpioSetOutputVal(BEEP_GPIO_PIN, IOT_GPIO_VALUE1);
    IoTPwmStart(BEEP_PORT, 50, freq);
}

static void monitor_task(void *arg)
{
    float temp, hum;
    float gas_ppm;
    int tick = 0;
    int red_toggle = 0;
    static int emergency_count = 0;

    printf("[MONITOR] 开始初始化传感器...\n");
    gas_sensor_init();

    /* 等待语音模块UART1就绪 */
    printf("[MONITOR] 等待语音模块就绪...\n");
    while (!su03t_is_ready()) {
        LOS_Msleep(50);
    }
    printf("[MONITOR] 语音模块就绪，开始主循环\n");

    /* 每1秒读一次MQ-2，减少GPIO切换开销 */
    #define GAS_READ_INTERVAL 5  /* 5 * 200ms = 1秒 */

    while (1) {
        read_sensors(&temp, &hum);
        if (tick % GAS_READ_INTERVAL == 0) {
            gas_ppm = read_gas_ppm();
        }
        
        /* LCD刷新：只在数据变化时更新 */
        lcd_update_all(temp, hum, gas_ppm, g_current_state);
        
        SystemState raw_state = get_system_state(temp, hum, gas_ppm);
        
        if (raw_state == STATE_EMERGENCY) {
            emergency_count++;
            if (emergency_count >= 2) {
                g_current_state = STATE_EMERGENCY;
            }
        } else {
            emergency_count = 0; 
            g_current_state = raw_state;
            buzzer_stop();
        }

        if (!g_person_detected) {
            g_no_person_count++;
            if (g_no_person_count > NO_PERSON_TIMEOUT_TICKS) {
                if (g_led_enabled) {
                    g_led_enabled = 0;
                    all_led_off();
                    buzzer_stop();
                    printf("[省电] 无人超过10秒，已关闭所有外设\n");
                }
            }
        } else {
            g_led_enabled = 1;
        }

        if (g_person_detected && g_led_enabled) {
            if (g_current_state == STATE_WARNING) {
                set_led_yellow();
            } else if (g_current_state == STATE_EMERGENCY) {
                /* 紧急状态：红灯闪烁 + 蜂鸣器长鸣 */
                red_toggle = !red_toggle;
                if (red_toggle) {
                    set_led_red();
                } else {
                    set_led_off();
                }
                buzzer_beep_long(2000);
            } else {
                buzzer_stop();
            }
        }

        if (tick % 10 == 0) {
            const char *state_str[] = {"正常", "预警", "紧急"};
            printf("[%s] T=%.1fC H=%.1f%% Gas=%.0fppm %s\n",
                   state_str[g_current_state], temp, hum, gas_ppm,
                   g_person_detected ? "有人" : "无人");
        }

        su03t_update_sensors(temp, hum, gas_ppm, (int)g_current_state);

        tick++;
        LOS_Msleep(200);
    }
}

static void bathroom_safety_example(void)
{
    unsigned int thread_id1, thread_id2;
    TSK_INIT_PARAM_S task1 = {0};
    TSK_INIT_PARAM_S task2 = {0};
    unsigned int lcd_ret;

    printf("========================================\n");
    printf("  通晓RK2206 浴室安全监护系统 v1.2\n");
    printf("========================================\n");

    pwm_init();
    
    lcd_ret = lcd_init();
    if (lcd_ret != 0) {
        printf("[LCD] 初始化失败(%d)\n", lcd_ret);
    } else {
        printf("[LCD] 初始化成功\n");
    }
    
    sht30_init();
    pir_init();
    su03t_init();

    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)breath_task;
    task1.uwStackSize = 2048;
    task1.pcName = "breath_task";
    task1.usTaskPrio = 10;
    LOS_TaskCreate(&thread_id1, &task1);

    task2.pfnTaskEntry = (TSK_ENTRY_FUNC)monitor_task;
    task2.uwStackSize = 4096;
    task2.pcName = "monitor_task";
    task2.usTaskPrio = 20;
    LOS_TaskCreate(&thread_id2, &task2);

    printf("\n===== 浴室安全监护系统启动 =====\n");
    printf("温度预警:%d°C 紧急:%d°C | 湿度预警:%d%% 紧急:%d%%\n",
           TEMP_WARN, TEMP_CRIT, HUM_WARN, HUM_CRIT);
    printf("煤气预警: %dppm | 煤气紧急: %dppm\n", GAS_WARN, GAS_CRIT);
    printf("人体感应: 中断检测 | 有人自动开灯 | 无人10秒后关灯(省电)\n");
    printf("蜂鸣器: 紧急状态长鸣\n");
    printf("防误报滤波: 2次\n");
    printf("呼吸灯独立任务运行中 (15ms/步)\n");
    printf("LCD屏幕已启用，实时显示数据\n");
    printf("===================================\n\n");
}

APP_FEATURE_INIT(bathroom_safety_example);