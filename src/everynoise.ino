#include "ArduinoJson.h"
#include "ESPAsyncWebServer.h"
#include "ESPRotary.h"
#include "ESPmDNS.h"
#include "OneButton.h"
#include "SPIFFS.h"
#include "SSD1306Wire.h"
#include "WiFiClientSecure.h"
#include "base64.h"
#include "driver/rtc_io.h"
#include "time.h"

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
  char name[64] = {'\0'};
  char refreshToken[150] = {'\0'};
  char country[3] = {'\0'};
  bool selected = false;
  char selectedDeviceId[41] = {'\0'};
} SptfUser_t;

typedef struct {
  char id[41] = {'\0'};
  char name[64] = {'\0'};
  uint8_t volumePercent;
} SptfDevice_t;

enum SptfActions { Idle, GetToken, CurrentlyPlaying, CurrentProfile, Next, Previous, Toggle, PlayGenre, GetDevices };

enum GrantTypes { gt_authorization_code, gt_refresh_token };

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

String nodeName = "everynoisebox";
String auth_code;
RTC_DATA_ATTR char access_token[300] = {'\0'};

RTC_DATA_ATTR time_t token_lifetime = 0;
RTC_DATA_ATTR time_t token_time = 0;
uint32_t last_curplay_millis = 0;
uint32_t next_curplay_millis = 0;

bool getting_token = false;
bool sptf_is_playing = false;
bool send_events = false;
uint32_t progress_ms = 0;

SptfActions sptfAction = Idle;

short playingGenreIndex = -1;
uint32_t genreIndex = 0;
unsigned long inactivityMillis = 45000;
unsigned long lastInputMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;

enum MenuModes {
  RootMenu,
  UserList,
  DeviceList,
  AlphabeticList,
  AlphabeticSuffixList,
  PopularityList,
  ModernityList,
  BackgroundList,
  TempoList
};
MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
uint32_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"play/pause", "users",     "devices",    "name prefix", "name suffix",
                               "popularity", "modernity", "background", "tempo"};
#define MAX_USERS 10
SptfUser_t users[MAX_USERS] = {};
uint8_t usersCount = 0;
SptfUser_t *activeUser = NULL;
RTC_DATA_ATTR char activeRefreshToken[150] = {'\0'};
#define MAX_DEVICES 10
SptfDevice_t devices[MAX_DEVICES] = {};
uint8_t devicesCount = 0;
SptfDevice_t *activeDevice = NULL;
RTC_DATA_ATTR char activeDeviceId[41] = {'\0'};
uint32_t menuSize = GENRE_COUNT;
uint32_t menuIndex = 0;
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
  display.displayOn();

  lastInputMillis = lastDisplayMillis = millis();
  menuIndex = genreIndex = menuOffset = random(GENRE_COUNT);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  WiFi.setHostname(nodeName.c_str());

  if (MDNS.begin(nodeName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS responder started, visit http://%s.local\n", nodeName.c_str());
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Initialize HTTP server handlers
  events.onConnect([](AsyncEventSourceClient *client) { Serial.printf("\n> [%d] events.onConnect\n", (int)millis()); });
  server.addHandler(&events);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_events = true;
    inactivityMillis = 90000;
    uint32_t ts = millis();
    Serial.printf("\n> [%d] server.on /\n", ts);
    if (access_token[0] == '\0' && !getting_token) {
      request->redirect("http://" + nodeName + ".local/authorize");
    } else {
      request->send(SPIFFS, "/index.html");
    }
  });

  server.on("/authorize", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    Serial.printf("\n> [%d] server.on /\n", ts);
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

  server.on("/resetusers", HTTP_GET, [](AsyncWebServerRequest *request) {
    access_token[0] = '\0';
    activeRefreshToken[0] = '\0';
    SPIFFS.remove("/users.json");
    sptfAction = Idle;
    request->send(200, "text/plain", "Tokens deleted, restarting");
    uint32_t start = millis();
    while (true) {
      if (millis() - start > 500) ESP.restart();
      yield();
    }
  });

  server.on("/toggleevents", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_events = !send_events;
    request->send(200, "text/plain", send_events ? "1" : "0");
  });

  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404); });

  server.begin();

  readUsersJson();
}

void setMenuMode(MenuModes newMode, uint32_t newMenuIndex) {
  menuMode = newMode;
  knob.resetPosition();
  switch (menuMode) {
    case RootMenu:
      menuSize = sizeof(rootMenuItems) / sizeof(rootMenuItems[0]);
      break;
    case UserList:
      menuSize = usersCount;
      break;
    case DeviceList:
      menuSize = devicesCount == 0 ? 1 : devicesCount;
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      menuSize = GENRE_COUNT;
      break;
    default:
      break;
  }
  menuOffset = menuIndex = newMenuIndex % menuSize;
}

void knobRotated(ESPRotary &r) {
  unsigned long now = millis();
  unsigned long lastInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  lastInputMillis = now;

  int newPosition = r.getPosition();
  int positionDelta = newPosition - lastKnobPosition;
  lastKnobPosition = newPosition;

  int steps = 1;
  if (menuSize > 20 && lastInputDelta > 1 && lastInputDelta <= 30) {
    double speed = (3 * lastKnobSpeed + (double)positionDelta / lastInputDelta) / 4.0;
    lastKnobSpeed = speed;
    steps = min(50, max(1, (int)(fabs(speed) * 200)));
  } else {
    lastKnobSpeed = 0.0;
  }

  int newMenuIndex = ((int)menuIndex + (positionDelta * steps)) % (int)menuSize;
  if (newMenuIndex < 0) newMenuIndex += menuSize;
  // Serial.printf("newIndex=%d from old=%d pos=%d delta=%d steps=%d size=%d\n", newMenuIndex, menuIndex, newPosition, positionDelta, steps, menuSize);
  menuIndex = newMenuIndex;

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
    case UserList:
      if (usersCount > 0) {
        setActiveUser(&users[menuIndex]);
        token_lifetime = 0;
        token_time = 0;
        access_token[0] = '\0';
        updateDisplay();
        writeUsersJson();
      }
      break;
    case DeviceList:
      if (devicesCount > 0) {
        setActiveDevice(&devices[menuIndex]);
        writeUsersJson();
      }
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      sptfAction = PlayGenre;
      break;
    case RootMenu:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (access_token[0] == '\0') return;
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
    if (access_token[0] != '\0') sptfAction = Toggle;
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex != lastMenuIndex) {
    uint32_t newMenuIndex = lastMenuIndex;

    switch (menuIndex) {
      case UserList:
        if (usersCount == 0) {
          Serial.printf("No users found! Visit http://%s.local to sign in.\n", nodeName.c_str());
          setMenuMode(lastMenuMode, lastMenuIndex);
        } else {
          newMenuIndex = 0;
          for (uint8_t i = 1; i < usersCount; i++) {
            if (strcmp(users[i].refreshToken, activeRefreshToken) == 0) {
              newMenuIndex = i;
              break;
            }
          }
          if (users[newMenuIndex].name[0] == '\0') sptfAction = CurrentProfile;
        }
        break;
      case DeviceList:
        if (devicesCount == 0 || activeDevice == NULL) {
          sptfAction = GetDevices;
        } else {
          newMenuIndex = 0;
          for (uint8_t i = 0; i < devicesCount; i++) {
            if (strcmp(devices[i].id, activeDeviceId) == 0) {
              newMenuIndex = i;
              break;
            }
          }
        }
        break;
      case AlphabeticList:
        newMenuIndex = genreIndex;
        break;
      case AlphabeticSuffixList:
        newMenuIndex = getGenreMenuIndex(genreIndexes_suffix, newMenuIndex);
        break;
      case PopularityList:
        newMenuIndex = getGenreMenuIndex(genreIndexes_popularity, newMenuIndex);
        break;
      case ModernityList:
        newMenuIndex = getGenreMenuIndex(genreIndexes_modernity, newMenuIndex);
        break;
      case BackgroundList:
        newMenuIndex = getGenreMenuIndex(genreIndexes_background, newMenuIndex);
        break;
      case TempoList:
        newMenuIndex = getGenreMenuIndex(genreIndexes_tempo, newMenuIndex);
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)menuIndex, newMenuIndex);
  } else {
    setMenuMode(lastMenuMode, lastMenuIndex);
  }
}

uint32_t getGenreMenuIndex(const uint16_t indexes[], uint32_t defaultIndex) {
  for (uint32_t i = 1; i < GENRE_COUNT; i++) {
    if (indexes[i] == genreIndex) return i;
  }
  return defaultIndex;
}

void updateDisplay() {
  const int centerX = 63;
  const int maxWidth = 126;
  const int lineOne = 0;
  const int lineTwo = 16;
  const int lineThree = 32;
  unsigned long current_millis = millis();
  int8_t t = current_millis / 10 % 6;
  bool connected = WiFi.isConnected();
  if (current_millis < 500) {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);
    display.drawString(centerX, lineTwo, "every noise");
    display.drawString(centerX, lineThree, "at once");
    display.drawRect(t, t, 127 - (t * 2), 63 - (t * 2));
    display.display();
    lastDisplayMillis = millis();
    delay(random(50));
  } else if (lastInputMillis > lastDisplayMillis || (current_millis - lastDisplayMillis) > 10) {
    const char *genre = genres[genreIndex];
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);

    if (!connected) {
      if (t == 0) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, ".");
      if (t == 1) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "|");
      if (t == 2) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "|'|");
      if (t == 3) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "'|'");
      if (t == 4) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, ".:.");
      if (t == 5) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "_");
      delay(50);
    } else if (usersCount == 0) {
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "setup at http://");
      display.drawStringMaxWidth(centerX, lineThree, maxWidth, nodeName + ".local");
    } else if (getting_token) {
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "spotify...");
    } else if (menuMode == RootMenu) {
      display.drawRect(7, 9, 114, 32);
      if (menuIndex == 0) {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, sptf_is_playing ? "pause" : "play");
      } else {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, rootMenuItems[menuIndex]);
      }
    } else if (menuMode == UserList) {
      char header[7];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      display.drawStringMaxWidth(centerX, lineOne, maxWidth, header);
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, users[menuIndex].name);

      SptfUser_t *user = &users[menuIndex];
      if (user->name[0] == '\0') {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "loading...");
      } else if (strcmp(user->refreshToken, activeRefreshToken) == 0) {
        char selectedName[66];
        sprintf(selectedName, "[%s]", user->name);
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, selectedName);
      } else {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, users[menuIndex].name);
      }
    } else if (menuMode == DeviceList) {
      if (devicesCount == 0) {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "loading...");
      } else {
        char header[7];
        sprintf(header, "%d / %d", menuIndex + 1, menuSize);
        display.drawStringMaxWidth(centerX, lineOne, maxWidth, header);

        SptfDevice_t *device = &devices[menuIndex];
        if (strcmp(device->id, activeDeviceId) == 0) {
          char selectedName[66];
          sprintf(selectedName, "[%s]", device->name);
          display.drawStringMaxWidth(centerX, lineTwo, maxWidth, selectedName);
        } else {
          display.drawStringMaxWidth(centerX, lineTwo, maxWidth, device->name);
        }
      }
    } else {
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, genre);

      if (sptf_is_playing && (sptfAction == Idle || sptfAction == CurrentlyPlaying) &&
          playingGenreIndex == genreIndex) {
        uint32_t estimated_millis = progress_ms + (current_millis - last_curplay_millis);
        uint8_t seconds = estimated_millis / 1000 % 60;
        uint8_t minutes = estimated_millis / (1000 * 60) % 60;
        uint8_t hours = estimated_millis / (1000 * 60 * 60);
        char elapsed[10];
        if (hours == 0) {
          sprintf(elapsed, "%d:%02d", minutes, seconds);
        } else {
          sprintf(elapsed, "%d:%02d:%02d", hours, minutes, seconds);
        }
        display.drawString(centerX, lineOne, elapsed);
      } else if (sptfAction == PlayGenre || (sptfAction == Toggle && !sptf_is_playing)) {
        display.drawString(centerX, lineOne, "play");
      } else if (sptfAction == Toggle && sptf_is_playing) {
        display.drawString(centerX, lineOne, "pause");
      } else if (sptfAction == Previous) {
        display.drawString(centerX, lineOne, "previous");
      } else if (sptfAction == Next) {
        display.drawString(centerX, lineOne, "next");
      } else {
        char label[13];
        if (menuMode == TempoList) {
          sprintf(label, "~%s bpm", genreLabels_tempo[menuIndex]);
        } else {
          sprintf(label, "%d / %d", menuIndex + 1, menuSize);
        }
        display.drawString(centerX, lineOne, label);
      }
    }

    display.display();
    lastDisplayMillis = millis();
  }
}

void eventsSendLog(const char *logData, EventsLogTypes type = log_line) {
  if (!send_events) return;
  events.send(logData, type == log_line ? "line" : "raw");
}

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

void loop() {
  knob.loop();
  button.tick();

  uint32_t cur_millis = millis();
  struct timeval now;
  gettimeofday(&now, NULL);
  time_t cur_secs = now.tv_sec;
  int token_age = cur_secs - token_time;
  bool connected = WiFi.isConnected();

  if (cur_millis - lastInputMillis > inactivityMillis) {
    Serial.printf("\n> [%d] Entering deep sleep.\n", cur_millis);
    eventsSendLog("Entering deep sleep.");
    display.clear();
    display.displayOff();
    WiFi.disconnect(true);
    rtc_gpio_isolate(GPIO_NUM_12);
    esp_deep_sleep_start();
  }

  if (!connected && cur_millis - lastReconnectAttemptMillis > 3000) {
    Serial.printf("> [%d] Trying to connect to network %s...\n", cur_millis, ssid);
    WiFi.begin();
    lastReconnectAttemptMillis = cur_millis;
  }

  if (connected && !getting_token && activeRefreshToken[0] != '\0' &&
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
          sptfGetToken(auth_code.c_str(), gt_authorization_code);
        } else if (activeRefreshToken[0] != '\0') {
          sptfGetToken(activeRefreshToken, gt_refresh_token);
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
      case CurrentProfile:
        sptfCurrentProfile();
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

String b64Encode(String str) {
  String encodedStr = base64::encode(str);

  // Remove unnecessary linefeeds
  int idx = -1;
  while ((idx = encodedStr.indexOf('\n')) != -1) {
    encodedStr.remove(idx, 1);
  }

  return encodedStr;
}

void setActiveUser(SptfUser_t *user) {
  activeUser = user;
  strncpy(activeRefreshToken, activeUser->refreshToken, sizeof(activeRefreshToken));
  if (user->selectedDeviceId[0] != '\0') strncpy(activeDeviceId, user->selectedDeviceId, sizeof(activeDeviceId));

  SptfDevice_t *device = NULL;
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (strcmp(devices[i].id, activeDeviceId) == 0) {
      device = &devices[i];
      break;
    }
  }
  setActiveDevice(device);
}

void setActiveDevice(SptfDevice_t *device) {
  activeDevice = device;
  if (activeDevice == NULL) {
    devicesCount = 0;
  } else {
    strncpy(activeDeviceId, activeDevice->id, sizeof(activeDeviceId));
    strncpy(activeUser->selectedDeviceId, activeDevice->id, sizeof(activeUser->selectedDeviceId));
  }
}

bool readUsersJson() {
  File f = SPIFFS.open("/users.json", "r");
  DynamicJsonDocument doc(3000);
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (error) {
    Serial.printf("Failed to read users.json: %s\n", error.c_str());
    return false;
  } else {
    serializeJson(doc, Serial);
    Serial.println();
  }

  usersCount = min(MAX_USERS, doc.size());

  for (uint8_t i = 0; i < usersCount; i++) {
    JsonObject jsonUser = doc[i];
    const char *name = jsonUser["name"];
    const char *token = jsonUser["token"];
    const char *country = jsonUser["country"];
    const char *selectedDeviceId = jsonUser["selectedDeviceId"];

    SptfUser_t *user = &users[i];
    strncpy(user->name, name, sizeof(user->name));
    strncpy(user->refreshToken, token, sizeof(user->refreshToken));
    strncpy(user->country, country, sizeof(user->country));
    strncpy(user->selectedDeviceId, selectedDeviceId, sizeof(user->selectedDeviceId));

    if (jsonUser["selected"].as<bool>() == true) setActiveUser(user);
  }

  if (usersCount > 0 && activeUser == NULL) setActiveUser(&users[0]);

  return true;
}

bool writeUsersJson() {
  File f = SPIFFS.open("/users.json", "w+");
  DynamicJsonDocument doc(3000);

  for (uint8_t i = 0; i < usersCount; i++) {
    SptfUser_t *user = &users[i];
    JsonObject obj = doc.createNestedObject();
    obj["name"] = user->name;
    obj["token"] = user->refreshToken;
    obj["country"] = user->country;
    obj["selectedDeviceId"] = user->selectedDeviceId;
    obj["selected"] = (bool)(strcmp(user->refreshToken, activeRefreshToken) == 0);
  }

  size_t bytes = serializeJson(doc, f);
  f.close();
  if (bytes <= 0) {
    Serial.printf("Failed to write users.json: %d\n", bytes);
    return false;
  }
  serializeJson(doc, Serial);
  Serial.println();
  return true;
}

HTTP_response_t httpRequest(const char *host, uint16_t port, const char *headers, const char *content = "") {
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

  while (client.connected()) {
    int availableSize = client.available();
    if (availableSize) {
      lastAvailableMillis = millis();

      if (!EOH) {
        // Read response headers
        readSize = client.readBytesUntil('\n', buff, buffSize);
        buff[readSize - 1] = '\0';  // replace \r with \0
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
        if (contentLength != 0 && totalReadSize >= contentLength) break;
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

HTTP_response_t sptfApiRequest(const char *method, const char *endpoint, const char *content = "") {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfApiRequest(%s, %s, %s)\n", ts, method, endpoint, content);

  char headers[512];
  snprintf(headers, sizeof(headers),
           "%s /v1/me%s HTTP/1.1\r\n"
           "Host: api.spotify.com\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n\r\n",
           method, endpoint, access_token, strlen(content));

  HTTP_response_t response = httpRequest("api.spotify.com", 443, headers, content);

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
void sptfGetToken(const char *code, GrantTypes grant_type) {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfGetToken(%s, %s)\n", ts, code,
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
             code);
  } else {
    snprintf(requestContent, sizeof(requestContent), "grant_type=refresh_token&refresh_token=%s", code);
  }

  uint8_t basicAuthSize = sizeof(SPTF_CLIENT_ID) + sizeof(SPTF_CLIENT_SECRET);
  char basicAuth[basicAuthSize];
  snprintf(basicAuth, basicAuthSize, "%s:%s", SPTF_CLIENT_ID, SPTF_CLIENT_SECRET);

  char requestHeaders[768];
  snprintf(requestHeaders, sizeof(requestHeaders),
           "POST /api/token HTTP/1.1\r\n"
           "Host: accounts.spotify.com\r\n"
           "Authorization: Basic %s\r\n"
           "Content-Length: %d\r\n"
           "Content-Type: application/x-www-form-urlencoded\r\n"
           "Connection: close\r\n\r\n",
           b64Encode(basicAuth).c_str(), strlen(requestContent));

  HTTP_response_t response = httpRequest("accounts.spotify.com", 443, requestHeaders, requestContent);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(768);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      strncpy(access_token, json["access_token"], sizeof(access_token));
      if (access_token[0] != '\0') {
        token_lifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval now;
        gettimeofday(&now, NULL);
        token_time = now.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          const char *newRefreshToken = json["refresh_token"];
          if (strcmp(newRefreshToken, activeRefreshToken) != 0) {
            strncpy(activeRefreshToken, newRefreshToken, sizeof(activeRefreshToken));
            bool found = false;
            for (uint8_t i = 0; i < usersCount; i++) {
              SptfUser_t *user = &users[i];
              if (strcmp(user->refreshToken, activeRefreshToken) == 0) {
                found = true;
                break;
              }
            }
            if (!found && usersCount < MAX_USERS) {
              SptfUser_t *user = &users[usersCount++];
              strncpy(user->refreshToken, activeRefreshToken, sizeof(user->refreshToken));
              user->selected = true;
              setActiveUser(user);
              writeUsersJson();
              sptfAction = CurrentProfile;
            };
          }
        }
      }
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts, response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
    }
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }

  if (success) sptfAction = grant_type == gt_authorization_code ? CurrentProfile : CurrentlyPlaying;

  getting_token = false;
}

void sptfCurrentlyPlaying() {
  if (access_token[0] == '\0' || !activeUser) return;
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfCurrentlyPlaying()\n", ts);
  next_curplay_millis = 0;

  char url[18];
  sprintf(url, "/player?market=%s", activeUser->country);
  HTTP_response_t response = sptfApiRequest("GET", url);

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
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts, response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
    }
  } else if (response.httpCode == 204) {
    sptf_is_playing = false;
    sptfResetProgress();
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
}

/**
 * Get information about the current Spotify user
 */
void sptfCurrentProfile() {
  if (access_token[0] == '\0') return;
  uint32_t ts = millis();
  Serial.printf("\n> [%d] sptfCurrentProfile()\n", ts);

  HTTP_response_t response = sptfApiRequest("GET", "");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      const char *display_name = json["display_name"];
      const char *country = json["country"];
      strncpy(activeUser->name, display_name, sizeof(activeUser->name));
      strncpy(activeUser->country, country, sizeof(activeUser->country));
      writeUsersJson();
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts, response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
    }
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }

  sptfAction = CurrentlyPlaying;
}

void sptfNext() {
  HTTP_response_t response = sptfApiRequest("POST", "/player/next");
  if (response.httpCode == 204) {
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

void sptfPrevious() {
  HTTP_response_t response = sptfApiRequest("POST", "/player/previous");
  if (response.httpCode == 204) {
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

void sptfToggle() {
  if (access_token[0] == '\0') return;

  bool was_playing = sptf_is_playing;
  sptf_is_playing = !sptf_is_playing;
  HTTP_response_t response;
  if (activeDeviceId[0] != '\0') {
    char path[65];
    snprintf(path, sizeof(path), was_playing ? "/player/pause?device_id=%s" : "/player/play?device_id=%s",
             activeDeviceId);
    response = sptfApiRequest("PUT", path);
  } else {
    response = sptfApiRequest("PUT", was_playing ? "/player/pause" : "/player/play");
  }

  if (response.httpCode == 204) {
    next_curplay_millis = millis() + 200;
  } else {
    sptf_is_playing = !sptf_is_playing;
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  sptfAction = CurrentlyPlaying;
};

void sptfPlayGenre(const char *playlistId) {
  if (access_token[0] == '\0') return;

  sptf_is_playing = false;
  playingGenreIndex = genreIndex;
  char requestContent[59];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"spotify:playlist:%s\"}", playlistId);
  HTTP_response_t response;
  if (activeDeviceId[0] != '\0') {
    char path[64];
    snprintf(path, sizeof(path), "/player/play?device_id=%s", activeDeviceId);
    response = sptfApiRequest("PUT", path, requestContent);
  } else {
    response = sptfApiRequest("PUT", "/player/play", requestContent);
  }

  if (response.httpCode == 204) {
    sptf_is_playing = true;
    sptfResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
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
  HTTP_response_t response = sptfApiRequest("GET", "/player/devices");
  if (response.httpCode == 200) {
    DynamicJsonDocument doc(4000);
    DeserializationError error = deserializeJson(doc, response.payload);

    if (!error) {
      JsonArray jsonDevices = doc["devices"];
      devicesCount = min(MAX_DEVICES, jsonDevices.size());
      if (devicesCount == 0) activeDeviceId[0] = '\0';
      if (menuMode == DeviceList) menuSize = devicesCount;
      for (uint8_t i = 0; i < devicesCount; i++) {
        JsonObject jsonDevice = doc["devices"][i];
        const char *id = jsonDevice["id"];
        const char *name = jsonDevice["name"];
        bool is_active = jsonDevice["is_active"];
        uint8_t volume_percent = jsonDevice["volume_percent"];

        SptfDevice_t *device = &devices[i];
        strncpy(device->id, id, sizeof(device->id));
        strncpy(device->name, name, sizeof(device->name));
        device->volumePercent = volume_percent;
        if (is_active) {
          activeDevice = device;
          strncpy(activeDeviceId, id, sizeof(activeDeviceId));
          if (menuMode == DeviceList) {
            setMenuMode(DeviceList, i);
            lastInputMillis = millis();
            updateDisplay();
          }
        }
      }
    }
  }
  sptfAction = CurrentlyPlaying;
}
