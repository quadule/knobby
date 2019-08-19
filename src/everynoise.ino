#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <OneButton.h>
#include <SPIFFS.h>
#include <SSD1306Wire.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <time.h>

#include "ESPRotary.h"

SSD1306Wire display(0x3c, 5, 4);

#define ROTARY_ENCODER_A_PIN 25
#define ROTARY_ENCODER_B_PIN 26
#define ROTARY_ENCODER_BUTTON_PIN 13
ESPRotary knob(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, 4);
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

#include "fonts.h"
#include "genres.h"
#include "settings.h"

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define startsWith(STR, SEARCH) (strncmp(STR, SEARCH, strlen(SEARCH)) == 0)
const uint16_t SPTF_MAX_POLLING_DELAY = 30000;
const uint16_t SPTF_MIN_POLLING_INTERVAL = 10000;

typedef struct {
  int httpCode;
  String payload;
} HTTP_response_t;

typedef struct {
  String id;
  String name;
} SptfDevice_t;

enum SptfActions {
  Idle,
  GetToken,
  CurrentlyPlaying,
  Next,
  Previous,
  Toggle,
  PlayGenre,
  GetDevices
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

String nodeName = "everynoisebox";
String auth_code;
RTC_DATA_ATTR char access_token[300] = {'\0'};
String refresh_token;

RTC_DATA_ATTR time_t token_lifetime = 0;
RTC_DATA_ATTR time_t token_time = 0;
uint32_t last_curplay_millis = 0;
uint32_t next_curplay_millis = 0;

bool getting_token = false;
bool sptf_is_playing = false;
bool send_events = false;
uint32_t progress_ms = 0;

SptfActions sptfAction = Idle;

#define INACTIVITY_MILLIS 45000
short playingGenreIndex = -1;
uint16_t genreIndex = 0;
unsigned long lastInputMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;
bool displayOn = true;

enum MenuModes {
  RootMenu,
  AlphabeticList,
  AlphabeticSuffixList,
  PopularityList,
  ModernityList,
  BackgroundList,
  TempoList,
  UserList,
  DeviceList
};
MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
uint16_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"play/pause", "alphabetic", "alphabetic suffix",
                               "popularity", "modernity",  "background",
                               "tempo",      "users",      "devices"};
const char *users[] = {"milo", "alexa"};
// SptfDevice_t d1 = {String("980a3f0b52a30b16ea3e276c1df41097cedbb75d"),
//                    String("doop")};
// SptfDevice_t d2 = {String("a5c5a50cac7fb4e2a2978e9678eb0d780131923e"),
//                    String("living room")};
// SptfDevice_t d3 = {String("a087863af82ed5ef8ecb31bb4e1de43683ec962f"),
//                    String("basement")};
#define MAX_DEVICES 10
SptfDevice_t devices[MAX_DEVICES] = {{}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
uint8_t devicesCount = 0;
SptfDevice_t *activeDevice = NULL;
uint16_t menuSize = GENRE_COUNT;
uint16_t menuIndex = 0;
int menuOffset = 0;
int lastKnobPosition = 0;
double lastKnobSpeed = 0.0;

void setup() {
  // turn off onboard led
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);

  SPIFFS.begin(true);
  Serial.begin(115200);

  // enable wakeup on button click
  esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN, LOW);

  knob.setChangedHandler(knobRotated);
  button.setDebounceTicks(30);
  button.setClickTicks(333);
  button.setPressTicks(333);
  button.attachClick(knobClicked);
  button.attachDoubleClick(knobDoubleClicked);
  button.attachLongPressStart(knobLongPressStarted);
  button.attachLongPressStop(knobLongPressStopped);

  display.init();

  lastInputMillis = lastDisplayMillis = millis();
  menuIndex = genreIndex = menuOffset = random(GENRE_COUNT);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  WiFi.setHostname(nodeName.c_str());

  if (MDNS.begin(nodeName.c_str())) {
    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  //-----------------------------------------------
  // Initialize HTTP server handlers
  //-----------------------------------------------
  events.onConnect([](AsyncEventSourceClient *client) {
    Serial.printf("\n> [%d] events.onConnect\n", (int)millis());
  });
  server.addHandler(&events);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_events = true;
    uint32_t ts = millis();
    Serial.printf("\n> [%d] server.on /\n", ts);
    if (access_token[0] == '\0' && !getting_token) {
      getting_token = true;
      char auth_url[300] = "";
      snprintf(auth_url, sizeof(auth_url),
               ("https://accounts.spotify.com/authorize/"
                "?response_type=code"
                "&scope=user-read-private+user-read-currently-playing+user-"
                "read-playback-state+user-modify-playback-state"
                "&redirect_uri=http%%3A%%2F%%2F" +
                nodeName +
                ".local%%2Fcallback%%2F"
                "&client_id=%s")
                   .c_str(),
               SPTF_CLIENT_ID);
      Serial.printf("  [%d] Redirect to: %s\n", ts, auth_url);
      request->redirect(auth_url);
    } else {
      request->send(SPIFFS, "/index.html");
    }
  });

  server.on("/callback", HTTP_GET, [](AsyncWebServerRequest *request) {
    auth_code = "";
    uint8_t paramsNr = request->params();
    for (uint8_t i = 0; i < paramsNr; i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "code") {
        auth_code = p->value();
        sptfAction = GetToken;
        break;
      }
    }
    if (sptfAction == GetToken) {
      request->redirect("/");
    } else {
      request->send(204);
    }
  });

  server.on("/next", HTTP_GET, [](AsyncWebServerRequest *request) {
    sptfAction = Next;
    request->send(204);
  });

  server.on("/previous", HTTP_GET, [](AsyncWebServerRequest *request) {
    sptfAction = Previous;
    request->send(204);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    sptfAction = Toggle;
    request->send(204);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/resettoken", HTTP_GET, [](AsyncWebServerRequest *request) {
    access_token[0] = '\0';
    refresh_token = "";
    deleteRefreshToken();
    sptfAction = Idle;
    request->send(200, "text/plain", "Tokens deleted, restarting");
    uint32_t start = millis();
    while (true) {
      if (millis() - start > 5000) {
        ESP.restart();
      }
      yield();
    }
  });

  server.on("/toggleevents", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_events = !send_events;
    request->send(200, "text/plain", send_events ? "1" : "0");
  });

  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404); });

  server.begin();

  //-----------------------------------------------
  // Get refresh token from EEPROM
  //-----------------------------------------------
  refresh_token = readRefreshToken();
}

void setMenuMode(MenuModes newMode, uint16_t newMenuIndex) {
  menuMode = newMode;
  knob.resetPosition();
  switch (menuMode) {
    case RootMenu:
      menuSize = sizeof(rootMenuItems) / sizeof(rootMenuItems[0]);
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      menuSize = GENRE_COUNT;
      break;
    case UserList:
      menuSize = sizeof(users) / sizeof(users[0]);
      break;
    case DeviceList:
      menuSize = devicesCount == 0 ? 1 : devicesCount;
      break;
    default:
      break;
  }
  menuOffset = menuIndex = abs(newMenuIndex % menuSize);
}

void knobRotated(ESPRotary &r) {
  unsigned long now = millis();
  unsigned long lastInputDelta =
      (now == lastInputMillis) ? 1 : now - lastInputMillis;
  lastInputMillis = now;

  int newPosition = r.getPosition();
  int positionDelta = newPosition - lastKnobPosition;
  lastKnobPosition = newPosition;

  int steps = 1;
  if (menuSize > 20 && lastInputDelta <= 35) {
    double speed =
        (lastKnobSpeed + (double)positionDelta / lastInputDelta) / 2.0;
    lastKnobSpeed = speed;
    steps = min(100, max(1, (int)(fabs(speed) * 200)));
    // Serial.printf("steps: %d, speed: %f", steps, speed);
  } else {
    lastKnobSpeed = 0.0;
  }

  menuIndex += positionDelta * steps;
  if (menuIndex < 0) menuIndex = menuSize + abs(menuIndex);
  menuIndex %= menuSize;

  switch (menuMode) {
    case AlphabeticList:
      genreIndex = menuIndex;
      break;
    case AlphabeticSuffixList:
      genreIndex = genreIndexes_suffix[menuIndex];
      break;
    case PopularityList:
      genreIndex = genreIndexes_popularity[menuIndex];
      break;
    case ModernityList:
      genreIndex = genreIndexes_modernity[menuIndex];
      break;
    case BackgroundList:
      genreIndex = genreIndexes_background[menuIndex];
      break;
    case TempoList:
      genreIndex = genreIndexes_tempo[menuIndex];
      break;
    default:
      break;
  }

  sptfAction = Idle;
}

void knobClicked() {
  lastInputMillis = millis();

  switch (menuMode) {
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      Serial.printf("play genre %s\n", genres[genreIndex]);
      sptfAction = PlayGenre;
      break;
    case UserList:
      Serial.printf("switching to user %s\n", users[menuIndex]);
      break;
    case DeviceList:
      if (devicesCount > 0) {
        activeDevice = &devices[menuIndex];
        Serial.printf("switching to device %s\n", activeDevice->name.c_str());
      }
      break;
    case RootMenu:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  Serial.println("next");
  sptfAction = Next;
}

void knobLongPressStarted() {
  lastInputMillis = millis();
  if (menuMode != RootMenu) {
    lastMenuMode = menuMode;
    lastMenuIndex = menuIndex;
  }
  if (sptf_is_playing) {
    setMenuMode(RootMenu, 0);
  } else {
    setMenuMode(RootMenu, menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  if (menuIndex == 0) {
    Serial.println("toggle");
    sptfAction = Toggle;
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex != lastMenuIndex) {
    uint16_t newMenuIndex = lastMenuIndex;

    switch (menuIndex) {
      case AlphabeticList:
        newMenuIndex = genreIndex;
        break;
      case AlphabeticSuffixList:
        newMenuIndex = getMenuIndexForGenre(genreIndexes_suffix, newMenuIndex);
        break;
      case PopularityList:
        newMenuIndex =
            getMenuIndexForGenre(genreIndexes_popularity, newMenuIndex);
        break;
      case ModernityList:
        newMenuIndex =
            getMenuIndexForGenre(genreIndexes_modernity, newMenuIndex);
        break;
      case BackgroundList:
        newMenuIndex =
            getMenuIndexForGenre(genreIndexes_background, newMenuIndex);
        break;
      case TempoList:
        newMenuIndex = getMenuIndexForGenre(genreIndexes_tempo, newMenuIndex);
        break;
      case DeviceList:
        if (devicesCount == 0) {
          sptfAction = GetDevices;
        } else {
          for (uint8_t i = 1; i < devicesCount; i++) {
            if (&devices[i] == activeDevice) {
              newMenuIndex = i;
              break;
            }
          }
        }
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)menuIndex, newMenuIndex);
  } else {
    setMenuMode(lastMenuMode, lastMenuIndex);
  }
}

uint16_t getMenuIndexForGenre(const uint16_t indexes[], uint16_t defaultIndex) {
  for (uint16_t i = 1; i < GENRE_COUNT; i++) {
    if (indexes[i] == genreIndex) return i;
  }
  return defaultIndex;
}

void updateDisplay() {
  unsigned long current_millis = millis();
  if (current_millis < 500) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);
    display.drawString(63, 16, "every noise");
    display.drawString(63, 32, "in a box");
    display.display();
    lastDisplayMillis = millis();
  } else if (lastInputMillis > lastDisplayMillis ||
             (current_millis - lastDisplayMillis) > 10) {
    const char *genre = genres[genreIndex];
    if (!displayOn) {
      display.displayOn();
      displayOn = true;
    }
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);

    if (menuMode == RootMenu) {
      display.drawRect(7, 10, 114, 32);
      if (menuIndex == 0) {
        display.drawStringMaxWidth(63, 18, 126,
                                   sptf_is_playing ? "pause" : "play");
      } else {
        display.drawStringMaxWidth(63, 18, 126, rootMenuItems[menuIndex]);
      }
    } else if (menuMode == UserList) {
      char number[7];
      sprintf(number, "%d / %d", menuIndex + 1, menuSize);
      display.drawStringMaxWidth(63, 1, 126, number);
      display.drawStringMaxWidth(63, 18, 126, users[menuIndex]);
    } else if (menuMode == DeviceList) {
      if (devicesCount == 0) {
        display.drawStringMaxWidth(63, 18, 126, "loading...");
      } else {
        char header[7];
        sprintf(header, "%d / %d", menuIndex + 1, menuSize);
        display.drawStringMaxWidth(63, 1, 126, header);

        if (activeDevice == &devices[menuIndex]) {
          display.drawStringMaxWidth(
              63, 18, 126,
              String("[") + activeDevice->name + String("]"));
        } else {
          display.drawStringMaxWidth(63, 18, 126, devices[menuIndex].name);
        }
      }
    } else {
      display.drawStringMaxWidth(63, 18, 126, genre);

      if (sptf_is_playing &&
          (sptfAction == Idle || sptfAction == CurrentlyPlaying) &&
          playingGenreIndex == genreIndex) {
        uint32_t estimated_millis =
            progress_ms + (current_millis - last_curplay_millis);
        uint8_t seconds = estimated_millis / 1000 % 60;
        uint8_t minutes = estimated_millis / (1000 * 60) % 60;
        uint8_t hours = estimated_millis / (1000 * 60 * 60);
        char elapsed[10];
        if (hours == 0) {
          sprintf(elapsed, "%d:%02d", minutes, seconds);
        } else {
          sprintf(elapsed, "%d:%02d:%02d", hours, minutes, seconds);
        }
        display.drawString(63, 1, elapsed);
      } else if (sptfAction == PlayGenre ||
                 (sptfAction == Toggle && !sptf_is_playing)) {
        display.drawString(63, 1, "play");
      } else if (sptfAction == Toggle && sptf_is_playing) {
        display.drawString(63, 1, "pause");
      } else if (sptfAction == Previous) {
        display.drawString(63, 1, "previous");
      } else if (sptfAction == Next) {
        display.drawString(63, 1, "next");
      } else {
        char label[16];
        if (menuMode == TempoList) {
          sprintf(label, "%s avg bpm", genreLabels_tempo[menuIndex]);
        } else {
          sprintf(label, "%d / %d", menuIndex + 1, menuSize);
        }
        display.drawString(63, 1, label);
      }
    }

    display.display();
    lastDisplayMillis = millis();
  }
}

void loop() {
  knob.loop();
  button.tick();

  uint32_t cur_millis = millis();
  struct timeval now;
  gettimeofday(&now, NULL);
  time_t cur_secs = now.tv_sec;
  int token_age = cur_secs - token_time;
  bool connected = WiFi.isConnected();

  if (cur_millis - lastInputMillis > INACTIVITY_MILLIS) {
    Serial.println("entering deep sleep");
    display.clear();
    display.displayOff();
    displayOn = false;
    WiFi.disconnect(true);
    esp_deep_sleep_start();
  }

  if (!connected && cur_millis - lastReconnectAttemptMillis > 3000) {
    Serial.println("not connected, retrying");
    WiFi.begin();
    lastReconnectAttemptMillis = cur_millis;
  }

  if (connected && !getting_token && refresh_token != "" &&
      (access_token[0] == '\0' || token_age >= token_lifetime)) {
    getting_token = true;
    sptfAction = GetToken;
  } else {
    updateDisplay();
  }

  // Spotify action handler
  if (WiFi.status() == WL_CONNECTED) {
    switch (sptfAction) {
      case Idle:
        break;
      case GetToken:
        if (auth_code != "") {
          sptfGetToken(auth_code, gt_authorization_code);
        } else if (refresh_token != "") {
          sptfGetToken(refresh_token, gt_refresh_token);
        } else {
          Serial.println("No token found, visit http://" + nodeName +
                         ".local to authenticate");
        }
        break;
      case CurrentlyPlaying:
        if (next_curplay_millis && (cur_millis >= next_curplay_millis) &&
            (cur_millis - last_curplay_millis >= SPTF_MIN_POLLING_INTERVAL)) {
          sptfCurrentlyPlaying();
        } else if (cur_millis - last_curplay_millis >= SPTF_MAX_POLLING_DELAY) {
          sptfCurrentlyPlaying();
        }
        break;
      case Next:
        sptfNext();
        break;
      case Previous:
        sptfPrevious();
        break;
      case Toggle:
        sptfToggle();
        break;
      case PlayGenre:
        sptfPlayGenre(playlists[genreIndex]);
        break;
      case GetDevices:
        sptfGetDevices();
        break;
    }
  }
}

/**
 * Send log to browser
 *
 * @param logData
 * @param event_type
 */
void eventsSendLog(const char *logData, EventsLogTypes type = log_line) {
  if (!send_events) return;
  events.send(logData, type == log_line ? "line" : "raw");
}

/**
 * Send infos to browser
 *
 * @param msg
 * @param payload
 */
void eventsSendInfo(const char *msg, const char *payload = "") {
  if (!send_events) return;

  DynamicJsonDocument json(512);
  json["msg"] = msg;
  if (strlen(payload)) {
    json["payload"] = payload;
  }

  String info;
  serializeJson(json, info);
  events.send(info.c_str(), "info");
}

/**
 * Send errors to browser
 *
 * @param errCode
 * @param errMsg
 * @param payload
 */
void eventsSendError(int code, const char *msg, const char *payload = "") {
  if (!send_events) return;

  DynamicJsonDocument json(512);
  json["code"] = code;
  json["msg"] = msg;
  if (strlen(payload)) {
    json["payload"] = payload;
  }

  String error;
  serializeJson(json, error);
  events.send(error.c_str(), "error");
}

/**
 * Base 64 encode
 *
 * @param str
 * @return
 */
String b64Encode(String str) {
  String encodedStr = base64::encode(str);

  // Remove unnecessary linefeeds
  int idx = -1;
  while ((idx = encodedStr.indexOf('\n')) != -1) {
    encodedStr.remove(idx, 1);
  }

  return encodedStr;
}

void writeRefreshToken(String refresh_token) {
  Serial.printf("Saving refresh token: %s\n", refresh_token.c_str());
  File f = SPIFFS.open("/refreshToken.txt", "w+");
  if (!f) {
    Serial.println("Failed to open config file");
    return;
  }
  f.println(refresh_token);
  f.close();
}

String readRefreshToken() {
  File f = SPIFFS.open("/refreshToken.txt", "r");
  if (!f) {
    Serial.println("Failed to open config file");
    return "";
  }
  while (f.available()) {
    String token = f.readStringUntil('\r');
    Serial.printf("Refresh Token: %s\n", token.c_str());
    f.close();
    return token;
  }
  return "";
}

/**
 * Delete refresh token from EEPROM
 */
void deleteRefreshToken() { SPIFFS.remove("/refreshToken.txt"); }

/**
 * HTTP request
 *
 * @param host
 * @param port
 * @param headers
 * @param content
 * @return
 */
HTTP_response_t httpRequest(const char *host, uint16_t port,
                            const char *headers, const char *content = "") {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] httpRequest(%s, %d, ...)\n", ts, host, port);

  WiFiClientSecure client;

  if (!client.connect(host, port)) {
    return {503, "Service unavailable (unable to connect)"};
  }

  /*
   * Send HTTP request
   */

  // Serial.printf("  [%d] Request:\n%s%s\n", ts, headers, content);
  eventsSendLog(">>>> REQUEST");
  eventsSendLog(headers);
  eventsSendLog(content);

  client.print(headers);
  if (strlen(content)) {
    client.print(content);
  }

  /*
   * Get HTTP response
   */

  uint32_t timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      client.stop();
      return {503, "Service unavailable (timeout)"};
    }
    yield();
  }

  // Serial.printf("  [%d] Response:\n", ts);
  eventsSendLog("<<<< RESPONSE");

  HTTP_response_t response = {0, ""};
  boolean EOH = false;
  uint32_t contentLength = 0;
  uint16_t buffSize = 1024;
  uint32_t readSize = 0;
  uint32_t totalReadSize = 0;
  uint32_t lastAvailableMillis = millis();
  char buff[buffSize];

  // !HERE
  // client.setNoDelay(false);

  while (client.connected()) {
    int availableSize = client.available();
    if (availableSize) {
      lastAvailableMillis = millis();

      if (!EOH) {
        // Read response headers
        readSize = client.readBytesUntil('\n', buff, buffSize);
        buff[readSize - 1] = '\0';  // replace /r by \0
        // Serial.printf("%s\n", buff);
        eventsSendLog(buff);
        if (startsWith(buff, "HTTP/1.")) {
          buff[12] = '\0';
          response.httpCode = atoi(&buff[9]);
          if (response.httpCode == 204) {
            break;
          }
        } else if (startsWith(buff, "Content-Length:")) {
          contentLength = atoi(&buff[16]);
          if (contentLength == 0) {
            break;
          }
          response.payload.reserve(contentLength + 1);
        } else if (buff[0] == '\0') {
          // End of headers
          EOH = true;
          eventsSendLog("");
        }
      } else {
        // Read response content
        readSize = client.readBytes(buff, min(buffSize - 1, availableSize));
        buff[readSize] = '\0';
        eventsSendLog(buff, log_raw);
        response.payload += buff;
        totalReadSize += readSize;
        if (contentLength != 0 && totalReadSize >= contentLength) {
          if (totalReadSize > contentLength)
            Serial.printf("read %d of length %d", totalReadSize, contentLength);
          break;
        }
      }
    } else {
      if ((millis() - lastAvailableMillis) > 5000) {
        response = {504, "Response timeout"};
        break;
      } else if (totalReadSize > 0 && (millis() - lastAvailableMillis) > 100) {
        break;
      } else {
        delay(50);
      }
    }
  }
  client.stop();

  return response;
}

/**
 * Call Spotify API
 *
 * @param method
 * @param endpoint
 * @return
 */
HTTP_response_t sptfApiRequest(const char *method, const char *endpoint,
                               const char *content = "") {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfApiRequest(%s, %s, %s)\n", ts, method, endpoint,
                content);

  char headers[512];
  snprintf(headers, sizeof(headers),
           "%s /v1/me/player%s HTTP/1.1\r\n"
           "Host: api.spotify.com\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n\r\n",
           method, endpoint, access_token, strlen(content));

  HTTP_response_t response =
      httpRequest("api.spotify.com", 443, headers, content);

  if (response.httpCode == 401) {
    Serial.println("401 Unauthorized, clearing access_token");
    access_token[0] = '\0';
  }

  return response;
}

/**
 * Get Spotify token
 *
 * @param code          Either an authorization code or a refresh token
 * @param grant_type    [gt_authorization_code|gt_refresh_token]
 */
void sptfGetToken(const String &code, GrantTypes grant_type) {
  uint32_t ts = millis();
  Serial.printf(
      "\n> [%d] sptfGetToken(%s, %s)\n", ts, code.c_str(),
      grant_type == gt_authorization_code ? "authorization" : "refresh");

  bool success = false;

  char requestContent[512];
  if (grant_type == gt_authorization_code) {
    snprintf(requestContent, sizeof(requestContent),
             ("grant_type=authorization_code"
              "&redirect_uri=http%%3A%%2F%%2F" +
              nodeName +
              ".local%%2Fcallback%%2F"
              "&code=%s")
                 .c_str(),
             code.c_str());
  } else {
    snprintf(requestContent, sizeof(requestContent),
             "grant_type=refresh_token&refresh_token=%s", code.c_str());
  }

  uint8_t basicAuthSize = sizeof(SPTF_CLIENT_ID) + sizeof(SPTF_CLIENT_SECRET);
  char basicAuth[basicAuthSize];
  snprintf(basicAuth, basicAuthSize, "%s:%s", SPTF_CLIENT_ID,
           SPTF_CLIENT_SECRET);

  char requestHeaders[768];
  snprintf(requestHeaders, sizeof(requestHeaders),
           "POST /api/token HTTP/1.1\r\n"
           "Host: accounts.spotify.com\r\n"
           "Authorization: Basic %s\r\n"
           "Content-Length: %d\r\n"
           "Content-Type: application/x-www-form-urlencoded\r\n"
           "Connection: close\r\n\r\n",
           b64Encode(basicAuth).c_str(), strlen(requestContent));

  HTTP_response_t response =
      httpRequest("accounts.spotify.com", 443, requestHeaders, requestContent);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(768);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      strcpy(access_token, json["access_token"]);
      if (access_token[0] != '\0') {
        token_lifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval now;
        gettimeofday(&now, NULL);
        token_time = now.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          String new_refresh_token = json["refresh_token"].as<String>();
          if (refresh_token != new_refresh_token) {
            refresh_token = new_refresh_token;
            writeRefreshToken(refresh_token);
          }
        }
      }
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts,
                    response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload",
                      response.payload.c_str());
    }
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode,
                  response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }

  if (success) sptfAction = CurrentlyPlaying;

  getting_token = false;
}

/**
 * Get information about the Spotify user's current playback
 */
void sptfCurrentlyPlaying() {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfCurrentlyPlaying()\n", ts);

  next_curplay_millis = 0;
  if (access_token[0] == '\0') return;

  HTTP_response_t response = sptfApiRequest("GET", "?market=US");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(5000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      last_curplay_millis = millis();
      sptf_is_playing = json["is_playing"];
      progress_ms = json["progress_ms"];
      uint32_t duration_ms = json["item"]["duration_ms"];

      // Check if current song is about to end
      if (sptf_is_playing) {
        uint32_t remaining_ms = duration_ms - progress_ms;
        if (remaining_ms < SPTF_MAX_POLLING_DELAY) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          next_curplay_millis = millis() + remaining_ms + 200;
        }
      }
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts,
                    response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload",
                      response.payload.c_str());
    }
  } else if (response.httpCode == 204) {
    sptf_is_playing = false;
    sptfResetProgress();
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode,
                  response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }
}

/**
 * Spotify next track
 */
void sptfNext() {
  HTTP_response_t response = sptfApiRequest("POST", "/next");
  if (response.httpCode == 204) {
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

/**
 * Spotify previous track
 */
void sptfPrevious() {
  HTTP_response_t response = sptfApiRequest("POST", "/previous");
  if (response.httpCode == 204) {
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

/**
 * Spotify toggle pause/play
 */
void sptfToggle() {
  if (access_token[0] == '\0') return;

  bool was_playing = sptf_is_playing;
  sptf_is_playing = !sptf_is_playing;
  HTTP_response_t response =
      sptfApiRequest("PUT", was_playing ? "/pause" : "/play");
  if (response.httpCode == 204) {
    next_curplay_millis = millis() + 200;
  } else {
    sptf_is_playing = !sptf_is_playing;
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

void sptfPlayGenre(const char *playlistId) {
  if (access_token[0] == '\0') return;

  sptf_is_playing = false;
  playingGenreIndex = genreIndex;
  char requestContent[58];
  snprintf(requestContent, sizeof(requestContent),
           "{\"context_uri\":\"spotify:playlist:%s\"}", playlistId);
  HTTP_response_t response;
  if (activeDevice != NULL) {
    const char *deviceId = activeDevice->id.c_str();
    char path[58];
    snprintf(path, sizeof(path), "/play?device_id=%s", deviceId);
    response = sptfApiRequest("PUT", path, requestContent);
  } else {
    response = sptfApiRequest("PUT", "/play", requestContent);
  }

  if (response.httpCode == 204) {
    sptf_is_playing = true;
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error",
                    response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

void sptfResetProgress() {
  progress_ms = 0;
  last_curplay_millis = millis();
  next_curplay_millis = millis() + 200;
};

void sptfGetDevices() {
  if (access_token[0] == '\0') return;
  HTTP_response_t response = sptfApiRequest("GET", "/devices");
  if (response.httpCode == 200) {
    DynamicJsonDocument doc(5000);
    DeserializationError error = deserializeJson(doc, response.payload);

    if (!error) {
      JsonArray jsonDevices = doc["devices"];
      devicesCount = min(MAX_DEVICES, jsonDevices.size());
      if (menuMode == DeviceList) menuSize = devicesCount;
      for (size_t i = 0; i < devicesCount; i++) {
        JsonObject jsonDevice = doc["devices"][i];
        const char *id = jsonDevice["id"];
        const char *name = jsonDevice["name"];
        bool is_active = jsonDevice["is_active"];
        // int volume_percent = jsonDevice["volume_percent"];

        devices[i].id = String(id);
        devices[i].name = String(name);
        if (is_active) {
          activeDevice = &devices[i];
          if (menuMode == DeviceList) menuIndex = i;
        }
      }
    }
  }
  sptfAction = CurrentlyPlaying;
}
