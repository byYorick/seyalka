#pragma once

/* MKS TinyBee V1.0 I2S shift register pins. Do not remap. */
#define TB_I2S_BCK_GPIO          25
#define TB_I2S_WS_GPIO           26
#define TB_I2S_DATA_GPIO         27

/* Direct inputs. */
#define TB_GPIO_CASSETTE_SENSOR  35
#define TB_GPIO_DIBBLER_HOME     33
#define TB_GPIO_TRANSFER_HOME    32
#define TB_GPIO_START_STOP       22
#define TB_GPIO_VACUUM_ADC       36
#define TB_GPIO_ADC_AUX          39
#define TB_GPIO_ADC_TH2          34

/* Encoder on EXP headers. */
#define TB_GPIO_ENCODER_A        14
#define TB_GPIO_ENCODER_B        12
#define TB_GPIO_ENCODER_BUTTON   13

/* ILI9341 SPI pins. */
#define TB_GPIO_LCD_SCK          18
#define TB_GPIO_LCD_MOSI         23
#define TB_GPIO_LCD_CS           5
#define TB_GPIO_LCD_DC           21
#define TB_GPIO_LCD_RST          4
#define TB_GPIO_LCD_BL           2

/* I2SO stepper bits. */
#define TB_I2SO_X_EN             0
#define TB_I2SO_X_STEP           1
#define TB_I2SO_X_DIR            2
#define TB_I2SO_Y_EN             3
#define TB_I2SO_Y_STEP           4
#define TB_I2SO_Y_DIR            5
#define TB_I2SO_Z_EN             6
#define TB_I2SO_Z_STEP           7
#define TB_I2SO_Z_DIR            8
#define TB_I2SO_E0_EN            9
#define TB_I2SO_E0_STEP          10
#define TB_I2SO_E0_DIR           11
#define TB_I2SO_E1_EN            12
#define TB_I2SO_E1_STEP          13
#define TB_I2SO_E1_DIR           14

/* I2SO power outputs. */
#define TB_I2SO_HE_BED           16
#define TB_I2SO_HE0              17
#define TB_I2SO_HE1              18
#define TB_I2SO_FAN1             19
#define TB_I2SO_FAN2             20
#define TB_I2SO_BEEPER           21

