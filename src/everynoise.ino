#include "ArduinoJson.h"
#include "ESPAsyncWebServer.h"
#include "ESPRotary.h"
#include "ESPmDNS.h"
#include "OneButton.h"
#include "SPIFFS.h"
#include "TFT_eSPI.h"
#include "WiFiClientSecure.h"
#include "base64.h"
#include "driver/rtc_io.h"
#include "time.h"

#define FONT_NAME "GillSans-Light24"
#define LINE_HEIGHT 34
TFT_eSPI tft = TFT_eSPI(135, 240);
TFT_eSprite img = TFT_eSprite(&tft);

#define ROTARY_ENCODER_A_PIN 12
#define ROTARY_ENCODER_B_PIN 13
#define ROTARY_ENCODER_BUTTON_PIN 15
ESPRotary knob(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, 4);
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

#include "genres.h"
#include "settings.h"

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define startsWith(STR, SEARCH) (strncmp(STR, SEARCH, strlen(SEARCH)) == 0)
const uint16_t SPOTIFY_MAX_POLLING_DELAY = 20000;
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

enum SpotifyActions {
  Idle,
  GetToken,
  CurrentlyPlaying,
  CurrentProfile,
  Next,
  Previous,
  Toggle,
  PlayGenre,
  GetDevices,
  SetVolume,
  ToggleShuffle
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

String nodeName = "everynoisebox";
String spotifyAuthCode;
RTC_DATA_ATTR char spotifyAccessToken[300] = "";

RTC_DATA_ATTR time_t token_lifetime = 0;
RTC_DATA_ATTR time_t token_time = 0;
uint32_t spotifyLastUpdateMillis = 0;
uint32_t next_curplay_millis = 0;

bool spotifyGettingToken = false;
bool spotifyIsPlaying = false;
bool spotifyIsShuffled = false;
bool send_events = false;
uint32_t progress_ms = 0;

SpotifyActions spotifyAction = Idle;

int playingGenreIndex = -1;
uint32_t genreIndex = 0;
unsigned long inactivityMillis = 60000;
unsigned long lastInputMillis = 1;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;
bool displayInvalidated = true;
bool displayInvalidatedPartial = false;

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
  ToggleBookmarkItem = 9,
  ToggleShuffleItem = 10,
  VolumeControl = 11
};
MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
uint32_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"play/pause",       "users",      "devices",   "bookmarks",  "name",
                               "name ending",      "popularity", "modernity", "background",
                               "add/del bookmark", "shuffle",    "volume"};
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
uint16_t bookmarksCount = 0;
const char *bookmarkedGenres[MAX_BOOKMARKS];

uint32_t menuSize = GENRE_COUNT;
uint32_t menuIndex = 0;
int lastKnobPosition = 0;
double lastKnobSpeed = 0.0;
int toggleBookmarkIndex = -1;
int volumeControlIndex = -1;
int toggleShuffleIndex = -1;

TaskHandle_t spotifyApiTask;

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

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

  tft.init();
  tft.setRotation(3);
  tft.loadFont(FONT_NAME);
  img.loadFont(FONT_NAME);

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
    inactivityMillis = 120000;
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
    SPIFFS.remove("/data.json");
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

bool shouldShowToggleBookmark() {
  return lastMenuMode != RootMenu && lastMenuMode != UserList && lastMenuMode != DeviceList &&
         lastMenuMode != VolumeControl;
}

bool shouldShowVolumeControl() { return activeDevice != NULL; }

bool shouldShowToggleShuffle() { return true; }

void setMenuMode(MenuModes newMode, uint32_t newMenuIndex) {
  menuMode = newMode;

  if (menuMode == RootMenu) {
    int nextDynamicIndex = ToggleBookmarkItem;
    toggleBookmarkIndex = shouldShowToggleBookmark() ? nextDynamicIndex++ : -1;
    volumeControlIndex = shouldShowVolumeControl() ? nextDynamicIndex++ : -1;
    toggleShuffleIndex = shouldShowToggleShuffle() ? nextDynamicIndex++ : -1;
    menuSize = nextDynamicIndex;
  }

  switch (menuMode) {
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
      menuSize = GENRE_COUNT;
      break;
    case VolumeControl:
      menuSize = 101;  // 0-100%
      break;
    default:
      break;
  }
  menuIndex = newMenuIndex % menuSize;
  displayInvalidated = true;
}

void setStatusMessage(const char *message, unsigned long durationMs = 1300) {
  strncpy(statusMessage, message, sizeof(statusMessage));
  statusMessageUntilMillis = millis() + durationMs;
  displayInvalidated = true;
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

  if (menuMode == VolumeControl) {
    int newMenuIndex = (int)menuIndex + positionDelta;
    newMenuIndex = newMenuIndex < 0 ? 0 : min(newMenuIndex, menuSize - 1);
    menuIndex = newMenuIndex;
  } else {
    int newMenuIndex = ((int)menuIndex + (positionDelta * steps)) % (int)menuSize;
    if (newMenuIndex < 0) newMenuIndex += menuSize;
    // Serial.printf("newIndex=%d from old=%d pos=%d delta=%d steps=%d size=%d\n", newMenuIndex, menuIndex, newPosition,
    // positionDelta, steps, menuSize);
    menuIndex = newMenuIndex;
  }

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
    case BookmarksList:
      genreIndex = max(0, getGenreIndex(bookmarkedGenres[menuIndex]));
      break;
    case VolumeControl:
      if (activeDevice != NULL) {
        activeDevice->volumePercent = menuIndex;
      }
    default:
      break;
  }

  displayInvalidatedPartial = true;
  // spotifyAction = Idle;
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
        displayInvalidated = true;
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
      spotifyAction = PlayGenre;
      setStatusMessage("play");
      break;
    case VolumeControl:
      spotifyAction = SetVolume;
      break;
    default:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (spotifyAccessToken[0] == '\0') return;
  spotifyAction = Next;
  setStatusMessage("next");
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
    if (spotifyAccessToken[0] != '\0') {
      spotifyAction = Toggle;
      setStatusMessage(spotifyIsPlaying ? "pause" : "play");
    }
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex == toggleBookmarkIndex) {
    if (!isBookmarked(genreIndex)) {
      addBookmark(genreIndex);
      setStatusMessage("added");
    } else {
      removeBookmark(genreIndex);
      if (lastMenuMode == BookmarksList) {
        lastMenuIndex = (lastMenuIndex == 0) ? 0 : min(lastMenuIndex, bookmarksCount - 1);
      }
      setStatusMessage("deleted");
    }
    setMenuMode(lastMenuMode, lastMenuIndex);
    writeDataJson();
  } else if (menuIndex == volumeControlIndex) {
    if (activeDevice != NULL) {
      setMenuMode(VolumeControl, activeDevice->volumePercent);
    }
  } else if (menuIndex == toggleShuffleIndex) {
    setStatusMessage(spotifyIsShuffled ? "shuffle off" : "shuffle on");
    spotifyAction = ToggleShuffle;
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

void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1) {
  const uint16_t lineSpacing = 8;
  const uint16_t lineHeight = img.gFont.yAdvance + lineSpacing;
  const uint16_t spriteHeight = img.gFont.yAdvance * maxLines + lineSpacing * (maxLines - 1);
  const uint16_t centerX = maxWidth / 2 + 1;
  const uint16_t len = strlen(text);

  uint16_t lineNumber = 0;
  uint16_t pos = 0;
  int16_t totalWidth = 0;
  uint16_t index = 0;
  uint16_t preferredBreakpoint = 0;
  uint16_t widthAtBreakpoint = 0;
  uint16_t lastDrawnPos = 0;

  img.createSprite(maxWidth, spriteHeight);

  while (pos < len) {
    uint16_t lastPos = pos;
    uint16_t unicode = img.decodeUTF8((uint8_t *)text, &pos, len - pos);
    int16_t width = 0;

    if (img.getUnicodeIndex(unicode, &index)) {
      if (pos == 0) width = -img.gdX[index];
      if (pos == len - 1) {
        width = (img.gWidth[index] + img.gdX[index]);
      } else {
        width = img.gxAdvance[index];
      }
    } else {
      width = img.gFont.spaceWidth + 1;
    }
    totalWidth += width;

    // Always try to break on a space or dash
    if (unicode == ' ' || unicode == '-') {
      preferredBreakpoint = lastPos;
      widthAtBreakpoint = totalWidth;
    }

    if (totalWidth >= maxWidth) {
      if (preferredBreakpoint == 0) {
        preferredBreakpoint = lastPos;
        widthAtBreakpoint = totalWidth;
      }
      uint16_t lineLength = preferredBreakpoint - lastDrawnPos;
      uint16_t lineWidth = widthAtBreakpoint;
      // Ignore trailing spaces when centering
      if (unicode == ' ') {
        lineLength -= 1;
        lineWidth -= 2 * width;
      }
      char line[lineLength + 1] = { 0 };
      strncpy(line, &text[lastDrawnPos], lineLength);

      img.setCursor(centerX - lineWidth / 2, lineNumber * lineHeight);
      img.printToSprite(line, lineLength);
      lastDrawnPos = preferredBreakpoint + 1;
      // It is possible that we did not draw all letters to n so we need
      // to account for the width of the chars from `n - preferredBreakpoint`
      // by calculating the width we did not draw yet.
      totalWidth = totalWidth - widthAtBreakpoint;
      preferredBreakpoint = 0;
      lineNumber++;
    }
  }

  // Draw last part if needed
  if (lastDrawnPos < len) {
    uint16_t lineLength = len - lastDrawnPos;
    char line[lineLength + 1] = { 0 };
    strncpy(line, &text[lastDrawnPos], lineLength);
    img.setCursor(centerX - totalWidth / 2, lineNumber * lineHeight);
    img.printToSprite(line, lineLength);
  }

  img.pushSprite(tft.getCursorX(), tft.getCursorY());
  img.deleteSprite();
}

void updateDisplay() {
  const int centerX = 120;
  const int lineOne = 18;
  const int lineTwo = lineOne + LINE_HEIGHT;
  const int lineThree = lineTwo + LINE_HEIGHT;
  const int textPadding = 12;
  const int textWidth = tft.width() - (2 * textPadding);
  bool connected = WiFi.isConnected();
  displayInvalidated = false;
  if (!displayInvalidatedPartial) tft.fillScreen(TFT_BLACK);
  displayInvalidatedPartial = false;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  unsigned long currentMillis = millis();
  int8_t t = currentMillis % 6;

  if (currentMillis < 1000) {
    tft.drawString("every noise", centerX, lineTwo);
    tft.drawString("at once", centerX, lineThree);
    tft.drawRect(t * 2, t * 2, 239 - (t * 4), 134 - (t * 4), TFT_WHITE);
    delay(90 + random(50));
    displayInvalidated = true;
  } else {
    if (!connected || spotifyGettingToken) {
      if (t == 0) tft.drawString(".", centerX, 24);
      if (t == 1) tft.drawString("...", centerX, 24);
      if (t == 2) tft.drawString(".:.", centerX, 24);
      if (t == 3) tft.drawString("-:-", centerX, 24);
      if (t == 4) tft.drawString("_-_", centerX, 24);
      if (t == 5) tft.drawString("_", centerX, 24);

      if (currentMillis > 2000) {
        if (!connected) {
          tft.drawString("wi-fi", centerX, lineTwo);
        } else if (spotifyGettingToken) {
          tft.drawString("spotify", centerX, lineTwo);
        }
      }
      delay(90 + random(50));
      displayInvalidated = true;
    } else if (usersCount == 0) {
      tft.drawString("setup at http://", centerX, lineTwo);
      tft.drawString(nodeName + ".local", centerX, lineThree);
    } else if (menuMode == RootMenu) {
      tft.drawRect(10, 37, 219, 49, TFT_WHITE);
      tft.fillRect(11, 38, 217, 47, TFT_BLACK);
      tft.setCursor(textPadding, lineTwo);
      if (menuIndex == 0) {
        drawCenteredText(spotifyIsPlaying ? "pause" : "play", textWidth);
      } else if (menuIndex == toggleBookmarkIndex) {
        drawCenteredText(isBookmarked(genreIndex) ? "del bookmark" : "add bookmark", textWidth);
      } else if (menuIndex == volumeControlIndex) {
        drawCenteredText(rootMenuItems[VolumeControl], textWidth);
      } else if (menuIndex == toggleShuffleIndex) {
        drawCenteredText(spotifyIsShuffled ? "unshuffle" : "shuffle", textWidth);
      } else {
        drawCenteredText(rootMenuItems[menuIndex], textWidth);
      }
    } else if (menuMode == UserList) {
      char header[8];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(header, textWidth);

      SptfUser_t *user = &users[menuIndex];
      if (user->name[0] == '\0') {
        tft.setCursor(textPadding, lineTwo);
        drawCenteredText("loading...", textWidth);
      } else if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) {
        char selectedName[66];
        sprintf(selectedName, "[%s]", user->name);
        tft.setCursor(textPadding, lineTwo);
        drawCenteredText(selectedName, textWidth);
      } else {
        tft.setCursor(textPadding, lineTwo);
        drawCenteredText(user->name, textWidth);
      }
    } else if (menuMode == DeviceList) {
      if (devicesCount == 0) {
        tft.setCursor(textPadding, lineTwo);
        drawCenteredText("loading...", textWidth);
      } else {
        char header[7];
        sprintf(header, "%d / %d", menuIndex + 1, menuSize);
        tft.setCursor(textPadding, lineOne);
        drawCenteredText(header, textWidth);

        SptfDevice_t *device = &devices[menuIndex];
        if (strcmp(device->id, activeDeviceId) == 0) {
          char selectedName[66];
          sprintf(selectedName, "[%s]", device->name);
          tft.setCursor(textPadding, lineTwo);
          drawCenteredText(selectedName, textWidth);
        } else {
          tft.setCursor(textPadding, lineTwo);
          drawCenteredText(device->name, textWidth);
        }
      }
    } else if (menuMode == VolumeControl) {
      uint8_t x = 10;
      uint8_t y = 30;
      uint8_t width = 220;
      img.createSprite(width, 82);
      img.fillSprite(TFT_BLACK);
      img.drawRoundRect(0, 0, width, 32, 5, TFT_WHITE);
      img.fillRoundRect(4, 4, round(2.12 * menuIndex), 24, 3, TFT_LIGHTGREY);
      char label[4];
      sprintf(label, "%d%%", activeDevice->volumePercent);
      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      img.drawString(label, width / 2 - img.textWidth(label) / 2, 48);
      tft.drawRect(0, 0, x - 1, y - 1, TFT_BLACK);
      img.pushSprite(x, y);
      img.deleteSprite();
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
        img.setTextColor(genreColors[genreIndex], TFT_BLACK);
      }

      tft.setCursor(0, lineTwo);
      drawCenteredText(text, tft.width(), 2);

      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      if (currentMillis < statusMessageUntilMillis && statusMessage[0] != '\0') {
        tft.setCursor(0, lineOne);
        drawCenteredText(statusMessage, tft.width());
      } else if (spotifyIsPlaying && (spotifyAction == Idle || spotifyAction == CurrentlyPlaying) &&
                 playingGenreIndex == genreIndex) {
        uint32_t estimated_millis = progress_ms + (currentMillis - spotifyLastUpdateMillis);
        uint8_t seconds = estimated_millis / 1000 % 60;
        uint8_t minutes = estimated_millis / (1000 * 60) % 60;
        uint8_t hours = estimated_millis / (1000 * 60 * 60);
        char elapsed[10];
        if (hours == 0) {
          sprintf(elapsed, "%d:%02d", minutes, seconds);
        } else {
          sprintf(elapsed, "%d:%02d:%02d", hours, minutes, seconds);
        }
        tft.setCursor(0, lineOne);
        drawCenteredText(elapsed, tft.width());
      } else {
        char label[13];
        if (menuMode == TempoList) {
          sprintf(label, "~%s bpm", genreLabels_tempo[menuIndex]);
        } else {
          sprintf(label, "%d / %d", menuIndex + 1, menuSize);
        }
        tft.setCursor(0, lineOne);
        drawCenteredText(label, tft.width());
      }
    }

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

  uint32_t currentMillis = millis();
  struct timeval now;
  gettimeofday(&now, NULL);
  time_t currentSeconds = now.tv_sec;
  int token_age = currentSeconds - token_time;
  bool connected = WiFi.isConnected();

  if (currentMillis - lastInputMillis > inactivityMillis) {
    tft.fillScreen(TFT_BLACK);
    Serial.printf("\n> [%d] Entering deep sleep.\n", currentMillis);
    eventsSendLog("Entering deep sleep.");
    spotifyAction = Idle;
    WiFi.disconnect(true);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
    esp_deep_sleep_start();
  }

  if (!connected && currentMillis - lastReconnectAttemptMillis > 3000) {
    Serial.printf("> [%d] Trying to connect to network %s...\n", currentMillis, ssid);
    WiFi.begin();
    lastReconnectAttemptMillis = currentMillis;
  }

  if (connected && !spotifyGettingToken && (spotifyAccessToken[0] == '\0' || token_age >= token_lifetime)) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  if (statusMessage[0] != '\0' && currentMillis > statusMessageUntilMillis) {
    statusMessageUntilMillis = 0;
    statusMessage[0] = '\0';
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else if (spotifyIsPlaying && playingGenreIndex == genreIndex && (currentMillis - lastDisplayMillis) > 950) {
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else if (lastInputMillis > lastDisplayMillis) {
    displayInvalidated = true;
  }

  if (displayInvalidated) updateDisplay();
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
          if (spotifyLastUpdateMillis == 0 ||
              next_curplay_millis && (cur_millis >= next_curplay_millis) &&
              cur_millis - spotifyLastUpdateMillis >= SPOTIFY_MIN_POLLING_INTERVAL) {
            spotifyCurrentlyPlaying();
          }
          break;
        case CurrentProfile:
          spotifyCurrentProfile();
          displayInvalidated = true;
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
          displayInvalidated = true;
          break;
        case SetVolume:
          spotifySetVolume();
          break;
        case ToggleShuffle:
          spotifyToggleShuffle();
          break;
      }
    }
    delay(10);
  }
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
  File f = SPIFFS.open("/data.json", "r");
  DynamicJsonDocument doc(5000);
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (error) {
    Serial.printf("Failed to read data.json: %s\n", error.c_str());
    return false;
  } else {
    serializeJson(doc, Serial);
    Serial.println();
  }

  JsonArray usersArray = doc["users"];
  usersCount = min(MAX_USERS, usersArray.size());

  for (uint8_t i = 0; i < usersCount; i++) {
    JsonObject jsonUser = usersArray[i];
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

  JsonArray bookmarksArray = doc["bookmarks"];

  for (int i = 0; i < bookmarksArray.size(); i++) {
    const char *genreName = bookmarksArray[i];
    int genreIndex = getGenreIndex(genreName);
    if (genreIndex >= 0) addBookmark(genreIndex);
  }

  return true;
}

bool writeDataJson() {
  File f = SPIFFS.open("/data.json", "w+");
  DynamicJsonDocument doc(5000);

  JsonArray usersArray = doc.createNestedArray("users");

  for (uint8_t i = 0; i < usersCount; i++) {
    SptfUser_t *user = &users[i];
    JsonObject obj = usersArray.createNestedObject();
    obj["name"] = user->name;
    obj["token"] = user->refreshToken;
    obj["country"] = user->country;
    obj["selectedDeviceId"] = user->selectedDeviceId;
    obj["selected"] = (bool)(strcmp(user->refreshToken, spotifyRefreshToken) == 0);
  }

  JsonArray bookmarksArray = doc.createNestedArray("bookmarks");

  for (int i = 0; i < bookmarksCount; i++) {
    const char *genreName = bookmarkedGenres[i];
    bookmarksArray.add(genreName);
  }

  size_t bytes = serializeJson(doc, f);
  f.close();
  if (bytes <= 0) {
    Serial.printf("Failed to write data.json: %d\n", bytes);
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
        delay(10);
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
           base64::encode(basicAuth).c_str(), strlen(requestContent));

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

  if (success) spotifyAction = grant_type == gt_authorization_code ? CurrentProfile : GetDevices;

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
      spotifyLastUpdateMillis = millis();
      spotifyIsPlaying = json["is_playing"];
      spotifyIsShuffled = json["shuffle_state"];
      progress_ms = json["progress_ms"];
      uint32_t duration_ms = json["item"]["duration_ms"];

      if (json.containsKey("device")) {
        SptfDevice_t *device = activeDevice;
        if (device == NULL && devicesCount == 0) device = activeDevice = &devices[devicesCount];
        JsonObject jsonDevice = json["device"];
        strncpy(activeDeviceId, jsonDevice["id"], sizeof(device->id));
        strncpy(device->id, jsonDevice["id"], sizeof(device->id));
        strncpy(device->name, jsonDevice["name"], sizeof(device->name));
        device->volumePercent = jsonDevice["volume_percent"];
      }

      if (spotifyIsPlaying) {
        // Check if current song is about to end
        inactivityMillis = 90000;
        uint32_t remaining_ms = duration_ms - progress_ms;
        if (remaining_ms < SPOTIFY_MAX_POLLING_DELAY) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          next_curplay_millis = millis() + remaining_ms + 200;
        }
      } else if (spotifyAction == CurrentlyPlaying) {
        spotifyAction = Idle;
      }
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", ts, response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
    }
  } else if (response.httpCode == 204) {
    spotifyAction = Idle;
    spotifyIsPlaying = false;
    spotifyIsShuffled = false;
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
  spotifyAction = spotifyIsPlaying ? CurrentlyPlaying : Idle;
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
  spotifyLastUpdateMillis = millis();
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
        }
      }
    }
  }
  spotifyAction = CurrentlyPlaying;
}

void spotifySetVolume() {
  if (activeDevice == NULL) return;
  char path[85];
  snprintf(path, sizeof(path), "/player/volume?device_id=%s&volume_percent=%d", activeDevice->id,
           activeDevice->volumePercent);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", path);
  spotifyAction = CurrentlyPlaying;
}

void spotifyToggleShuffle() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyIsShuffled = !spotifyIsShuffled;
  HTTP_response_t response;

  char path[30];
  snprintf(path, sizeof(path), "/player/shuffle?state=%s", spotifyIsShuffled ? "true" : "false");
  response = spotifyApiRequest("PUT", path);

  if (response.httpCode == 204) {
    next_curplay_millis = millis() + 200;
  } else {
    spotifyIsShuffled = !spotifyIsShuffled;
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = spotifyIsPlaying ? CurrentlyPlaying : Idle;
};
