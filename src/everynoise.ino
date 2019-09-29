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
const uint16_t SPOTIFY_MAX_POLLING_DELAY = 30000;
const uint16_t SPOTIFY_MIN_POLLING_INTERVAL = 10000;

typedef struct {
  int httpCode;
  String payload;
} HTTP_response_t;

typedef struct {
  char name[64] = "";
  char refreshToken[150] = "";
  char country[3] = "";
  bool selected = false;
  char selectedDeviceId[41] = "";
} SptfUser_t;

typedef struct {
  char id[41] = "";
  char name[64] = "";
  uint8_t volumePercent;
} SptfDevice_t;

enum SpotifyActions { Idle, GetToken, CurrentlyPlaying, CurrentProfile, Next, Previous, Toggle, PlayGenre, GetDevices };

enum GrantTypes { gt_authorization_code, gt_refresh_token };

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

String nodeName = "everynoisebox";
String spotifyAuthCode;
RTC_DATA_ATTR char spotifyAccessToken[300] = "";

RTC_DATA_ATTR time_t token_lifetime = 0;
RTC_DATA_ATTR time_t token_time = 0;
uint32_t last_curplay_millis = 0;
uint32_t next_curplay_millis = 0;

bool spotifyGettingToken = false;
bool spotifyIsPlaying = false;
bool send_events = false;
uint32_t progress_ms = 0;

SpotifyActions spotifyAction = Idle;

int playingGenreIndex = -1;
uint32_t genreIndex = 0;
unsigned long inactivityMillis = 45000;
unsigned long lastInputMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;

char statusMessage[20] = "";
unsigned long statusMessageUntilMillis = 0;

enum MenuModes {
  RootMenu = 0,
  UserList = 1,
  DeviceList = 2,
  BookmarksList = 3,
  AlphabeticList = 4,
  AlphabeticSuffixList = 5,
  PopularityList = 6,
  ModernityList = 7,
  BackgroundList = 8,
  TempoList = 9,
  ToggleBookmark = 10
};
MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
uint32_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"play/pause",         "users",      "devices",   "bookmarks",  "name prefix",
                               "name suffix",        "popularity", "modernity", "background", "tempo",
                               "add/remove bookmark"};
#define MAX_USERS 10
SptfUser_t users[MAX_USERS] = {};
uint8_t usersCount = 0;
SptfUser_t *activeUser = NULL;
RTC_DATA_ATTR char spotifyRefreshToken[150] = "";

#define MAX_DEVICES 10
SptfDevice_t devices[MAX_DEVICES] = {};
uint8_t devicesCount = 0;
SptfDevice_t *activeDevice = NULL;
RTC_DATA_ATTR char activeDeviceId[41] = "";

#define MAX_BOOKMARKS 1024
uint16_t bookmarksCount = 95;
const char *bookmarkedGenres[MAX_BOOKMARKS] = {"indie rock",
                                               "indie punk",
                                               "nu gaze",
                                               "alternative rock",
                                               "garage psych",
                                               "indie pop",
                                               "chamber pop",
                                               "indietronica",
                                               "modern rock",
                                               "lo-fi",
                                               "freak folk",
                                               "dream pop",
                                               "new rave",
                                               "garage rock",
                                               "electropop",
                                               "post-rock",
                                               "noise pop",
                                               "indie folk",
                                               "shimmer pop",
                                               "american shoegaze",
                                               "anti-folk",
                                               "folk-pop",
                                               "alternative emo",
                                               "dance-punk",
                                               "slow core",
                                               "philly indie",
                                               "shoegaze",
                                               "alternative metal",
                                               "post-hardcore",
                                               "la indie",
                                               "chillwave",
                                               "art pop",
                                               "metropopolis",
                                               "modern blues rock",
                                               "escape room",
                                               "indie shoegaze",
                                               "noise rock",
                                               "compositional ambient",
                                               "gbvfi",
                                               "indie surf",
                                               "american post-rock",
                                               "dreamgaze",
                                               "post-metal",
                                               "alternative pop",
                                               "alternative dance",
                                               "canadian indie",
                                               "permanent wave",
                                               "small room",
                                               "neo-psychedelic",
                                               "punk blues",
                                               "seattle indie",
                                               "austindie",
                                               "rock",
                                               "math rock",
                                               "experimental pop",
                                               "indie garage rock",
                                               "chamber psych",
                                               "australian garage punk",
                                               "baltimore indie",
                                               "grave wave",
                                               "dark post-punk",
                                               "midwest emo",
                                               "boston rock",
                                               "blues-rock",
                                               "instrumental post-rock",
                                               "baroque pop",
                                               "indie soul",
                                               "quebec indie",
                                               "bay area indie",
                                               "australian indie",
                                               "vancouver indie",
                                               "chicago indie",
                                               "brooklyn indie",
                                               "toronto indie",
                                               "new jersey indie",
                                               "power pop",
                                               "experimental rock",
                                               "popgaze",
                                               "post-doom metal",
                                               "ok indie",
                                               "electronic rock",
                                               "scottish indie",
                                               "big beat",
                                               "etherpop",
                                               "folk punk",
                                               "trip hop",
                                               "downtempo",
                                               "melbourne indie",
                                               "portland indie",
                                               "neo-synthpop",
                                               "progressive post-hardcore",
                                               "swedish indie rock",
                                               "grunge",
                                               "melodic hardcore",
                                               "stoner rock"};

uint32_t menuSize = GENRE_COUNT;
uint32_t menuIndex = 0;
int lastKnobPosition = 0;
double lastKnobSpeed = 0.0;

TaskHandle_t spotifyApiTask;

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
  menuIndex = genreIndex = random(GENRE_COUNT);

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
    if (spotifyAccessToken[0] == '\0' && !spotifyGettingToken) {
      request->redirect("http://" + nodeName + ".local/authorize");
    } else {
      request->send(SPIFFS, "/index.html");
    }
  });

  server.on("/authorize", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    Serial.printf("\n> [%d] server.on /\n", ts);
    spotifyGettingToken = true;
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
             SPOTIFY_CLIENT_ID);
    Serial.printf("  [%d] Redirect to: %s\n", ts, auth_url);
    request->redirect(auth_url);
  });

  server.on("/callback", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAuthCode = "";
    uint8_t paramsNr = request->params();
    for (uint8_t i = 0; i < paramsNr; i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "code") {
        spotifyAuthCode = p->value();
        spotifyAction = GetToken;
        break;
      }
    }
    if (spotifyAction == GetToken) {
      request->redirect("/");
    } else {
      request->send(204);
    }
  });

  server.on("/next", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAction = Next;
    request->send(204);
  });

  server.on("/previous", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAction = Previous;
    request->send(204);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAction = Toggle;
    request->send(204);
  });

  server.on("/heap", HTTP_GET,
            [](AsyncWebServerRequest *request) { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

  server.on("/resetusers", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAccessToken[0] = '\0';
    spotifyRefreshToken[0] = '\0';
    SPIFFS.remove("/users.json");
    spotifyAction = Idle;
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

  readDataJson();

  xTaskCreate(spotifyApiLoop,   /* Function to implement the task */
              "spotifyApiLoop", /* Name of the task */
              10000,            /* Stack size in words */
              NULL,             /* Task input parameter */
              1,                /* Priority of the task */
              &spotifyApiTask); /* Task handle. */
}

int getGenreIndex(const char *genreName) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    if (strcmp(genreName, genres[i]) == 0) return i;
  }
  return -1;
}

const char *genrePtr(const char *genreName) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    if (strcmp(genreName, genres[i]) == 0) return genres[i];
  }
  return NULL;
}

void setMenuMode(MenuModes newMode, uint32_t newMenuIndex) {
  menuMode = newMode;
  switch (menuMode) {
    case RootMenu:
      menuSize = sizeof(rootMenuItems) / sizeof(rootMenuItems[0]);

      // toggle bookmark menu item
      if (lastMenuMode == RootMenu || lastMenuMode == UserList || lastMenuMode == DeviceList) menuSize--;
      break;
    case UserList:
      menuSize = usersCount;
      break;
    case DeviceList:
      menuSize = devicesCount == 0 ? 1 : devicesCount;
      break;
    case BookmarksList:
      menuSize = bookmarksCount == 0 ? 1 : bookmarksCount;
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      menuSize = GENRE_COUNT;
      break;
  }
  menuIndex = newMenuIndex % menuSize;
}

void knobRotated(ESPRotary &r) {
  unsigned long now = millis();
  unsigned long lastInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  lastInputMillis = now;

  int newPosition = r.getPosition();
  int positionDelta = newPosition - lastKnobPosition;
  lastKnobPosition = newPosition;

  int steps = 1;
  if (menuSize > 20 && lastInputDelta > 1 && lastInputDelta <= 25) {
    double speed = (3 * lastKnobSpeed + (double)positionDelta / lastInputDelta) / 4.0;
    lastKnobSpeed = speed;
    steps = min(50, max(1, (int)(fabs(speed) * 180)));
  } else {
    lastKnobSpeed = 0.0;
  }

  int newMenuIndex = ((int)menuIndex + (positionDelta * steps)) % (int)menuSize;
  if (newMenuIndex < 0) newMenuIndex += menuSize;
  // Serial.printf("newIndex=%d from old=%d pos=%d delta=%d steps=%d size=%d\n", newMenuIndex, menuIndex, newPosition,
  // positionDelta, steps, menuSize);
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
    case BookmarksList:
      genreIndex = max(0, getGenreIndex(bookmarkedGenres[menuIndex]));
      break;
    default:
      break;
  }

  spotifyAction = Idle;
}

void knobClicked() {
  lastInputMillis = millis();

  switch (menuMode) {
    case UserList:
      if (usersCount > 0) {
        setActiveUser(&users[menuIndex]);
        token_lifetime = 0;
        token_time = 0;
        spotifyAccessToken[0] = '\0';
        updateDisplay();
        writeDataJson();
      }
      break;
    case DeviceList:
      if (devicesCount > 0) {
        setActiveDevice(&devices[menuIndex]);
        writeDataJson();
      }
      break;
    case BookmarksList:
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case BackgroundList:
    case TempoList:
      spotifyAction = PlayGenre;
      break;
    default:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (spotifyAccessToken[0] == '\0') return;
  spotifyAction = Next;
}

void knobLongPressStarted() {
  lastInputMillis = millis();
  if (menuMode != RootMenu) {
    lastMenuMode = menuMode;
    lastMenuIndex = menuIndex;
  }
  if (spotifyIsPlaying) {
    setMenuMode(RootMenu, 0);
  } else {
    setMenuMode(RootMenu, (uint32_t)menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  if (menuIndex == 0) {
    if (spotifyAccessToken[0] != '\0') spotifyAction = Toggle;
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex == ToggleBookmark) {
    if (!isBookmarked(genreIndex)) {
      addBookmark(genreIndex);
      strcpy(statusMessage, "added");
    } else {
      removeBookmark(genreIndex);
      if (lastMenuMode == BookmarksList) {
        lastMenuIndex = (lastMenuIndex == 0) ? 0 : min(lastMenuIndex, bookmarksCount - 1);
      }
      strcpy(statusMessage, "removed");
    }
    statusMessageUntilMillis = millis() + 1000;
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
            if (strcmp(users[i].refreshToken, spotifyRefreshToken) == 0) {
              newMenuIndex = i;
              break;
            }
          }
          if (users[newMenuIndex].name[0] == '\0') spotifyAction = CurrentProfile;
        }
        break;
      case DeviceList:
        if (devicesCount == 0 || activeDevice == NULL) {
          spotifyAction = GetDevices;
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
      case BookmarksList:
        newMenuIndex = 0;
        for (uint16_t i = 0; i < bookmarksCount; i++) {
          if (strcmp(bookmarkedGenres[i], genres[genreIndex]) == 0) {
            newMenuIndex = i;
            break;
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
  int8_t t = current_millis % 6;
  bool connected = WiFi.isConnected();
  if (current_millis < 500) {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);
    display.drawString(centerX, lineTwo, "every noise");
    display.drawString(centerX, lineThree, "at once");
    display.drawRect(t, t, 127 - (t * 2), 63 - (t * 2));
    display.display();
    lastDisplayMillis = current_millis;
    delay(80);
  } else if (lastInputMillis > lastDisplayMillis || (current_millis - lastDisplayMillis) > 10) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(Dialog_plain_12);

    if (!connected) {
      if (t == 0) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, ".");
      if (t == 1) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "...");
      if (t == 2) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, ".:.");
      if (t == 3) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "-:-");
      if (t == 4) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "_-_");
      if (t == 5) display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "_");
      delay(80);
    } else if (usersCount == 0) {
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "setup at http://");
      display.drawStringMaxWidth(centerX, lineThree, maxWidth, nodeName + ".local");
    } else if (spotifyGettingToken) {
      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "spotify...");
    } else if (menuMode == RootMenu) {
      display.drawRect(7, 9, 114, 32);
      if (menuIndex == 0) {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, spotifyIsPlaying ? "pause" : "play");
      } else if (menuIndex == ToggleBookmark) {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, isBookmarked(genreIndex) ? "- bookmark" : "+ bookmark");
      } else {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, rootMenuItems[menuIndex]);
      }
    } else if (menuMode == UserList) {
      char header[7];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      display.drawStringMaxWidth(centerX, lineOne, maxWidth, header);

      SptfUser_t *user = &users[menuIndex];
      if (user->name[0] == '\0') {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "loading...");
      } else if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) {
        char selectedName[66];
        sprintf(selectedName, "[%s]", user->name);
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, selectedName);
      } else {
        display.drawStringMaxWidth(centerX, lineTwo, maxWidth, user->name);
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
      // } else if (menuMode == BookmarksList) {
      //   if (bookmarksCount == 0) {
      //     display.drawStringMaxWidth(centerX, lineTwo, maxWidth, "no bookmarks yet");
      //   } else {
      //     char header[7];
      //     sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      //     display.drawStringMaxWidth(centerX, lineOne, maxWidth, header);

      //     const char *genre = bookmarkedGenres[menuIndex];
      //     display.drawStringMaxWidth(centerX, lineTwo, maxWidth, genre);
      //   }
    } else {
      const char *text;
      if (menuMode == BookmarksList) {
        if (bookmarksCount == 0) {
          text = "no bookmarks yet";
        } else {
          text = bookmarkedGenres[menuIndex];
        }
      } else {
        text = genres[genreIndex];
      }

      display.drawStringMaxWidth(centerX, lineTwo, maxWidth, text);

      if (current_millis < statusMessageUntilMillis && statusMessage[0] != '\0') {
        display.drawString(centerX, lineOne, statusMessage);
      } else if (spotifyIsPlaying && (spotifyAction == Idle || spotifyAction == CurrentlyPlaying) &&
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
      } else if (spotifyAction == PlayGenre || (spotifyAction == Toggle && !spotifyIsPlaying)) {
        display.drawString(centerX, lineOne, "play");
      } else if (spotifyAction == Toggle && spotifyIsPlaying) {
        display.drawString(centerX, lineOne, "pause");
      } else if (spotifyAction == Previous) {
        display.drawString(centerX, lineOne, "previous");
      } else if (spotifyAction == Next) {
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

  if (connected && !spotifyGettingToken && (spotifyAccessToken[0] == '\0' || token_age >= token_lifetime)) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  } else {
    updateDisplay();
  }
}

void spotifyApiLoop(void *params) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t cur_millis = millis();

      switch (spotifyAction) {
        case Idle:
          break;
        case GetToken:
          if (spotifyAuthCode != "") {
            spotifyGetToken(spotifyAuthCode.c_str(), gt_authorization_code);
          } else if (spotifyRefreshToken[0] != '\0') {
            spotifyGetToken(spotifyRefreshToken, gt_refresh_token);
          }
          break;
        case CurrentlyPlaying:
          if (next_curplay_millis && (cur_millis >= next_curplay_millis) &&
              (cur_millis - last_curplay_millis >= SPOTIFY_MIN_POLLING_INTERVAL)) {
            spotifyCurrentlyPlaying();
          } else if (cur_millis - last_curplay_millis >= SPOTIFY_MAX_POLLING_DELAY) {
            spotifyCurrentlyPlaying();
          }
          break;
        case CurrentProfile:
          spotifyCurrentProfile();
          break;
        case Next:
          spotifyNext();
          break;
        case Previous:
          spotifyPrevious();
          break;
        case Toggle:
          spotifyToggle();
          break;
        case PlayGenre:
          playingGenreIndex = genreIndex;
          spotifyPlayPlaylist(playlists[genreIndex]);
          break;
        case GetDevices:
          spotifyGetDevices();
          break;
      }
    }
    delay(10);
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
  strncpy(spotifyRefreshToken, activeUser->refreshToken, sizeof(spotifyRefreshToken));
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

bool readDataJson() {
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

bool writeDataJson() {
  File f = SPIFFS.open("/users.json", "w+");
  DynamicJsonDocument doc(3000);

  for (uint8_t i = 0; i < usersCount; i++) {
    SptfUser_t *user = &users[i];
    JsonObject obj = doc.createNestedObject();
    obj["name"] = user->name;
    obj["token"] = user->refreshToken;
    obj["country"] = user->country;
    obj["selectedDeviceId"] = user->selectedDeviceId;
    obj["selected"] = (bool)(strcmp(user->refreshToken, spotifyRefreshToken) == 0);
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

bool isBookmarked(uint32_t genreIndex) {
  for (int i = 0; i < bookmarksCount; i++) {
    if (strcmp(bookmarkedGenres[i], genres[genreIndex]) == 0) {
      return true;
    }
  }
  return false;
}

void addBookmark(uint32_t genreIndex) {
  if (bookmarksCount < MAX_BOOKMARKS) {
    bookmarkedGenres[bookmarksCount++] = genres[genreIndex];
  }
}

void removeBookmark(uint32_t genreIndex) {
  for (int i = 0; i < bookmarksCount; i++) {
    if (strcmp(bookmarkedGenres[i], genres[genreIndex]) == 0) {
      bookmarksCount--;
      for (int j = i; j < bookmarksCount - 1; j++) bookmarkedGenres[j] = bookmarkedGenres[j + 1];
      break;
    }
  }
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

HTTP_response_t spotifyApiRequest(const char *method, const char *endpoint, const char *content = "") {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] spotifyApiRequest(%s, %s, %s)\n", ts, method, endpoint, content);

  char headers[512];
  snprintf(headers, sizeof(headers),
           "%s /v1/me%s HTTP/1.1\r\n"
           "Host: api.spotify.com\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n\r\n",
           method, endpoint, spotifyAccessToken, strlen(content));

  HTTP_response_t response = httpRequest("api.spotify.com", 443, headers, content);

  if (response.httpCode == 401) {
    Serial.println("401 Unauthorized, clearing spotifyAccessToken");
    spotifyAccessToken[0] = '\0';
  }

  return response;
}

/**
 * Get Spotify token
 *
 * @param code          Either an authorization code or a refresh token
 * @param grant_type    [gt_authorization_code|gt_refresh_token]
 */
void spotifyGetToken(const char *code, GrantTypes grant_type) {
  uint32_t ts = millis();
  Serial.printf("\n> [%d] spotifyGetToken(%s, %s)\n", ts, code,
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

  uint8_t basicAuthSize = sizeof(SPOTIFY_CLIENT_ID) + sizeof(SPOTIFY_CLIENT_SECRET);
  char basicAuth[basicAuthSize];
  snprintf(basicAuth, basicAuthSize, "%s:%s", SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);

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
      strncpy(spotifyAccessToken, json["access_token"], sizeof(spotifyAccessToken));
      if (spotifyAccessToken[0] != '\0') {
        token_lifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval now;
        gettimeofday(&now, NULL);
        token_time = now.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          const char *newRefreshToken = json["refresh_token"];
          if (strcmp(newRefreshToken, spotifyRefreshToken) != 0) {
            strncpy(spotifyRefreshToken, newRefreshToken, sizeof(spotifyRefreshToken));
            bool found = false;
            for (uint8_t i = 0; i < usersCount; i++) {
              SptfUser_t *user = &users[i];
              if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) {
                found = true;
                break;
              }
            }
            if (!found && usersCount < MAX_USERS) {
              SptfUser_t *user = &users[usersCount++];
              strncpy(user->refreshToken, spotifyRefreshToken, sizeof(user->refreshToken));
              user->selected = true;
              setActiveUser(user);
              writeDataJson();
              spotifyAction = CurrentProfile;
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

  if (success) spotifyAction = grant_type == gt_authorization_code ? CurrentProfile : CurrentlyPlaying;

  spotifyGettingToken = false;
}

void spotifyCurrentlyPlaying() {
  if (spotifyAccessToken[0] == '\0' || !activeUser) return;
  uint32_t ts = millis();
  Serial.printf("\n> [%d] spotifyCurrentlyPlaying()\n", ts);
  next_curplay_millis = 0;

  char url[18];
  sprintf(url, "/player?market=%s", activeUser->country);
  HTTP_response_t response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(5000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      last_curplay_millis = millis();
      spotifyIsPlaying = json["is_playing"];
      progress_ms = json["progress_ms"];
      uint32_t duration_ms = json["item"]["duration_ms"];

      // Check if current song is about to end
      if (spotifyIsPlaying) {
        uint32_t remaining_ms = duration_ms - progress_ms;
        if (remaining_ms < SPOTIFY_MAX_POLLING_DELAY) {
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
    spotifyIsPlaying = false;
    spotifyResetProgress();
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
}

/**
 * Get information about the current Spotify user
 */
void spotifyCurrentProfile() {
  if (spotifyAccessToken[0] == '\0') return;
  uint32_t ts = millis();
  Serial.printf("\n> [%d] spotifyCurrentProfile()\n", ts);

  HTTP_response_t response = spotifyApiRequest("GET", "");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      const char *display_name = json["display_name"];
      const char *country = json["country"];
      strncpy(activeUser->name, display_name, sizeof(activeUser->name));
      strncpy(activeUser->country, country, sizeof(activeUser->country));
      writeDataJson();
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts, response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
    }
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }

  spotifyAction = CurrentlyPlaying;
}

void spotifyNext() {
  HTTP_response_t response = spotifyApiRequest("POST", "/player/next");
  if (response.httpCode == 204) {
    spotifyResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyPrevious() {
  HTTP_response_t response = spotifyApiRequest("POST", "/player/previous");
  if (response.httpCode == 204) {
    spotifyResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyToggle() {
  if (spotifyAccessToken[0] == '\0') return;

  bool was_playing = spotifyIsPlaying;
  spotifyIsPlaying = !spotifyIsPlaying;
  HTTP_response_t response;
  if (activeDeviceId[0] != '\0') {
    char path[65];
    snprintf(path, sizeof(path), was_playing ? "/player/pause?device_id=%s" : "/player/play?device_id=%s",
             activeDeviceId);
    response = spotifyApiRequest("PUT", path);
  } else {
    response = spotifyApiRequest("PUT", was_playing ? "/player/pause" : "/player/play");
  }

  if (response.httpCode == 204) {
    next_curplay_millis = millis() + 200;
  } else {
    spotifyIsPlaying = !spotifyIsPlaying;
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyPlayPlaylist(const char *playlistId) {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyIsPlaying = false;
  char requestContent[59];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"spotify:playlist:%s\"}", playlistId);
  HTTP_response_t response;
  if (activeDeviceId[0] != '\0') {
    char path[64];
    snprintf(path, sizeof(path), "/player/play?device_id=%s", activeDeviceId);
    response = spotifyApiRequest("PUT", path, requestContent);
  } else {
    response = spotifyApiRequest("PUT", "/player/play", requestContent);
  }

  if (response.httpCode == 204) {
    spotifyIsPlaying = true;
    spotifyResetProgress();
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyResetProgress() {
  progress_ms = 0;
  last_curplay_millis = millis();
  next_curplay_millis = millis() + 200;
};

void spotifyGetDevices() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response = spotifyApiRequest("GET", "/player/devices");
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
        if (is_active || strcmp(activeDeviceId, id) == 0) {
          activeDevice = device;
          strncpy(activeDeviceId, id, sizeof(activeDeviceId));
          if (menuMode == DeviceList) menuIndex = i;
        }
      }
    }
  }
  spotifyAction = CurrentlyPlaying;
}
