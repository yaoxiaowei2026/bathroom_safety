# 浴室安全监护系统 (Bathroom Safety Monitoring System)

## 作品简介

基于 OpenHarmony LiteOS-M + 瑞芯微 RK2206 的智能浴室安全监护系统。集成多传感器融合、语音交互、LCD显示、云端告警等功能，实时监测浴室环境安全。

## 硬件平台

- **主控**: 瑞芯微 RK2206 (Cortex-M4)
- **系统**: OpenHarmony LiteOS-M
- **开发板**: 通晓 E53 物联网开发板

## 功能特性

| 功能 | 描述 |
|------|------|
| 温湿度监测 | SHT30传感器，I2C通信，CRC校验 |
| 煤气泄漏检测 | MQ-2传感器，SPI/ADC分时复用，PPM浓度计算 |
| 人体感应 | 红外传感器，GPIO中断，10秒无人超时省电 |
| 语音交互 | SU-03T模块，一问一答，支持温度/湿度/煤气/状态查询 |
| LCD显示 | ST7789S驱动，320x240 TFT，中文界面 |
| 三级告警 | 正常(绿灯呼吸)/预警(黄灯)/紧急(红灯+蜂鸣器2000Hz) |
| 云端上报 | 预留华为云IoT MQTT接口（开发中，当前串口模拟输出） |

## 三级告警阈值

| 等级 | 温度 | 湿度 | 煤气 | 灯光 | 蜂鸣器 |
|------|------|------|------|------|--------|
| 正常 | <34°C | <75% | <400ppm | 绿灯呼吸 | 关 |
| 预警 | ≥34°C | ≥75% | ≥400ppm | 黄灯常亮 | 关 |
| 紧急 | ≥35°C | ≥85% | ≥800ppm | 红灯常亮 | 2000Hz长鸣 |

## 通信协议

### SU-03T语音模块协议 (UART2, 115200bps)
- **请求帧**: `03 XX` (XX=01温度/02湿度/03煤气/04状态)
- **回复帧**: `AA 55 [msg_id] [data] 55 AA`
- 温度/湿度/煤气: 8字节double类型
- 状态: 空数据帧, msg_id区分(05正常/06预警/07紧急)

### 华为云IoT MQTT（预留接口，开发中）
- 设备ID: `6a467d58c00ccb6d4b625038_bathroom_01`
- 上报属性: temperature, humidity, gas, status
- 下行命令: light_control
- 当前状态: 代码框架已完成，串口模拟输出，待WiFi环境就绪后联调

## 目录结构

```
bathroom_safety/
├── bathroom_safety.c    # 主程序: 任务调度、状态机、传感器整合
├── sht30.c/h            # SHT30温湿度传感器驱动 (I2C)
├── mq2.c/h              # MQ-2煤气传感器驱动 (ADC)
├── su_03t.c/h           # SU-03T语音模块驱动 (UART2)
├── lcd.c/h              # ST7789S LCD显示驱动 (SPI)
├── lcd_font.h           # 中文字库数据
├── iot.c/h              # 华为云IoT MQTT对接（预留，未参与编译）
├── wifi_connect.c/h     # WiFi连接模块（预留，未参与编译）
├── mqtt_client.c/h      # MQTT客户端（预留，未参与编译）
├── music.c/h            # 语音/音乐模块（预留）
├── BUILD.gn             # 构建配置
├── iot_config.h         # WiFi配置
├── endian.h             # 字节序定义
├── stdint.h             # 标准整数类型桩
├── stddef.h             # 标准定义桩
├── sys/                 # POSIX兼容桩头文件
└── arpa/ netinet/       # 网络兼容桩头文件
```

## 关键技术点

1. **SPI/ADC分时复用**: MQ-2和LCD共享GPIO0_PC2引脚，通过 `spi_to_adc_switch()` / `adc_to_spi_switch()` 动态切换
2. **防误报滤波**: 连续3次紧急状态才触发告警
3. **省电模式**: 无人时10秒后关闭LCD/LED/蜂鸣器
4. **一问一答语音**: 被动监听UART，收到请求才回复
5. **SHT30 CRC校验**: 确保温湿度数据可靠性
