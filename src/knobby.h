#if !defined(KNOBBY_H)
#define KNOBBY_H

#include "Arduino.h"
#include "esp_adc_cal.h"
#include "esp_ota_ops.h"
#include "Wire.h"

#ifdef LILYGO_WATCH_2019_WITH_TOUCH
  #include "board/twatch2019_with_touch.h"
  #define LILYGO_WATCH_HAS_TOUCH
  #define LILYGO_WATCH_HAS_PCF8563
  #define LILYGO_WATCH_HAS_AXP202
  #define LILYGO_WATCH_HAS_BACKLIGHT
  #define LILYGO_WATCH_HAS_BUTTON
  #undef LILYGO_WATCH_HAS_BMA423
  #include "TTGO.h"
  TTGOClass *ttgo;
#endif

enum PowerStatus {
  PowerStatusUnknown = -1,
  PowerStatusPowered = 0,
  PowerStatusOnBattery = 1
};

class Knobby {
  public:
    Knobby();
    void setup();
    void loop();

    void printHeader();

    uint8_t batteryPercentage();
    float batteryVoltage();
    PowerStatus powerStatus();
    void setBatteryVoltage(float voltage);
    bool shouldUpdateBattery();
    void updateBattery();

  private:
    float _readSettledBatteryVoltage();
    uint8_t _batteryPercentage = 0;
    float _batteryVoltage = 0.0;
    const float _batteryVoltageLevels[101] = {
        3.200, 3.250, 3.300, 3.350, 3.400, 3.450, 3.500, 3.550, 3.600, 3.650, 3.700, 3.703, 3.706, 3.710, 3.713,
        3.716, 3.719, 3.723, 3.726, 3.729, 3.732, 3.735, 3.739, 3.742, 3.745, 3.748, 3.752, 3.755, 3.758, 3.761,
        3.765, 3.768, 3.771, 3.774, 3.777, 3.781, 3.784, 3.787, 3.790, 3.794, 3.797, 3.800, 3.805, 3.811, 3.816,
        3.821, 3.826, 3.832, 3.837, 3.842, 3.847, 3.853, 3.858, 3.863, 3.868, 3.874, 3.879, 3.884, 3.889, 3.895,
        3.900, 3.906, 3.911, 3.917, 3.922, 3.928, 3.933, 3.939, 3.944, 3.950, 3.956, 3.961, 3.967, 3.972, 3.978,
        3.983, 3.989, 3.994, 4.000, 4.008, 4.015, 4.023, 4.031, 4.038, 4.046, 4.054, 4.062, 4.069, 4.077, 4.085,
        4.092, 4.100, 4.111, 4.122, 4.133, 4.144, 4.156, 4.167, 4.178, 4.189, 4.200};
    enum BatteryReadingRate {
      BatteryReadingOff = 0,
      BatteryReadingFast = 50,
      BatteryReadingSlow = 3000
    };
    std::vector<float> _batteryReadings;
    const unsigned int _batteryReadingsMax = 10;
    BatteryReadingRate _batteryReadingRate = BatteryReadingOff;
    float _batteryVoltageThreshold = 4.33;
    unsigned int _batteryUpdatedMillis = 0;
    PowerStatus _powerStatus = PowerStatusUnknown;
    int _vref = 1100;
};

Knobby::Knobby() {
  _batteryReadings.reserve(_batteryReadingsMax);
}

void Knobby::setup() {
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    ttgo = TTGOClass::getWatch();
  #else
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
      log_d("eFuse Vref: %u mV", adc_chars.vref);
      _vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
      log_d("Two Point --> coeff_a:%umV coeff_b:%umV", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
      log_d("Default Vref: 1100mV");
    }
    pinMode(ADC_EN, OUTPUT);
  #endif

  loop();
}

void Knobby::loop() {
  if (shouldUpdateBattery()) updateBattery();
}

void Knobby::printHeader() {
  const esp_app_desc_t *desc = esp_ota_get_app_description();
  Serial.printf("\n\n    _                 _     _              |\n");
  Serial.printf("   | |               | |   | |             |\n");
  Serial.printf("   | |  _ ____   ___ | |__ | |__  _   _    |   mac %s\n", WiFi.macAddress().c_str());
  Serial.printf("   | |_/ )  _ \\ / _ \\|  _ \\|  _ \\| | | |   |   built %s %s\n", desc->date, desc->time);
  Serial.printf("   |  _ (| | | | |_| | |_) ) |_) ) |_| |   |   git version %s\n", GIT_VERSION);
  Serial.printf("   |_| \\_)_| |_|\\___/|____/|____/ \\__  |   |   esp-idf %s\n", desc->idf_ver);
  Serial.printf("                                 (____/    |\n");
  Serial.printf("    by milo winningham                     |\n");
  Serial.printf("    https://knobby.quadule.com             |\n");
  Serial.printf("                                           |\n");
  Serial.printf("___________________________________________|____________________________________\n\n");
  Serial.flush();
}

uint8_t Knobby::batteryPercentage() {
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    return ttgo->power->getBattPercentage();
  #else
    return _batteryPercentage;
  #endif
}

float Knobby::batteryVoltage() {
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    return ttgo->power->getBattVoltage();
  #else
    return _batteryVoltage;
  #endif
}

PowerStatus Knobby::powerStatus() {
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    return ttgo->power->isChargeing() ? PowerStatusPowered : PowerStatusOnBattery;
  #else
    return _powerStatus;
  #endif
}

bool Knobby::shouldUpdateBattery() {
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    return false;
  #else
    return _powerStatus == PowerStatusUnknown || _batteryReadings.size() < _batteryReadingsMax ||
           millis() - _batteryUpdatedMillis >= _batteryReadingRate;
  #endif
}

void Knobby::setBatteryVoltage(float voltage) {
  if (_batteryReadings.size() == _batteryReadingsMax) _batteryReadings.erase(_batteryReadings.begin());
  _batteryReadings.push_back(voltage);

  float averageVoltage = 0.0;
  for (auto v : _batteryReadings) averageVoltage += v;
  averageVoltage /= _batteryReadings.size();
  _batteryVoltage = averageVoltage;

  if (_batteryVoltage < _batteryVoltageLevels[0]) {
    _batteryPercentage = 0;
  } else if (_batteryVoltage > _batteryVoltageLevels[100]) {
    _batteryPercentage = 100;
  } else {
    auto itr = std::find_if(_batteryVoltageLevels, _batteryVoltageLevels + 100,
                            [&](float const &level) -> int { return _batteryVoltage <= level; });
    _batteryPercentage = std::distance(_batteryVoltageLevels, itr);
  }
}

float Knobby::_readSettledBatteryVoltage() {
  const float voltageSettlingThreshold = 0.015;
  float previousVoltage = _batteryVoltage;
  float voltage = 0.0;
  for (auto i = 0; i < 10; i++) {
    voltage = (analogRead(ADC_PIN) / 4095.0) * 2.0 * 3.3 * (_vref / 1000.0);
    if (abs(voltage - previousVoltage) < voltageSettlingThreshold) break;
    previousVoltage = voltage;
    delayMicroseconds(100 + random(100));
  }
  return voltage;
}

void Knobby::updateBattery() {
  float newVoltage = 0.0;
  switch (_batteryReadingRate) {
    case BatteryReadingOff:
      digitalWrite(ADC_EN, HIGH);
      _batteryReadingRate = BatteryReadingFast;
      break;
    case BatteryReadingSlow:
      digitalWrite(ADC_EN, HIGH);
      newVoltage = _readSettledBatteryVoltage();
      digitalWrite(ADC_EN, LOW);
      break;
    case BatteryReadingFast:
      newVoltage = _readSettledBatteryVoltage();
      break;
  }

  if (newVoltage > 0.0) {
    PowerStatus oldPowerStatus = _powerStatus;
    _powerStatus = newVoltage < _batteryVoltageThreshold ? PowerStatusOnBattery : PowerStatusPowered;
    bool powerStatusChanged = _powerStatus != oldPowerStatus && oldPowerStatus != PowerStatusUnknown;
    float voltageChange = abs(newVoltage - _batteryVoltage);
    if (powerStatusChanged || voltageChange > 0.1) {
      digitalWrite(ADC_EN, HIGH);
      _batteryReadings.clear();
      _batteryReadingRate = BatteryReadingFast;
    } else if (_batteryReadings.size() == _batteryReadingsMax) {
      digitalWrite(ADC_EN, LOW);
      _batteryReadingRate = BatteryReadingSlow;
    }

    setBatteryVoltage(newVoltage);
    _batteryUpdatedMillis = millis();
  }
}

#endif // KNOBBY_H
