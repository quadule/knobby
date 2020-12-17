#if !defined(KNOBBY_H)
#define KNOBBY_H

#include "Arduino.h"
#include "esp_adc_cal.h"
#include "Wire.h"

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

    uint8_t batteryPercentage();
    float batteryVoltage();
    PowerStatus powerStatus();
    void setBatteryVoltage(float voltage);
    bool shouldUpdateBattery();
    void updateBattery();

  private:
    void _readSettledBatteryVoltage();
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
      BatteryReadingFast = 70,
      BatteryReadingSlow = 3000
    };
    std::vector<float> _batteryReadings;
    const unsigned int _batteryReadingsMax = 5;
    unsigned int _batteryReadingMillis = 0;
    BatteryReadingRate _batteryReadingRate = BatteryReadingOff;
    float _batteryVoltageThreshold = 4.4;
    unsigned int _batteryUpdatedMillis = 0;
    PowerStatus _powerStatus = PowerStatusUnknown;
    int _vref = 1100;
};

Knobby::Knobby() {
  _batteryReadings.reserve(_batteryReadingsMax);
}

void Knobby::setup() {
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6,
                                                          (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    log_d("eFuse Vref: %u mV", adc_chars.vref);
    _vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    log_d("Two Point --> coeff_a:%umV coeff_b:%umV", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
    log_d("Default Vref: 1100mV");
  }

  pinMode(ADC_EN, OUTPUT);
  loop();
}

void Knobby::loop() {
  if (shouldUpdateBattery()) updateBattery();
}

uint8_t Knobby::batteryPercentage() {
  return _batteryPercentage;
}

float Knobby::batteryVoltage() {
  return _batteryVoltage;
}

PowerStatus Knobby::powerStatus() {
  return _powerStatus;
}

bool Knobby::shouldUpdateBattery() {
  return _batteryUpdatedMillis == 0 || millis() - _batteryUpdatedMillis >= _batteryReadingRate;
}

void Knobby::setBatteryVoltage(float voltage) {
  if (_batteryReadings.size() == _batteryReadingsMax) _batteryReadings.erase(_batteryReadings.begin());
  _batteryReadings.push_back(voltage);

  float averageVoltage = 0.0;
  for (auto v : _batteryReadings) averageVoltage += v;
  averageVoltage /= (float)_batteryReadings.size();
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

void Knobby::_readSettledBatteryVoltage() {
  const float voltageSettlingThreshold = 0.015;
  float previousVoltage = _batteryVoltage;
  float voltage = 0.0;
  for (auto i = 0; i < 10; i++) {
    voltage = (analogRead(ADC_PIN) / 4095.0) * 2.0 * 3.3 * (_vref / 1000.0);
    if (abs(voltage - previousVoltage) < voltageSettlingThreshold) break;
    previousVoltage = voltage;
    delayMicroseconds(100 + random(100));
  }

  PowerStatus oldPowerStatus = _powerStatus;
  _powerStatus = voltage < _batteryVoltageThreshold ? PowerStatusOnBattery : PowerStatusPowered;
  if (_powerStatus != oldPowerStatus && oldPowerStatus != PowerStatusUnknown) {
    _batteryReadings.clear();
    _batteryReadingRate = BatteryReadingFast;
  }

  setBatteryVoltage(voltage);
}

void Knobby::updateBattery() {
  switch (_batteryReadingRate) {
    case BatteryReadingOff:
      digitalWrite(ADC_EN, HIGH);
      _batteryReadingRate = BatteryReadingFast;
      break;
    case BatteryReadingSlow:
      _readSettledBatteryVoltage();
      digitalWrite(ADC_EN, LOW);
      _batteryReadingRate = BatteryReadingOff;
      break;
    case BatteryReadingFast:
      _readSettledBatteryVoltage();
      if (_batteryReadings.size() == _batteryReadingsMax && _batteryPercentage > 0) {
        _batteryReadingRate = BatteryReadingSlow;
      }
      break;
  }
  _batteryUpdatedMillis = millis();
}

#endif // KNOBBY_H
