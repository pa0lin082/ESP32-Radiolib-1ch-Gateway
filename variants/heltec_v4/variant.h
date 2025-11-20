#ifndef VARIANT_H
#define VARIANT_H

// LED
#define LED_PIN 35

// OLED Display (SSD1306/SSD1315 compatible)
#define USE_SSD1306
#define RESET_OLED 21
#define I2C_SDA 17
#define I2C_SCL 18

// Power control
#define VEXT_ENABLE 36  // active low, powers the oled display and the lora antenna boost
#define BUTTON_PIN 0

// Battery monitoring
#define ADC_CTRL 37
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5
#define ADC_MULTIPLIER 4.9 * 1.045

// LoRa SX1262
#define USE_SX1262
#define LORA_DIO0 -1        // Not connected on SX1262
#define LORA_RESET 12
#define LORA_DIO1 14        // SX1262 IRQ
#define LORA_DIO2 13        // SX1262 BUSY
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

// SX1262 configuration
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8


#define DISPLAY_ENABLED true
#endif // VARIANT_H


