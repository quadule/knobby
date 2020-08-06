#include "sdkconfig.h"
#include <Arduino.h>

#include "ArduinoJson.h"
#include "ESPAsyncWebServer.h"
#include "ESPRotary.h"
#include "ESPmDNS.h"
#include <OneButton.h>
#include "SPIFFS.h"
#include "StreamString.h"
#include "TFT_eSPI.h"
#include "WiFiClientSecure.h"
#include "base64.h"
#include "driver/rtc_io.h"
#include "esp_adc_cal.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "time.h"

#include "genres.h"
#include "settings.h"
#include "spotify.h"

void backgroundApiLoop(void *params);
void reportBatteryVoltage();

void setActiveDevice(SpotifyDevice_t *device);
void setActiveUser(SpotifyUser_t *user);

bool readDataJson();
bool writeDataJson();
