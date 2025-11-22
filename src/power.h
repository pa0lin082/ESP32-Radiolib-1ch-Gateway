#pragma once


#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#include <Esp.h>
#include "variant.h"


#ifndef NUM_OCV_POINTS
#define NUM_OCV_POINTS 11
#endif

#ifndef DEFAULT_VREF
#define DEFAULT_VREF 1100
#endif

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

#ifndef OCV_ARRAY
#ifdef CELL_TYPE_LIFEPO4
#define OCV_ARRAY 3400, 3350, 3320, 3290, 3270, 3260, 3250, 3230, 3200, 3120, 3000
#elif defined(CELL_TYPE_LEADACID)
#define OCV_ARRAY 2120, 2090, 2070, 2050, 2030, 2010, 1990, 1980, 1970, 1960, 1950
#elif defined(CELL_TYPE_ALKALINE)
#define OCV_ARRAY 1580, 1400, 1350, 1300, 1280, 1250, 1230, 1190, 1150, 1100, 1000
#elif defined(CELL_TYPE_NIMH)
#define OCV_ARRAY 1400, 1300, 1280, 1270, 1260, 1250, 1240, 1230, 1210, 1150, 1000
#elif defined(CELL_TYPE_LTO)
#define OCV_ARRAY 2700, 2560, 2540, 2520, 2500, 2460, 2420, 2400, 2380, 2320, 1500
#elif defined(TRACKER_T1000_E)
#define OCV_ARRAY 4190, 4042, 3957, 3885, 3820, 3776, 3746, 3725, 3696, 3644, 3100
#elif defined(HELTEC_MESH_POCKET_BATTERY_5000)
#define OCV_ARRAY 4300, 4240, 4120, 4000, 3888, 3800, 3740, 3698, 3655, 3580, 3400
#elif defined(HELTEC_MESH_POCKET_BATTERY_10000)
#define OCV_ARRAY 4100, 4060, 3960, 3840, 3729, 3625, 3550, 3500, 3420, 3345, 3100
#elif defined(SEEED_WIO_TRACKER_L1)
#define OCV_ARRAY 4200, 3876, 3826, 3763, 3713, 3660, 3573, 3485, 3422, 3359, 3300
#elif defined(SEEED_SOLAR_NODE)
#define OCV_ARRAY 4200, 3986, 3922, 3812, 3734, 3645, 3527, 3420, 3281, 3087, 2786
#elif defined(R1_NEO)
#define OCV_ARRAY 4330, 4292, 4254, 4216, 4178, 4140, 4102, 4064, 4026, 3988, 3950
#else // LiIon
#define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#endif
#endif

 /*Note: 12V lead acid is 6 cells, most board accept only 1 cell LiIon/LiPo*/
#ifndef NUM_CELLS
#define NUM_CELLS 1
#endif

/// C++ v17+ clamp function, limits a given value to a range defined by lo and hi
template <class T> constexpr const T &clamp(const T &v, const T &lo, const T &hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

 
 
 #if defined(BATTERY_PIN) && defined(ARCH_ESP32)
 
 #ifndef BAT_MEASURE_ADC_UNIT // ADC1 is default
 static const adc1_channel_t adc_channel = ADC_CHANNEL;
 static const adc_unit_t unit = ADC_UNIT_1;
 #else // ADC2
 static const adc2_channel_t adc_channel = ADC_CHANNEL;
 static const adc_unit_t unit = ADC_UNIT_2;
 RTC_NOINIT_ATTR uint64_t RTC_reg_b;
 
 #endif // BAT_MEASURE_ADC_UNIT
 
 esp_adc_cal_characteristics_t *adc_characs = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
 #ifndef ADC_ATTENUATION
 static const adc_atten_t atten = ADC_ATTEN_DB_12;
 #else
 static const adc_atten_t atten = ADC_ATTENUATION;
 #endif
 #endif // BATTERY_PIN && ARCH_ESP32
 
 #ifdef EXT_CHRG_DETECT
 #ifndef EXT_CHRG_DETECT_MODE
 static const uint8_t ext_chrg_detect_mode = INPUT;
 #else
 static const uint8_t ext_chrg_detect_mode = EXT_CHRG_DETECT_MODE;
 #endif
 #ifndef EXT_CHRG_DETECT_VALUE
 static const uint8_t ext_chrg_detect_value = HIGH;
 #else
 static const uint8_t ext_chrg_detect_value = EXT_CHRG_DETECT_VALUE;
 #endif
 #endif
 

 
 // Copy of the base class defined in axp20x.h.
 // I'd rather not include axp20x.h as it brings Wire dependency.
 class HasBatteryLevel
 {
   public:
     /**
      * Battery state of charge, from 0 to 100 or -1 for unknown
      */
     virtual int getBatteryPercent() { return -1; }
 
     /**
      * The raw voltage of the battery or NAN if unknown
      */
     virtual uint16_t getBattVoltage() { return 0; }
 
     /**
      * return true if there is a battery installed in this unit
      */
     virtual bool isBatteryConnect() { return false; }
 
     virtual bool isVbusIn() { return false; }
     virtual bool isCharging() { return false; }
 };

 
 bool pmu_irq = false;
 

 #ifndef AREF_VOLTAGE
 #define AREF_VOLTAGE 3.3
 #endif
 
 /**
  * If this board has a battery level sensor, set this to a valid implementation
  */
 static HasBatteryLevel *batteryLevel; // Default to NULL for no battery level sensor
 
 #ifdef BATTERY_PIN
 
 static void adcEnable()
 {
 #ifdef ADC_CTRL // enable adc voltage divider when we need to read
     pinMode(ADC_CTRL, INPUT);
     uint8_t adc_ctl_enable_value = !(digitalRead(ADC_CTRL));
     pinMode(ADC_CTRL, OUTPUT);
     digitalWrite(ADC_CTRL, adc_ctl_enable_value);
     delay(10);
 #endif
 }
 
 static void adcDisable()
 {
 #ifdef ADC_CTRL // disable adc voltage divider when we need to read
 #ifdef ADC_USE_PULLUP
     pinMode(ADC_CTRL, INPUT_PULLDOWN);
 #else
 #ifdef HELTEC_V4
     pinMode(ADC_CTRL, ANALOG);
 #else
     digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
 #endif
 #endif
 #endif
 }
 
 #endif
 
 /**
  * A simple battery level sensor that assumes the battery voltage is attached via a voltage-divider to an analog input
  */
 class AnalogBatteryLevel : public HasBatteryLevel
 {
   public:
     /**
      * Battery state of charge, from 0 to 100 or -1 for unknown
      */
     virtual int getBatteryPercent() override
     {

 
         float v = getBattVoltage();
 
         if (v < noBatVolt)
             return -1; // If voltage is super low assume no battery installed

         /**
          * @brief   Battery voltage lookup table interpolation to obtain a more
          * precise percentage rather than the old proportional one.
          * @author  Gabriele Russo
          * @date    06/02/2024
          */
         float battery_SOC = 0.0;
         uint16_t voltage = v / NUM_CELLS; // single cell voltage (average)
         for (int i = 0; i < NUM_OCV_POINTS; i++) {
             if (OCV[i] <= voltage) {
                 if (i == 0) {
                     battery_SOC = 100.0; // 100% full
                 } else {
                     // interpolate between OCV[i] and OCV[i-1]
                     battery_SOC = (float)100.0 / (NUM_OCV_POINTS - 1.0) *
                                   (NUM_OCV_POINTS - 1.0 - i + ((float)voltage - OCV[i]) / (OCV[i - 1] - OCV[i]));
                 }
                 break;
             }
         }
         return clamp((int)(battery_SOC), 0, 100);
     }
 
     /**
      * The raw voltage of the batteryin millivolts or NAN if unknown
      */
     virtual uint16_t getBattVoltage() override
     {
 

 
 
 #ifndef ADC_MULTIPLIER
 #define ADC_MULTIPLIER 2.0
 #endif
 
 #ifndef BATTERY_SENSE_SAMPLES
 #define BATTERY_SENSE_SAMPLES                                                                                                    \
     15 // Set the number of samples, it has an effect of increasing sensitivity in complex electromagnetic environment.
 #endif
 
 #ifdef BATTERY_PIN
         // Override variant or default ADC_MULTIPLIER if we have the override pref
         float operativeAdcMultiplier = ADC_MULTIPLIER;
         // Do not call analogRead() often.
         const uint32_t min_read_interval = 5000;
         if (!initial_read_done || (millis() - last_read_time_ms > min_read_interval)) {
             last_read_time_ms = millis();
 
             uint32_t raw = 0;
             float scaled = 0;
 
             adcEnable();
 #ifdef ARCH_ESP32 // ADC block for espressif platforms
             raw = espAdcRead();
             scaled = esp_adc_cal_raw_to_voltage(raw, adc_characs);
             scaled *= operativeAdcMultiplier;
 #else // block for all other platforms
             for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
                 raw += analogRead(BATTERY_PIN);
             }
             raw = raw / BATTERY_SENSE_SAMPLES;
             scaled = operativeAdcMultiplier * ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * raw;
 #endif
             adcDisable();
 
             if (!initial_read_done) {
                 // Flush the smoothing filter with an ADC reading, if the reading is plausibly correct
                 if (scaled > last_read_value)
                     last_read_value = scaled;
                 initial_read_done = true;
             } else {
                 // Already initialized - filter this reading
                 last_read_value += (scaled - last_read_value) * 0.5; // Virtual LPF
             }
 
             // LOG_DEBUG("battery gpio %d raw val=%u scaled=%u filtered=%u", BATTERY_PIN, raw, (uint32_t)(scaled), (uint32_t)
             // (last_read_value));
         }
         return last_read_value;
 #endif // BATTERY_PIN
         return 0;
     }
 
 #if defined(ARCH_ESP32) && !defined(HAS_PMU) && defined(BATTERY_PIN)
     /**
      * ESP32 specific function for getting calibrated ADC reads
      */
     uint32_t espAdcRead()
     {
 
         uint32_t raw = 0;
         uint8_t raw_c = 0; // raw reading counter
 
 #ifndef BAT_MEASURE_ADC_UNIT // ADC1
         for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
             int val_ = adc1_get_raw(adc_channel);
             if (val_ >= 0) { // save only valid readings
                 raw += val_;
                 raw_c++;
             }
             // delayMicroseconds(100);
         }
 #else                            // ADC2
 #ifdef CONFIG_IDF_TARGET_ESP32S3 // ESP32S3
         // ADC2 wifi bug workaround not required, breaks compile
         // On ESP32S3, ADC2 can take turns with Wifi (?)
 
         int32_t adc_buf;
         esp_err_t read_result;
 
         // Multiple samples
         for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
             adc_buf = 0;
             read_result = -1;
 
             read_result = adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf);
             if (read_result == ESP_OK) {
                 raw += adc_buf;
                 raw_c++; // Count valid samples
             } else {
                 LOG_DEBUG("An attempt to sample ADC2 failed");
             }
         }
 
 #else  // Other ESP32
         int32_t adc_buf = 0;
         for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
             // ADC2 wifi bug workaround, see
             // https://github.com/espressif/arduino-esp32/issues/102
             WRITE_PERI_REG(SENS_SAR_READ_CTRL2_REG, RTC_reg_b);
             SET_PERI_REG_MASK(SENS_SAR_READ_CTRL2_REG, SENS_SAR2_DATA_INV);
             adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf);
             raw += adc_buf;
             raw_c++;
         }
 #endif // BAT_MEASURE_ADC_UNIT
 
 #endif // End BAT_MEASURE_ADC_UNIT
         return (raw / (raw_c < 1 ? 1 : raw_c));
     }
 #endif
 
     /**
      * return true if there is a battery installed in this unit
      */
     // if we have a integrated device with a battery, we can assume that the battery is always connected
 #ifdef BATTERY_IMMUTABLE
     virtual bool isBatteryConnect() override { return true; }
 #elif defined(ADC_V)
     virtual bool isBatteryConnect() override
     {
         int lastReading = digitalRead(ADC_V);
         // 判断值是否变化
         for (int i = 2; i < 500; i++) {
             int reading = digitalRead(ADC_V);
             if (reading != lastReading) {
                 return false; // 有变化，USB供电, 没接电池
             }
         }
 
         return true;
     }
 #else
     virtual bool isBatteryConnect() override { return getBatteryPercent() != -1; }
 #endif
 
     /// If we see a battery voltage higher than physics allows - assume charger is pumping
     /// in power
     /// On some boards we don't have the power management chip (like AXPxxxx)
     /// so we use EXT_PWR_DETECT GPIO pin to detect external power source
     virtual bool isVbusIn() override
     {
 #ifdef EXT_PWR_DETECT
 #if defined(HELTEC_CAPSULE_SENSOR_V3) || defined(HELTEC_SENSOR_HUB)
         // if external powered that pin will be pulled down
         if (digitalRead(EXT_PWR_DETECT) == LOW) {
             return true;
         }
         // if it's not LOW - check the battery
 #else
         // if external powered that pin will be pulled up
         if (digitalRead(EXT_PWR_DETECT) == HIGH) {
             return true;
         }
         // if it's not HIGH - check the battery
 #endif
 #endif
         return getBattVoltage() > chargingVolt;
     }
 
     /// Assume charging if we have a battery and external power is connected.
     /// we can't be smart enough to say 'full'?
     virtual bool isCharging() override
     {

         // by default, we check the battery voltage only
         return isVbusIn();
     }
 
   private:
     /// If we see a battery voltage higher than physics allows - assume charger is pumping
     /// in power
 
     /// For heltecs with no battery connected, the measured voltage is 2204, so
     // need to be higher than that, in this case is 2500mV (3000-500)
     const uint16_t OCV[NUM_OCV_POINTS] = {OCV_ARRAY};
     const float chargingVolt = (OCV[0] + 10) * NUM_CELLS;
     const float noBatVolt = (OCV[NUM_OCV_POINTS - 1] - 500) * NUM_CELLS;
     // Start value from minimum voltage for the filter to not start from 0
     // that could trigger some events.
     // This value is over-written by the first ADC reading, it the voltage seems reasonable.
     bool initial_read_done = false;
     float last_read_value = (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS);
     uint32_t last_read_time_ms = 0;
 

 };
 
 static AnalogBatteryLevel analogLevel;
 

 
 bool analogInit()
 {
 #ifdef EXT_PWR_DETECT
 #if defined(HELTEC_CAPSULE_SENSOR_V3) || defined(HELTEC_SENSOR_HUB)
     pinMode(EXT_PWR_DETECT, INPUT_PULLUP);
 #else
     pinMode(EXT_PWR_DETECT, INPUT);
 #endif
 #endif
 #ifdef EXT_CHRG_DETECT
     pinMode(EXT_CHRG_DETECT, ext_chrg_detect_mode);
 #endif
 
 #ifdef BATTERY_PIN
     Serial.printf("[POWER] Use analog input %d for battery level\n", BATTERY_PIN);
 
     // disable any internal pullups
     pinMode(BATTERY_PIN, INPUT);
 
 #ifndef BATTERY_SENSE_RESOLUTION_BITS
 #define BATTERY_SENSE_RESOLUTION_BITS 10
 #endif
 
 #ifdef ARCH_ESP32 // ESP32 needs special analog stuff
 
 #ifndef ADC_WIDTH // max resolution by default
     static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
 #else
     static const adc_bits_width_t width = ADC_WIDTH;
 #endif
 #ifndef BAT_MEASURE_ADC_UNIT // ADC1
     adc1_config_width(width);
     adc1_config_channel_atten(adc_channel, atten);
 #else // ADC2
     adc2_config_channel_atten(adc_channel, atten);
 #ifndef CONFIG_IDF_TARGET_ESP32S3
     // ADC2 wifi bug workaround
     // Not required with ESP32S3, breaks compile
     RTC_reg_b = READ_PERI_REG(SENS_SAR_READ_CTRL2_REG);
 #endif
 #endif
     // calibrate ADC
     esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_characs);
     // show ADC characterization base
     if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
         Serial.printf("[POWER] ADC config based on Two Point values stored in eFuse\n");
     } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
         Serial.printf("ADC config based on reference voltage stored in eFuse");
     }
 #ifdef CONFIG_IDF_TARGET_ESP32S3
     // ESP32S3
     else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP_FIT) {
         Serial.printf("ADC config based on Two Point values and fitting curve coefficients stored in eFuse");
     }
 #endif
     else {
         Serial.printf("ADC config based on default reference voltage");
     }
 #endif // ARCH_ESP32
 
 #ifdef ARCH_NRF52
 #ifdef VBAT_AR_INTERNAL
     analogReference(VBAT_AR_INTERNAL);
 #else
     analogReference(AR_INTERNAL); // 3.6V
 #endif
 #endif // ARCH_NRF52
 
 #ifndef ARCH_ESP32
     analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
 #endif
 
     batteryLevel = &analogLevel;
     return true;
 #else
     return false;
 #endif
 }
 
 /**
  * Initializes the Power class.
  *
  * @return true if the setup was successful, false otherwise.
  */
 
 
 
 void reboot()
 {
   ESP.restart();
   
 }
 
//  void Power::shutdown()
//  {
 
//  #if HAS_SCREEN
//      if (screen) {
//  #ifdef T_DECK_PRO
//          screen->showSimpleBanner("Device is powered off.\nConnect USB to start!", 0); // T-Deck Pro has no power button
//  #else
//          screen->showSimpleBanner("Shutting Down...", 0); // stays on screen
//  #endif
//      }
//  #endif
//  #if !defined(ARCH_STM32WL)
//      playShutdownMelody();
//  #endif
//      nodeDB->saveToDisk();
 
//  #if defined(ARCH_NRF52) || defined(ARCH_ESP32) || defined(ARCH_RP2040)
//  #ifdef PIN_LED1
//      ledOff(PIN_LED1);
//  #endif
//  #ifdef PIN_LED2
//      ledOff(PIN_LED2);
//  #endif
//  #ifdef PIN_LED3
//      ledOff(PIN_LED3);
//  #endif
//      doDeepSleep(DELAY_FOREVER, true, true);
//  #elif defined(ARCH_PORTDUINO)
//      exit(EXIT_SUCCESS);
//  #else
//      LOG_WARN("FIXME implement shutdown for this platform");
//  #endif
//  }
 

 
 