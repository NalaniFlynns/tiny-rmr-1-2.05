#include "hal_opt3001.h"
#include "app_config.h"

volatile uint32_t g_als_lux_raw = 0;
volatile uint32_t g_als_lux_filtered = 0;
volatile uint8_t  g_als_sensor_status = 1;   /* 初始: ALS 未启用 */
volatile uint8_t  g_als_err_cnt = 0;
volatile uint32_t g_als_err_start_tick = 0;

#define I2C_SCL_HIGH()  DL_GPIO_disableOutput(PORT_I2C, PIN_SW_SCL)
#define I2C_SCL_LOW()   do { DL_GPIO_clearPins(PORT_I2C, PIN_SW_SCL); DL_GPIO_enableOutput(PORT_I2C, PIN_SW_SCL); } while(0)
#define I2C_SDA_HIGH()  DL_GPIO_disableOutput(PORT_I2C, PIN_SW_SDA)
#define I2C_SDA_LOW()   do { DL_GPIO_clearPins(PORT_I2C, PIN_SW_SDA); DL_GPIO_enableOutput(PORT_I2C, PIN_SW_SDA); } while(0)
#define I2C_SDA_READ()  ((DL_GPIO_readPins(PORT_I2C, PIN_SW_SDA) & PIN_SW_SDA) ? 1 : 0)

static void delay_i2c_us(uint32_t us) { delay_cycles((uint32_t)((uint64_t)MCU_CPU_FREQ_MHZ * us)); }
static void i2c_stop(void) { I2C_SDA_LOW(); delay_i2c_us(5); I2C_SCL_HIGH(); delay_i2c_us(5); I2C_SDA_HIGH(); delay_i2c_us(5); }

static bool i2c_start(void) {
    I2C_SDA_HIGH(); I2C_SCL_HIGH(); delay_i2c_us(5);
    if (I2C_SDA_READ() == 0) { i2c_stop(); return false; }
    I2C_SDA_LOW(); delay_i2c_us(5); I2C_SCL_LOW(); delay_i2c_us(5); return true;
}

static bool i2c_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) I2C_SDA_HIGH(); else I2C_SDA_LOW();
        data <<= 1; delay_i2c_us(5); I2C_SCL_HIGH(); delay_i2c_us(5); I2C_SCL_LOW();
    }
    I2C_SDA_HIGH(); delay_i2c_us(5); I2C_SCL_HIGH(); delay_i2c_us(5);
    bool ack = (I2C_SDA_READ() == 0); I2C_SCL_LOW(); delay_i2c_us(5); return ack;
}

static uint8_t i2c_read_byte(bool send_ack) {
    uint8_t data = 0; I2C_SDA_HIGH();
    for (int i = 0; i < 8; i++) {
        delay_i2c_us(5); I2C_SCL_HIGH(); delay_i2c_us(5);
        data = (data << 1) | I2C_SDA_READ(); I2C_SCL_LOW();
    }
    if (send_ack) I2C_SDA_LOW(); else I2C_SDA_HIGH();
    delay_i2c_us(5); I2C_SCL_HIGH(); delay_i2c_us(5); I2C_SCL_LOW(); delay_i2c_us(5); I2C_SDA_HIGH(); return data;
}

void opt3001_init(void) {
    DL_GPIO_clearPins(PORT_I2C, PIN_SW_SDA | PIN_SW_SCL);
    I2C_SDA_HIGH(); I2C_SCL_HIGH();
}

void opt3001_trigger_conversion(void) {
    i2c_start(); 
    i2c_write_byte(OPT3001_ADDR << 1); 
    i2c_write_byte(OPT3001_REG_CONFIG);  
    i2c_write_byte(OPT3001_CFG_HI); 
    i2c_write_byte(OPT3001_CFG_LO); 
    i2c_stop();
}

uint32_t opt3001_read_lux(void) {
    i2c_start(); i2c_write_byte(OPT3001_ADDR << 1); i2c_write_byte(OPT3001_REG_RESULT); i2c_stop();
    i2c_start(); i2c_write_byte((OPT3001_ADDR << 1) | 0x01);
    uint8_t msb = i2c_read_byte(true);   
    uint8_t lsb = i2c_read_byte(false);  
    i2c_stop(); 
    
    uint16_t raw = (msb << 8) | lsb;
    if (raw == 0xFFFF) return 0xFFFFFFFF; 
    
    uint16_t e = (raw >> 12) & 0x0F;
    if (e > 11) return 0xFFFFFFFF; 
    uint16_t m = raw & 0x0FFF;
    
    uint32_t lux = (1UL << e) * m;
    uint32_t res = (lux > OPT3001_MAX_LUX) ? OPT3001_MAX_LUX : lux;
    
    g_als_lux_raw = res;
    return res;
}