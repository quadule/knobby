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

#define FONT_NAME "GillSans24"
#define LINE_HEIGHT 28
TFT_eSPI tft = TFT_eSPI(135, 240);
TFT_eSprite img = TFT_eSprite(&tft);

const int centerX = 120;
const int lineOne = 13;
const int lineTwo = lineOne + LINE_HEIGHT + 3;
const int lineThree = lineTwo + LINE_HEIGHT;
const int lineSpacing = 4;
const int textPadding = 10;
const int textWidth = 239 - textPadding * 2;

#define ROTARY_ENCODER_A_PIN 12
#define ROTARY_ENCODER_B_PIN 13
#define ROTARY_ENCODER_BUTTON_PIN 15
ESPRotary knob(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, 4);
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

#include "genres.h"
#include "settings.h"
#include "spotify.h"

#define startsWith(STR, SEARCH) (strncmp(STR, SEARCH, strlen(SEARCH)) == 0)

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

String nodeName = "everynoisebox";
bool sendLogEvents = false;
int playingGenreIndex = -1;
RTC_DATA_ATTR uint16_t genreIndex = 0;
RTC_DATA_ATTR time_t lastSleepSeconds = 0;
unsigned long inactivityMillis = 60000;
unsigned long lastInputMillis = 1;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;
unsigned long randomizingGenreEndMillis = 0;
unsigned long randomizingGenreTicks = 0;
bool randomizingGenreAutoplay = false;
bool displayInvalidated = true;
bool displayInvalidatedPartial = false;
char statusMessage[24] = "";
unsigned long statusMessageUntilMillis = 0;

enum MenuModes {
  RootMenu = 0,
  UserList = 1,
  DeviceList = 2,
  BookmarksList = 3,
  CountryList = 4,
  AlphabeticList = 5,
  AlphabeticSuffixList = 6,
  PopularityList = 7,
  ModernityList = 8,
  ColorList = 9,
  ToggleBookmarkItem = 10,
  ToggleShuffleItem = 11,
  VolumeControl = 12
};
RTC_DATA_ATTR MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
uint16_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"play/pause",  "users",      "devices",   "bookmarks", "countries",        "name",
                               "name ending", "popularity", "modernity", "color",     "add/del bookmark", "shuffle",
                               "volume"};

#define MAX_BOOKMARKS 1024
uint16_t bookmarksCount = 0;
const char *bookmarkedGenres[MAX_BOOKMARKS];

RTC_DATA_ATTR uint16_t menuSize = GENRE_COUNT;
RTC_DATA_ATTR uint16_t menuIndex = 0;
int lastKnobPosition = 0;
float lastKnobSpeed = 0.0;
const unsigned long extraLongPressMillis = 1500;
unsigned long longPressStartedMillis = 0;
bool knobRotatedWhileLongPressed = false;
int toggleBookmarkIndex = -1;
int volumeControlIndex = -1;
int toggleShuffleIndex = -1;

TaskHandle_t spotifyApiTask;

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

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
    sendLogEvents = true;
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
    sendLogEvents = !sendLogEvents;
    request->send(200, "text/plain", sendLogEvents ? "1" : "0");
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

  tft.fillScreen(TFT_BLACK);
}

int getGenreIndexByName(const char *genreName) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    if (strcmp(genreName, genres[i]) == 0) return i;
  }
  return -1;
}

bool isGenreMenu(MenuModes mode) {
  return mode != RootMenu && mode != UserList && mode != DeviceList && mode != VolumeControl;
}

unsigned long longPressedMillis() {
  return longPressStartedMillis == 0 ? 0 : millis() - longPressStartedMillis;
}

unsigned long extraLongPressedMillis() {
  long extraMillis = (long)longPressedMillis() - extraLongPressMillis;
  return extraMillis < 0 ? 0 : extraMillis;
}

bool shouldShowRandom() {
  return longPressedMillis() > extraLongPressMillis && !knobRotatedWhileLongPressed && isGenreMenu(lastMenuMode);
}

bool shouldShowToggleBookmark() {
  return isGenreMenu(lastMenuMode);
}

bool shouldShowVolumeControl() { return activeSpotifyDevice != NULL; }

bool shouldShowToggleShuffle() { return true; }

uint16_t getMenuSize(MenuModes mode) {
  int nextDynamicIndex = ToggleBookmarkItem;

  switch (mode) {
    case RootMenu:
      toggleBookmarkIndex = shouldShowToggleBookmark() ? nextDynamicIndex++ : -1;
      volumeControlIndex = shouldShowVolumeControl() ? nextDynamicIndex++ : -1;
      toggleShuffleIndex = shouldShowToggleShuffle() ? nextDynamicIndex++ : -1;
      return nextDynamicIndex;
    case UserList:
      return usersCount;
    case DeviceList:
      return spotifyDevicesCount == 0 ? 1 : spotifyDevicesCount;
    case BookmarksList:
      return bookmarksCount == 0 ? 1 : bookmarksCount;
    case CountryList:
      return COUNTRY_COUNT;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case ColorList:
      return GENRE_COUNT;
    case VolumeControl:
      return 101;  // 0-100%
    default:
      return 0;
  }
}

uint16_t getGenreIndexForMenuIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case AlphabeticList:
      return index;
    case AlphabeticSuffixList:
      return genreIndexes_suffix[index];
    case PopularityList:
      return genreIndexes_popularity[index];
    case ModernityList:
      return genreIndexes_modernity[index];
    case ColorList:
      return genreIndexes_color[index];
    default:
      return 0;
  }
}

uint16_t getMenuIndexForGenreIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case AlphabeticList:
      return index;
    case AlphabeticSuffixList:
      return getIndexOfGenreIndex(genreIndexes_suffix, index);
    case PopularityList:
      return getIndexOfGenreIndex(genreIndexes_popularity, index);
    case ModernityList:
      return getIndexOfGenreIndex(genreIndexes_modernity, index);
    case ColorList:
      return getIndexOfGenreIndex(genreIndexes_color, index);
    default:
      return 0;
  }
}

uint16_t getIndexOfGenreIndex(const uint16_t indexes[], uint16_t indexToFind) {
  for (uint16_t i = 0; i < GENRE_COUNT; i++) {
    if (indexes[i] == indexToFind) return i;
  }
  return 0;
}

void setMenuMode(MenuModes newMode, uint16_t newMenuIndex) {
  menuMode = newMode;
  menuSize = getMenuSize(newMode);
  menuIndex = menuSize == 0 ? 0 : newMenuIndex % menuSize;
  displayInvalidated = true;
  displayInvalidatedPartial = false;
}

void setStatusMessage(const char *message, unsigned long durationMs = 1300) {
  strncpy(statusMessage, message, sizeof(statusMessage) - 1);
  statusMessageUntilMillis = millis() + durationMs;
  displayInvalidated = true;
  displayInvalidatedPartial = true;
}

void startRandomizingGenres(bool autoplay = false) {
  unsigned long currentMillis = millis();
  if (currentMillis > randomizingGenreEndMillis) {
    randomizingGenreEndMillis = millis() + 1500;
    randomizingGenreTicks = 0;
    randomizingGenreAutoplay = autoplay;
    displayInvalidated = true;
    displayInvalidatedPartial = false;
  }
}

void knobRotated(ESPRotary &r) {
  unsigned long now = millis();
  unsigned long lastInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  lastInputMillis = now;
  if (button.isLongPressed()) knobRotatedWhileLongPressed = true;

  int newPosition = r.getPosition();
  int positionDelta = newPosition - lastKnobPosition;
  lastKnobPosition = newPosition;

  if (menuSize == 0) return;
  int steps = 1;
  float speed = 0.0;
  if (menuSize > 20 && lastInputDelta >= 1 && lastInputDelta < 32) {
    speed = (float)positionDelta / (float)lastInputDelta;
    steps = max(1, (int)(fabs(speed) * 180));
  }
  lastKnobSpeed = lastInputDelta > 100 ? 0.0 : (17.0 * lastKnobSpeed + speed) / 18.0;

  if (menuMode == VolumeControl) {
    int newMenuIndex = (int)menuIndex + positionDelta;
    newMenuIndex = newMenuIndex < 0 ? 0 : min(newMenuIndex, menuSize - 1);
    menuIndex = newMenuIndex;
  } else {
    int newMenuIndex = ((int)menuIndex + (positionDelta * steps)) % (int)menuSize;
    if (newMenuIndex < 0) newMenuIndex += menuSize;
    menuIndex = newMenuIndex;
  }

  switch (menuMode) {
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case ColorList:
      genreIndex = getGenreIndexForMenuIndex(menuIndex, menuMode);
      break;
    case BookmarksList:
      genreIndex = max(0, getGenreIndexByName(bookmarkedGenres[menuIndex]));
      break;
    case VolumeControl:
      if (activeSpotifyDevice != NULL) {
        activeSpotifyDevice->volumePercent = menuIndex;
      }
    default:
      break;
  }

  displayInvalidated = true;
  displayInvalidatedPartial = true;
}

void knobClicked() {
  lastInputMillis = millis();

  switch (menuMode) {
    case UserList:
      if (usersCount > 0) {
        setActiveUser(&spotifyUsers[menuIndex]);
        spotifyTokenLifetime = 0;
        spotifyTokenSeconds = 0;
        spotifyAccessToken[0] = '\0';
        displayInvalidated = true;
        writeDataJson();
      }
      break;
    case DeviceList:
      if (spotifyDevicesCount > 0) {
        setActiveDevice(&spotifyDevices[menuIndex]);
        writeDataJson();
        if (spotifyState.isPlaying) spotifyAction = TransferPlayback;
      }
      break;
    case BookmarksList:
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
    case ModernityList:
    case ColorList:
      spotifyPlayPlaylistId = genrePlaylists[genreIndex];
      strncpy(spotifyState.playlistId, spotifyPlayPlaylistId, sizeof(spotifyState.playlistId) - 1);
      spotifyAction = PlayPlaylist;
      setStatusMessage("play");
      break;
    case CountryList:
      spotifyPlayPlaylistId = countryPlaylists[menuIndex];
      strncpy(spotifyState.playlistId, spotifyPlayPlaylistId, sizeof(spotifyState.playlistId) - 1);
      spotifyAction = PlayPlaylist;
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
  longPressStartedMillis = lastInputMillis = millis();
  knobRotatedWhileLongPressed = false;
  if (menuMode != RootMenu) {
    lastMenuMode = menuMode;
    lastMenuIndex = menuIndex;
  }
  if (spotifyState.isPlaying) {
    setMenuMode(RootMenu, 0);
  } else {
    setMenuMode(RootMenu, (uint16_t)menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  if (shouldShowRandom()) {
    startRandomizingGenres();
  } else if (menuIndex == 0) {
    if (spotifyAccessToken[0] != '\0') {
      spotifyAction = Toggle;
      setStatusMessage(spotifyState.isPlaying ? "pause" : "play");
    }
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex == toggleBookmarkIndex) {
    if (!isBookmarked(genreIndex)) {
      addBookmark(genreIndex);
      setStatusMessage("added");
    } else {
      removeBookmark(genreIndex);
      if (lastMenuMode == BookmarksList) {
        lastMenuIndex = (lastMenuIndex == 0) ? 0 : min((int)lastMenuIndex, (int)bookmarksCount - 1);
      }
      setStatusMessage("deleted");
    }
    setMenuMode(lastMenuMode, lastMenuIndex);
    writeDataJson();
  } else if (menuIndex == volumeControlIndex) {
    if (activeSpotifyDevice != NULL) {
      setMenuMode(VolumeControl, activeSpotifyDevice->volumePercent);
    }
  } else if (menuIndex == toggleShuffleIndex) {
    setStatusMessage(spotifyState.isShuffled ? "shuffle off" : "shuffle on");
    spotifyAction = ToggleShuffle;
    setMenuMode(lastMenuMode, lastMenuIndex);
  } else if (menuIndex != lastMenuIndex) {
    uint16_t newMenuIndex = lastMenuIndex;

    switch (menuIndex) {
      case UserList:
        if (usersCount == 0) {
          Serial.printf("No users found! Visit http://%s.local to sign in.\n", nodeName.c_str());
          setMenuMode(lastMenuMode, lastMenuIndex);
        } else {
          newMenuIndex = 0;
          for (uint8_t i = 1; i < usersCount; i++) {
            if (strcmp(spotifyUsers[i].refreshToken, spotifyRefreshToken) == 0) {
              newMenuIndex = i;
              break;
            }
          }
          if (spotifyUsers[newMenuIndex].name[0] == '\0') spotifyAction = CurrentProfile;
        }
        break;
      case DeviceList:
        if (spotifyDevicesCount == 0 || activeSpotifyDevice == NULL) {
          spotifyAction = GetDevices;
        } else {
          newMenuIndex = 0;
          for (uint8_t i = 0; i < spotifyDevicesCount; i++) {
            if (strcmp(spotifyDevices[i].id, activeSpotifyDeviceId) == 0) {
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
      case CountryList:
        newMenuIndex = random(COUNTRY_COUNT);
        break;
      case AlphabeticList:
      case AlphabeticSuffixList:
      case PopularityList:
      case ModernityList:
      case ColorList:
        newMenuIndex = getMenuIndexForGenreIndex(genreIndex, (MenuModes)menuIndex);
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)menuIndex, newMenuIndex);
  } else {
    setMenuMode(lastMenuMode, lastMenuIndex);
  }
  longPressStartedMillis = 0;
}

void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1) {
  const uint16_t lineHeight = img.gFont.yAdvance + lineSpacing;
  const uint16_t spriteHeight = img.gFont.yAdvance * maxLines + lineSpacing * (maxLines - 1);
  const uint16_t centerX = maxWidth / 2;
  const uint16_t len = strlen(text);

  uint16_t lineNumber = 0;
  uint16_t pos = 0;
  int16_t totalWidth = 0;
  uint16_t index = 0;
  uint16_t preferredBreakpoint = 0;
  uint16_t widthAtBreakpoint = 0;
  bool breakpointOnSpace = false;
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
    if (unicode == ' ') {
      preferredBreakpoint = pos;
      widthAtBreakpoint = totalWidth - width;
      breakpointOnSpace = true;
    } else if (unicode == '-') {
      preferredBreakpoint = pos;
      widthAtBreakpoint = totalWidth;
      breakpointOnSpace = false;
    }

    if (totalWidth >= maxWidth - width) {
      if (preferredBreakpoint == 0) {
        preferredBreakpoint = lastPos;
        widthAtBreakpoint = totalWidth;
        breakpointOnSpace = false;
      }
      uint16_t lineLength = preferredBreakpoint - lastDrawnPos;
      uint16_t lineWidth = widthAtBreakpoint;

      char line[lineLength + 1] = {0};
      strncpy(line, &text[lastDrawnPos], lineLength);

      img.setCursor(centerX - lineWidth / 2, lineNumber * lineHeight);
      img.printToSprite(line, lineLength);
      lastDrawnPos = preferredBreakpoint;
      // It is possible that we did not draw all letters to n so we need
      // to account for the width of the chars from `n - preferredBreakpoint`
      // by calculating the width we did not draw yet.
      totalWidth = totalWidth - widthAtBreakpoint;
      if (breakpointOnSpace) totalWidth -= img.gFont.spaceWidth + 1;
      preferredBreakpoint = 0;
      lineNumber++;
      if (lineNumber > maxLines) break;
    }
  }

  // Draw last part if needed
  if (lastDrawnPos < len) {
    uint16_t lineLength = len - lastDrawnPos;
    char line[lineLength + 1] = {0};
    strncpy(line, &text[lastDrawnPos], lineLength);
    img.setCursor(centerX - totalWidth / 2, lineNumber * lineHeight);
    img.printToSprite(line, lineLength);
  }

  img.pushSprite(tft.getCursorX(), tft.getCursorY());
  img.deleteSprite();
}

void formatMillis(char *output, unsigned long millis) {
  unsigned int seconds = millis / 1000 % 60;
  unsigned int minutes = millis / (1000 * 60) % 60;
  unsigned int hours = millis / (1000 * 60 * 60);

  if (hours == 0) {
    sprintf(output, "%d:%02d", minutes, seconds);
  } else {
    sprintf(output, "%d:%02d:%02d", hours, minutes, seconds);
  }
}

void updateDisplay() {
  displayInvalidated = false;
  if (!displayInvalidatedPartial) tft.fillScreen(TFT_BLACK);
  displayInvalidatedPartial = false;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  unsigned long currentMillis = millis();

  if (usersCount == 0) {
    tft.drawString("setup at http://", centerX, lineTwo);
    tft.drawString(nodeName + ".local", centerX, lineThree);
  } else if (currentMillis < randomizingGenreEndMillis) {
    randomizingGenreTicks += 1;
    genreIndex = random(getMenuSize(lastMenuMode));
    menuIndex = getMenuIndexForGenreIndex(genreIndex, lastMenuMode);
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    drawCenteredText(genres[genreIndex], textWidth, 3);
    auto delayMillis = pow(randomizingGenreTicks, 2);
    if (millis() + delayMillis >= randomizingGenreEndMillis) {
      if (randomizingGenreAutoplay) {
        spotifyAction = PlayPlaylist;
        spotifyPlayPlaylistId = genrePlaylists[genreIndex];
        setStatusMessage("play");
      }
      randomizingGenreEndMillis = 0;
      setMenuMode(lastMenuMode, menuIndex);
    }
    delay(delayMillis);
    displayInvalidated = true;
    displayInvalidatedPartial = randomizingGenreTicks > 1;
  } else if (menuMode == RootMenu) {
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    if (shouldShowRandom()) {
      double pressedProgress = min(1.0, (double)extraLongPressedMillis() / (double)extraLongPressMillis);
      tft.drawFastHLine(0, 0, (int)(pressedProgress * 239), TFT_WHITE);
      drawCenteredText("random", textWidth);
      delay(20);
    } else if (menuIndex == 0) {
      drawCenteredText(spotifyState.isPlaying ? "pause" : "play", textWidth);
    } else if (menuIndex == toggleBookmarkIndex) {
      drawCenteredText(isBookmarked(genreIndex) ? "del bookmark" : "add bookmark", textWidth);
    } else if (menuIndex == volumeControlIndex) {
      drawCenteredText(rootMenuItems[VolumeControl], textWidth);
    } else if (menuIndex == toggleShuffleIndex) {
      drawCenteredText(spotifyState.isShuffled ? "unshuffle" : "shuffle", textWidth);
    } else {
      drawCenteredText(rootMenuItems[menuIndex], textWidth);
    }
    tft.drawRoundRect(9, lineTwo - 15, 221, 49, 5, TFT_WHITE);
  } else if (menuMode == UserList) {
    char header[8];
    sprintf(header, "%d / %d", menuIndex + 1, menuSize);
    tft.setCursor(textPadding, lineOne);
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    drawCenteredText(header, textWidth);

    SpotifyUser_t *user = &spotifyUsers[menuIndex];
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    if (user->name[0] == '\0') {
      drawCenteredText("loading...", textWidth);
    } else if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) {
      char selectedName[66];
      sprintf(selectedName, "[%s]", user->name);
      drawCenteredText(selectedName, textWidth);
    } else {
      drawCenteredText(user->name, textWidth);
    }
  } else if (menuMode == DeviceList) {
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    if (spotifyDevicesCount == 0) {
      tft.setCursor(textPadding, lineTwo);
      drawCenteredText("loading...", textWidth);
    } else {
      char header[7];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(header, textWidth);

      SpotifyDevice_t *device = &spotifyDevices[menuIndex];
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      if (strcmp(device->id, activeSpotifyDeviceId) == 0) {
        char selectedName[66];
        sprintf(selectedName, "[%s]", device->name);
        drawCenteredText(selectedName, textWidth);
      } else {
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
    img.fillRoundRect(4, 4, round(2.12 * menuIndex), 24, 3, TFT_DARKGREY);
    char label[4];
    sprintf(label, "%d%%", activeSpotifyDevice->volumePercent);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    img.drawString(label, width / 2 - img.textWidth(label) / 2, 48);
    tft.drawRect(0, 0, x - 1, y - 1, TFT_BLACK);
    img.pushSprite(x, y);
    img.deleteSprite();
  } else {
    const char *text;
    const char *playlistId;

    if (menuMode == CountryList) {
      text = countries[menuIndex];
      playlistId = countryPlaylists[menuIndex];
    } else if (menuMode == BookmarksList) {
      text = bookmarksCount == 0 ? "no bookmarks yet" : bookmarkedGenres[genreIndex];
      playlistId = genrePlaylists[genreIndex];
    } else {
      text = genres[genreIndex];
      playlistId = genrePlaylists[genreIndex];
    }
    bool isActivePlaylist = strcmp(playlistId, spotifyState.playlistId) == 0;

    tft.setCursor(textPadding, lineOne);
    if (currentMillis < statusMessageUntilMillis && statusMessage[0] != '\0') {
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      drawCenteredText(statusMessage, textWidth);
    } else if (isActivePlaylist && (spotifyState.isPlaying || spotifyAction == PlayPlaylist)) {
      char elapsed[11];
      uint32_t estimatedMillis = spotifyState.progressMillis;
      if (spotifyState.isPlaying) estimatedMillis += currentMillis - spotifyState.lastUpdateMillis;
      formatMillis(elapsed, estimatedMillis);

      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(textPadding, lineOne);
      if (spotifyState.durationMillis == 0) {
        drawCenteredText(elapsed, textWidth);
      } else {
        char duration[11];
        formatMillis(duration, spotifyState.durationMillis);
        char status[24];
        sprintf(status, "%s / %s", elapsed, duration);
        drawCenteredText(status, textWidth);
      }
    } else {
      char label[13];
      sprintf(label, "%d / %d", menuIndex + 1, menuSize);
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(label, textWidth);
    }

    if (menuMode == BookmarksList) {
      if (bookmarksCount == 0) {
        img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      } else {
        int genreIndex = getGenreIndexByName(text);
        if (genreIndex >= 0) img.setTextColor(genreColors[genreIndex], TFT_BLACK);
      }
    } else if (menuMode == CountryList) {
      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    } else {
      img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    }

    tft.setCursor(textPadding, lineTwo);
    if (isActivePlaylist && spotifyState.isPlaying && spotifyState.name[0] != '\0') {
      char playing[201];
      snprintf(playing, sizeof(playing) - 1, "%s – %s", spotifyState.artistName, spotifyState.name);
      drawCenteredText(playing, textWidth, 3);
    } else {
      drawCenteredText(text, textWidth, 3);
    }
  }
  lastDisplayMillis = millis();
}

void eventsSendLog(const char *logData, EventsLogTypes type = log_line) {
  if (!sendLogEvents) return;
  events.send(logData, type == log_line ? "line" : "raw");
}

void eventsSendInfo(const char *msg, const char *payload = "") {
  if (!sendLogEvents) return;

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
  if (!sendLogEvents) return;

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
  time_t secondsAsleep = currentSeconds - lastSleepSeconds;
  time_t spotifyTokenAge = currentSeconds - spotifyTokenSeconds;
  bool connected = WiFi.isConnected();

  if (currentMillis - lastInputMillis > inactivityMillis) {
    if (!isGenreMenu(menuMode)) menuMode = AlphabeticList;
    lastSleepSeconds = currentSeconds;
    spotifyAction = Idle;
    tft.fillScreen(TFT_BLACK);
    Serial.printf("\n> [%d] Entering deep sleep.\n", currentMillis);
    eventsSendLog("Entering deep sleep.");
    WiFi.disconnect(true);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN, LOW);
    esp_deep_sleep_start();
  }

  if (!connected && currentMillis - lastReconnectAttemptMillis > 3000) {
    Serial.printf("> [%d] Trying to connect to network %s...\n", currentMillis, ssid);
    WiFi.begin();
    lastReconnectAttemptMillis = currentMillis;
  }

  if (connected && !spotifyGettingToken && (spotifyAccessToken[0] == '\0' || spotifyTokenAge > spotifyTokenLifetime)) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  if (currentMillis < 1000 && (secondsAsleep == 0 || secondsAsleep > 60 * 5)) {
    bool pickRandomGenre = secondsAsleep == 0 || secondsAsleep > (60 * 30);
    startupAnimation(pickRandomGenre);
  }

  if (shouldShowRandom() && extraLongPressedMillis() > extraLongPressMillis) {
    longPressStartedMillis = 0;
    startRandomizingGenres(true);
  }

  if (statusMessage[0] != '\0' && currentMillis > statusMessageUntilMillis) {
    statusMessageUntilMillis = 0;
    statusMessage[0] = '\0';
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else if (spotifyState.isPlaying && (currentMillis - lastDisplayMillis) > 950) {
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else if (shouldShowRandom() && lastDisplayMillis < longPressStartedMillis + extraLongPressMillis * 2) {
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else if (lastInputMillis > lastDisplayMillis) {
    displayInvalidated = true;
  }

  if (displayInvalidated) {
    updateDisplay();
  } else {
    if (spotifyAction != Idle && spotifyAction != CurrentlyPlaying && menuMode != RootMenu) {
      tft.drawFastHLine(-currentMillis % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(currentMillis + 20) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(currentMillis + 40) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(currentMillis + 60) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(currentMillis + 80) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(currentMillis + 100) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(currentMillis + 120) % tft.width(), 0, 20, TFT_BLACK);
    } else {
      tft.drawFastHLine(0, 0, 239, TFT_BLACK);
    }
  }
}

void startupAnimation(bool pickRandomGenre) {
  img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  while (millis() < 1800) {
    tft.fillScreen(TFT_BLACK);
    if (random(10) < 6) {
      tft.setCursor(textPadding, 38);
      drawCenteredText("every noise", textWidth);
    }
    if (random(10) < 5) {
      tft.setCursor(textPadding, 68);
      drawCenteredText("at once", textWidth);
    }
    delay(70 + random(30));
  }
  tft.fillScreen(TFT_BLACK);

  if (pickRandomGenre) {
    menuMode = AlphabeticList;
    menuSize = GENRE_COUNT;
    while (millis() < 2600) {
      static int ticks = 0;
      ticks += 1;
      genreIndex = menuIndex = random(GENRE_COUNT);
      const char *randomGenre = genres[genreIndex];
      uint16_t randomColor = genreColors[genreIndex];
      uint8_t alpha = min(10 + ticks * 10, 255);
      uint16_t fadedColor = tft.alphaBlend(alpha, randomColor, TFT_BLACK);
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(fadedColor, TFT_BLACK);
      drawCenteredText(randomGenre, textWidth, 3);
      delay(pow(ticks, 2));
    }
  }

  displayInvalidated = true;
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
          if (spotifyState.lastUpdateMillis == 0 ||
              (nextCurrentlyPlayingMillis && (cur_millis >= nextCurrentlyPlayingMillis)) ||
              (cur_millis - spotifyState.lastUpdateMillis >= SPOTIFY_POLL_INTERVAL)) {
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
        case PlayPlaylist:
          spotifyPlayPlaylist();
          break;
        case GetDevices:
          spotifyGetDevices();
          displayInvalidated = true;
          displayInvalidatedPartial = true;
          break;
        case SetVolume:
          spotifySetVolume();
          break;
        case ToggleShuffle:
          spotifyToggleShuffle();
          break;
        case TransferPlayback:
          spotifyTransferPlayback();
          break;
      }
    }
    delay(10);
  }
}

void setActiveUser(SpotifyUser_t *user) {
  activeSpotifyUser = user;
  strncpy(spotifyRefreshToken, activeSpotifyUser->refreshToken, sizeof(spotifyRefreshToken) - 1);
  if (user->selectedDeviceId[0] != '\0')
    strncpy(activeSpotifyDeviceId, user->selectedDeviceId, sizeof(activeSpotifyDeviceId) - 1);

  SpotifyDevice_t *device = NULL;
  for (uint8_t i = 0; i < spotifyDevicesCount; i++) {
    if (strcmp(spotifyDevices[i].id, activeSpotifyDeviceId) == 0) {
      device = &spotifyDevices[i];
      break;
    }
  }
  setActiveDevice(device);
}

void setActiveDevice(SpotifyDevice_t *device) {
  activeSpotifyDevice = device;
  if (activeSpotifyDevice == NULL) {
    spotifyDevicesCount = 0;
  } else {
    strncpy(activeSpotifyDeviceId, activeSpotifyDevice->id, sizeof(activeSpotifyDeviceId) - 1);
    strncpy(activeSpotifyUser->selectedDeviceId, activeSpotifyDevice->id, sizeof(activeSpotifyUser->selectedDeviceId) - 1);
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
  usersCount = min((size_t)MAX_SPOTIFY_USERS, usersArray.size());

  for (uint8_t i = 0; i < usersCount; i++) {
    JsonObject jsonUser = usersArray[i];
    const char *name = jsonUser["name"];
    const char *token = jsonUser["token"];
    const char *country = jsonUser["country"];
    const char *selectedDeviceId = jsonUser["selectedDeviceId"];

    SpotifyUser_t *user = &spotifyUsers[i];
    strncpy(user->name, name, sizeof(user->name) - 1);
    strncpy(user->refreshToken, token, sizeof(user->refreshToken) - 1);
    strncpy(user->country, country, sizeof(user->country) - 1);
    strncpy(user->selectedDeviceId, selectedDeviceId, sizeof(user->selectedDeviceId) - 1);

    if (jsonUser["selected"].as<bool>() == true) setActiveUser(user);
  }

  if (usersCount > 0 && activeSpotifyUser == NULL) setActiveUser(&spotifyUsers[0]);

  JsonArray bookmarksArray = doc["bookmarks"];

  for (int i = 0; i < bookmarksArray.size(); i++) {
    const char *genreName = bookmarksArray[i];
    int genreIndex = getGenreIndexByName(genreName);
    if (genreIndex >= 0) addBookmark(genreIndex);
  }

  return true;
}

bool writeDataJson() {
  File f = SPIFFS.open("/data.json", "w+");
  DynamicJsonDocument doc(5000);

  JsonArray usersArray = doc.createNestedArray("users");

  for (uint8_t i = 0; i < usersCount; i++) {
    SpotifyUser_t *user = &spotifyUsers[i];
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

bool isBookmarked(uint16_t genreIndex) {
  for (int i = 0; i < bookmarksCount; i++) {
    if (strcmp(bookmarkedGenres[i], genres[genreIndex]) == 0) {
      return true;
    }
  }
  return false;
}

void addBookmark(uint16_t genreIndex) {
  if (bookmarksCount < MAX_BOOKMARKS) {
    bookmarkedGenres[bookmarksCount++] = genres[genreIndex];
  }
}

void removeBookmark(uint16_t genreIndex) {
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
        delay(1);
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
      strncpy(spotifyAccessToken, json["access_token"], sizeof(spotifyAccessToken) - 1);
      if (spotifyAccessToken[0] != '\0') {
        spotifyTokenLifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval now;
        gettimeofday(&now, NULL);
        spotifyTokenSeconds = now.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          const char *newRefreshToken = json["refresh_token"];
          if (strcmp(newRefreshToken, spotifyRefreshToken) != 0) {
            strncpy(spotifyRefreshToken, newRefreshToken, sizeof(spotifyRefreshToken) - 1);
            bool found = false;
            for (uint8_t i = 0; i < usersCount; i++) {
              SpotifyUser_t *user = &spotifyUsers[i];
              if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) {
                found = true;
                break;
              }
            }
            if (!found && usersCount < MAX_SPOTIFY_USERS) {
              SpotifyUser_t *user = &spotifyUsers[usersCount++];
              strncpy(user->refreshToken, spotifyRefreshToken, sizeof(user->refreshToken) - 1);
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
  if (spotifyAccessToken[0] == '\0' || !activeSpotifyUser) return;
  uint32_t ts = millis();
  nextCurrentlyPlayingMillis = 0;

  char url[18];
  sprintf(url, "/player?market=%s", activeSpotifyUser->country);
  HTTP_response_t response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(7000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      spotifyState.lastUpdateMillis = millis();
      spotifyState.isPlaying = json["is_playing"];
      spotifyState.isShuffled = json["shuffle_state"];
      spotifyState.progressMillis = json["progress_ms"];

      JsonObject context = json["context"];
      if (context.isNull()) {
        spotifyState.playlistId[0] = '\0';
      } else {
        if (strcmp(context["type"], "playlist") == 0) {
          const char *id = strrchr(context["uri"], ':') + 1;
          strncpy(spotifyState.playlistId, id, sizeof(spotifyState.playlistId) - 1);
        }
      }

      JsonObject item = json["item"];
      if (!item.isNull()) {
        spotifyState.durationMillis = item["duration_ms"];
        strncpy(spotifyState.name, item["name"], sizeof(spotifyState.name) - 1);

        JsonObject album = item["album"];
        if (!album.isNull()) {
          strncpy(spotifyState.albumName, album["name"], sizeof(spotifyState.albumName) - 1);
        } else {
          spotifyState.albumName[0] = '\0';
        }

        JsonArray artists = item["artists"];
        if (artists.size() > 0) {
          strncpy(spotifyState.artistName, artists[0]["name"], sizeof(spotifyState.artistName) - 1);
        } else {
          spotifyState.artistName[0] = '\0';
        }
      }

      if (json.containsKey("device")) {
        JsonObject jsonDevice = json["device"];
        const char *playingDeviceId = jsonDevice["id"];
        strncpy(activeSpotifyDeviceId, playingDeviceId, sizeof(activeSpotifyDeviceId) - 1);

        int playingDeviceIndex = -1;
        for (uint8_t i = 0; i < spotifyDevicesCount; i++) {
          if (strcmp(spotifyDevices[i].id, playingDeviceId) == 0) {
            playingDeviceIndex = i;
            break;
          }
        }
        if (playingDeviceIndex == -1) playingDeviceIndex = spotifyDevicesCount = 0;
        activeSpotifyDevice = &spotifyDevices[playingDeviceIndex];

        strncpy(activeSpotifyDevice->id, playingDeviceId, sizeof(activeSpotifyDevice->id) - 1);
        strncpy(activeSpotifyDevice->name, jsonDevice["name"], sizeof(activeSpotifyDevice->name) - 1);
        activeSpotifyDevice->volumePercent = jsonDevice["volume_percent"];
      }

      if (spotifyState.isPlaying) {
        // Check if current song is about to end
        inactivityMillis = 90000;
        uint32_t remainingMillis = spotifyState.durationMillis - spotifyState.progressMillis;
        if (remainingMillis < SPOTIFY_POLL_INTERVAL) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          nextCurrentlyPlayingMillis = millis() + remainingMillis + 100;
        }
      } else if (spotifyAction == CurrentlyPlaying) {
        inactivityMillis = 60000;
        spotifyAction = Idle;
      }
    } else {
      Serial.printf("  [%d] Heap free: %d\n", ts, ESP.getFreeHeap());
      Serial.printf("  [%d] Error %s parsing response:\n  %s\n", ts, error.c_str(), response.payload.c_str());
      eventsSendError(500, error.c_str(), response.payload.c_str());
    }
  } else if (response.httpCode == 204) {
    spotifyAction = Idle;
    spotifyState.isShuffled = false;
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

  HTTP_response_t response = spotifyApiRequest("GET", "");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, response.payload);

    if (!error) {
      const char *displayName = json["display_name"];
      const char *country = json["country"];
      strncpy(activeSpotifyUser->name, displayName, sizeof(activeSpotifyUser->name) - 1);
      strncpy(activeSpotifyUser->country, country, sizeof(activeSpotifyUser->country) - 1);
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
  spotifyResetProgress();
  HTTP_response_t response = spotifyApiRequest("POST", "/player/next");
  if (response.httpCode == 204) {
    spotifyResetProgress();
    spotifyState.isPlaying = true;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyPrevious() {
  spotifyResetProgress();
  HTTP_response_t response = spotifyApiRequest("POST", "/player/previous");
  if (response.httpCode == 204) {
    spotifyResetProgress();
    spotifyState.isPlaying = true;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyToggle() {
  if (spotifyAccessToken[0] == '\0') return;

  bool wasPlaying = spotifyState.isPlaying;
  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[65];
    snprintf(path, sizeof(path), wasPlaying ? "/player/pause?device_id=%s" : "/player/play?device_id=%s",
             activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path);
  } else {
    response = spotifyApiRequest("PUT", wasPlaying ? "/player/pause" : "/player/play");
  }

  if (response.httpCode == 204) {
    spotifyState.isPlaying = !wasPlaying;
    nextCurrentlyPlayingMillis = millis() + 1;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
};

void spotifyPlayPlaylist() {
  if (spotifyAccessToken[0] == '\0' || spotifyPlayPlaylistId[0] == '\0') return;

  spotifyResetProgress();
  char requestContent[59];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"spotify:playlist:%s\"}", spotifyPlayPlaylistId);
  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[64];
    snprintf(path, sizeof(path), "/player/play?device_id=%s", activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path, requestContent);
  } else {
    response = spotifyApiRequest("PUT", "/player/play", requestContent);
  }

  if (response.httpCode == 204) {
    spotifyResetProgress();
    spotifyState.isPlaying = true;
    strncpy(spotifyState.playlistId, spotifyPlayPlaylistId, sizeof(spotifyState.playlistId) - 1);
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
  spotifyPlayPlaylistId = nullptr;
};

void spotifyResetProgress() {
  spotifyState.name[0] = '\0';
  spotifyState.albumName[0] = '\0';
  spotifyState.artistName[0] = '\0';
  spotifyState.durationMillis = 0;
  spotifyState.progressMillis = 0;
  spotifyState.lastUpdateMillis = millis();
  spotifyState.isPlaying = false;
  nextCurrentlyPlayingMillis = millis() + 1;
  displayInvalidated = true;
  displayInvalidatedPartial = true;
};

void spotifyGetDevices() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response = spotifyApiRequest("GET", "/player/devices");
  if (response.httpCode == 200) {
    DynamicJsonDocument doc(4000);
    DeserializationError error = deserializeJson(doc, response.payload);

    if (!error) {
      JsonArray jsonDevices = doc["devices"];
      spotifyDevicesCount = min((size_t)MAX_SPOTIFY_DEVICES, jsonDevices.size());
      if (spotifyDevicesCount == 0) activeSpotifyDeviceId[0] = '\0';
      if (menuMode == DeviceList) menuSize = spotifyDevicesCount;
      for (uint8_t i = 0; i < spotifyDevicesCount; i++) {
        JsonObject jsonDevice = doc["devices"][i];
        const char *id = jsonDevice["id"];
        const char *name = jsonDevice["name"];
        bool isActive = jsonDevice["is_active"];
        uint8_t volume_percent = jsonDevice["volume_percent"];

        SpotifyDevice_t *device = &spotifyDevices[i];
        strncpy(device->id, id, sizeof(device->id) - 1);
        strncpy(device->name, name, sizeof(device->name) - 1);
        device->volumePercent = volume_percent;
        if (isActive || strcmp(activeSpotifyDeviceId, id) == 0) {
          activeSpotifyDevice = device;
          strncpy(activeSpotifyDeviceId, id, sizeof(activeSpotifyDeviceId) - 1);
        }
      }
    }
  }
  spotifyAction = CurrentlyPlaying;
}

void spotifySetVolume() {
  if (activeSpotifyDevice == NULL) return;
  char path[85];
  snprintf(path, sizeof(path), "/player/volume?device_id=%s&volume_percent=%d", activeSpotifyDevice->id,
           activeSpotifyDevice->volumePercent);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", path);
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
}

void spotifyToggleShuffle() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyState.isShuffled = !spotifyState.isShuffled;
  HTTP_response_t response;

  char path[30];
  snprintf(path, sizeof(path), "/player/shuffle?state=%s", spotifyState.isShuffled ? "true" : "false");
  response = spotifyApiRequest("PUT", path);

  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + 1;
  } else {
    spotifyState.isShuffled = !spotifyState.isShuffled;
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
};

void spotifyTransferPlayback() {
  if (activeSpotifyDevice == NULL) return;
  char requestContent[61];
  snprintf(requestContent, sizeof(requestContent), "{\"device_ids\":[\"%s\"]}", activeSpotifyDeviceId);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", "/player", requestContent);
  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + 1;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};
