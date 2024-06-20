#if !defined(KNOBBY_H)
#define KNOBBY_H

#include <Arduino.h>
#include <base64.h>
#include <esp_adc_cal.h>
#include <esp_ota_ops.h>
#include <Preferences.h>
#include <Wire.h>

#ifdef LILYGO_WATCH_2019_WITH_TOUCH
  #include <board/twatch2019_with_touch.h>
  #define LILYGO_WATCH_HAS_TOUCH
  #define LILYGO_WATCH_HAS_PCF8563
  #define LILYGO_WATCH_HAS_AXP202
  #define LILYGO_WATCH_HAS_BACKLIGHT
  #define LILYGO_WATCH_HAS_BUTTON
  #undef LILYGO_WATCH_HAS_BMA423
  #include <TTGO.h>
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

    const String& name();
    void setName(const char *name);

    const String& password();
    void setPassword(const char *password);

    bool flippedDisplay();
    void setFlippedDisplay(bool flipped);
    int buttonPin();
    void setButtonPin(int pin);
    int rotaryAPin();
    void setRotaryAPin(int pin);
    int rotaryBPin();
    void setRotaryBPin(int pin);
    int pulseCount();
    void setPulseCount(int count);

    void printHeader();
    void resetSettings();

    uint8_t batteryPercentage();
    float batteryVoltage();
    PowerStatus powerStatus();
    void setBatteryVoltage(float voltage);
    bool shouldUpdateBattery();
    void updateBattery();

  private:
    String _name;
    String _password;
    Preferences _preferences;

    bool _flippedDisplay = false;
    int _buttonPin  = ROTARY_ENCODER_BUTTON_PIN;
    int _rotaryAPin = ROTARY_ENCODER_A_PIN;
    int _rotaryBPin = ROTARY_ENCODER_B_PIN;
    int _pulseCount = ROTARY_ENCODER_PULSE_COUNT;

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
    float _batteryVoltageThreshold = 4.3;
    unsigned int _batteryUpdatedMillis = 0;
    PowerStatus _powerStatus = PowerStatusUnknown;
};

Knobby::Knobby() {
  _batteryReadings.reserve(_batteryReadingsMax);
}

void Knobby::setup() {
  _preferences.begin("knobby");

  _flippedDisplay = _preferences.getBool("flipDisplay", _flippedDisplay);
  _buttonPin = _preferences.getChar("buttonPin", _buttonPin);
  _rotaryAPin = _preferences.getChar("rotaryAPin", _rotaryAPin);
  _rotaryBPin = _preferences.getChar("rotaryBPin", _rotaryBPin);
  _pulseCount = _preferences.getChar("pulseCount", _pulseCount);

  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    ttgo = TTGOClass::getWatch();
  #else
    if (ADC_EN >= 0) pinMode(ADC_EN, OUTPUT);
  #endif

  loop();
}

void Knobby::loop() {
  if (shouldUpdateBattery()) updateBattery();
}

const String& Knobby::name() {
  if (_name.isEmpty() && _preferences.getType("name") == PT_STR) {
    _name = _preferences.getString("name");
  }
  if (_name.isEmpty()) {
    _name = "knobby-";
    String suffix = WiFi.macAddress().substring(12, 17);
    suffix.replace(":", "");
    suffix.toLowerCase();
    _name.concat(suffix);
    setName(_name.c_str());
  }
  return _name;
}

void Knobby::setName(const char *name) {
  _name = name;
  _preferences.putString("name", _name);
}

const String& Knobby::password() {
  if (_password.isEmpty() && _preferences.getType("password") == PT_STR) {
    _password = _preferences.getString("password");
  }
  if (_password.isEmpty()) {
    unsigned char randomBytes[10];
    for (auto i=0; i<10; i++) randomBytes[i] = random(256);
    _password = base64::encode((const uint8_t *)&randomBytes, 10).substring(0, 10);
    _password.toLowerCase();
    _password.replace('1', '!');
    _password.replace('l', '-');
    _password.replace('+', '@');
    _password.replace('/', '&');
    setPassword(_password.c_str());
  }
  return _password;
}

void Knobby::setPassword(const char *password) {
  _password = password;
  _preferences.putString("password", _password);
}

bool Knobby::flippedDisplay() { return _flippedDisplay; }

void Knobby::setFlippedDisplay(bool flip) {
  _flippedDisplay = flip;
  _preferences.putBool("flipDisplay", _flippedDisplay);
}

int Knobby::buttonPin() { return _buttonPin; }

void Knobby::setButtonPin(int pin) {
  if (pin >= 0 && pin <= 39) {
    _buttonPin = pin;
    _preferences.putInt("buttonPin", _buttonPin);
  }
}

int Knobby::rotaryAPin() { return _rotaryAPin; }

void Knobby::setRotaryAPin(int pin) {
  if (pin >= 0 && pin <= 39) {
    _rotaryAPin = pin;
    _preferences.putInt("rotaryAPin", _rotaryAPin);
  }
}

int Knobby::rotaryBPin() { return _rotaryBPin; }

void Knobby::setRotaryBPin(int pin) {
  if (pin >= 0 && pin <= 39) {
    _rotaryBPin = pin;
    _preferences.putInt("rotaryBPin", _rotaryBPin);
  }
}

int Knobby::pulseCount() { return _pulseCount; }

void Knobby::setPulseCount(int count) {
  if (count > 0 && count <= 8) {
    _pulseCount = count;
    _preferences.putInt("pulseCount", _pulseCount);
  }
}

void Knobby::printHeader() {
  const esp_app_desc_t *desc = esp_ota_get_app_description();
  log_printf("\n    _                 _     _              |\n");
  log_printf("   | |               | |   | |             |\n");
  log_printf("   | |  _ ____   ___ | |__ | |__  _   _    |   mac %s\n", WiFi.macAddress().c_str());
  log_printf("   | |_/ )  _ \\ / _ \\|  _ \\|  _ \\| | | |   |   built %s %s\n", desc->date, desc->time);
  log_printf("   |  _ (| | | | |_| | |_) ) |_) ) |_| |   |   git version %s\n", GIT_VERSION);
  log_printf("   |_| \\_)_| |_|\\___/|____/|____/ \\__  |   |   esp-idf %s\n", desc->idf_ver);
  log_printf("    by milo winningham           (____/    |   arduino %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  log_printf("    https://knobby.net                     |\n");
  log_printf("___________________________________________|____________________________________\n");
  log_printf("\n");
  if (WiFi.SSID().isEmpty()) {
    log_printf("    setup this device via usb or wifi:\n");
    log_printf("      * (re)connect and visit https://setup.knobby.net to configure\n");
    log_printf("      * or join the wifi network %s with password %s\n", name(), password());
  } else {
    log_printf("    connecting to wifi network: %s\n", WiFi.SSID());
    log_printf("    for configuration and more: http://knobby.local?pass=%s\n", password());
  }
  log_printf("\n");
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
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  for (auto i = 0; i < 10; i++) {
    voltage = esp_adc_cal_raw_to_voltage(analogRead(ADC_PIN), &adc_chars) * 2.0 / 1000.0;
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
      if (ADC_EN >= 0) digitalWrite(ADC_EN, HIGH);
      _batteryReadingRate = BatteryReadingFast;
      break;
    case BatteryReadingSlow:
      if (ADC_EN >= 0) digitalWrite(ADC_EN, HIGH);
      newVoltage = _readSettledBatteryVoltage();
      if (ADC_EN >= 0) digitalWrite(ADC_EN, LOW);
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
      if (ADC_EN >= 0) digitalWrite(ADC_EN, HIGH);
      _batteryReadings.clear();
      _batteryReadingRate = BatteryReadingFast;
    } else if (_batteryReadings.size() == _batteryReadingsMax) {
      if (ADC_EN >= 0) digitalWrite(ADC_EN, LOW);
      _batteryReadingRate = BatteryReadingSlow;
    }

    setBatteryVoltage(newVoltage);
    _batteryUpdatedMillis = millis();
  }
}

#endif // KNOBBY_H
