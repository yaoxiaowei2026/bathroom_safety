#include "ohos_init.h"
#include "iot_errno.h"
#include "iot_i2c.h"
#include "sht30.h"
#include "los_task.h"

/* sht30对应i2c */
#define SHT30_I2C_PORT EI2C0_M2

/* sht30地址 */
#define SHT30_I2C_ADDRESS 0x44

static uint8_t sht30_check_crc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum)
{
    uint8_t crc = 0xFF;
    uint8_t bit = 0;
    uint8_t byteCtr = 0;

    for (byteCtr = 0; byteCtr < nbrOfBytes; byteCtr++) {
        crc ^= data[byteCtr];
        for (bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return (crc == checksum);
}

static float sht30_calc_temperature(uint16_t u16sT)
{
    float temperature = 0;
    u16sT &= ~0x0003;
    temperature = (175 * (float)u16sT / 65535 - 45);
    return temperature;
}

static float sht30_calc_RH(uint16_t u16sRH)
{
    float humidityRH = 0;
    u16sRH &= ~0x0003;
    humidityRH = (100 * (float)u16sRH / 65535);
    return humidityRH;
}

void sht30_init(void)
{
    uint32_t ret = 0;
    uint8_t send_data[2] = {0x22, 0x36};

    ret = IoTI2cInit(SHT30_I2C_PORT, EI2C_FRE_400K);
    if (ret != 0) {
        printf("i2c init fail!\r\n");
        return;
    }

    IoTI2cWrite(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, send_data, 2);
}

void sht30_read_data(double *dat)
{
    uint8_t data[3];
    uint16_t tmp;
    uint8_t rc;
    uint8_t SHT30_Data_Buffer[6];
    uint8_t send_data[2] = {0xE0, 0x00};

    IoTI2cWrite(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, send_data, 2);
    LOS_Msleep(50);

    IoTI2cRead(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, SHT30_Data_Buffer, 6);

    data[0] = SHT30_Data_Buffer[0];
    data[1] = SHT30_Data_Buffer[1];
    data[2] = SHT30_Data_Buffer[2];
    rc = sht30_check_crc(data, 2, data[2]);
    if (rc) {
        tmp = ((uint16_t)data[0] << 8) | data[1];
        dat[0] = sht30_calc_temperature(tmp);
    }

    data[0] = SHT30_Data_Buffer[3];
    data[1] = SHT30_Data_Buffer[4];
    data[2] = SHT30_Data_Buffer[5];
    rc = sht30_check_crc(data, 2, data[2]);
    if (rc) {
        tmp = ((uint16_t)data[0] << 8) | data[1];
        dat[1] = sht30_calc_RH(tmp);
    }
}