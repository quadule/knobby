#include "main.h"

#define FONT_NAME "GillSans24"
#define ICON_SIZE 24
#define LINE_HEIGHT 27

const String ICON_VOLUME_UP = "\uE900";
const String ICON_VOLUME_OFF = "\uE901";
const String ICON_VOLUME_MUTE = "\uE902";
const String ICON_VOLUME_DOWN = "\uE903";
const String ICON_AUDIOTRACK = "\uE904";
const String ICON_LIBRARY_MUSIC = "\uE905";
const String ICON_FAVORITE = "\uE906";
const String ICON_FAVORITE_OUTLINE = "\uE907";
const String ICON_SHUFFLE_ON = "\uE908";
const String ICON_SHUFFLE = "\uE909";
const String ICON_SKIP_PREVIOUS = "\uE90A";
const String ICON_SKIP_NEXT = "\uE90B";
const String ICON_PLAY_ARROW = "\uE90C";
const String ICON_PAUSE = "\uE90D";
const String ICON_WIFI = "\uE90E";
const String ICON_SPOTIFY = "\uEA94";

TFT_eSPI tft = TFT_eSPI(135, 240);
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite ico = TFT_eSprite(&tft);

const int centerX = 120;
const int lineOne = 10;
const int lineTwo = lineOne + LINE_HEIGHT + 9;
const int lineThree = lineTwo + LINE_HEIGHT;
const int lineSpacing = 3;
const int textPadding = 10;
const int textWidth = 239 - textPadding * 2;
#define TFT_LIGHTBLACK       0x1082      /*  16,   16,  16 */

#define ADC_EN                    14  //ADC_EN is the ADC detection enable port
#define ADC_PIN                   34
#define LED_PIN                    9 // onboard blue LED
#define ROTARY_ENCODER_A_PIN      12
#define ROTARY_ENCODER_B_PIN      13
#define ROTARY_ENCODER_BUTTON_PIN 15
ESPRotary knob(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, 4);
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

#define startsWith(STR, SEARCH) (strncmp(STR, SEARCH, strlen(SEARCH)) == 0)

enum EventsLogTypes { log_line, log_raw };

AsyncWebServer server(80);
AsyncEventSource events("/events");

int vref = 1100;
float batteryVoltage = 0.0;
String nodeName = "knobby";
RTC_DATA_ATTR unsigned int bootCount = 0;
RTC_DATA_ATTR time_t bootSeconds = 0;
bool sendLogEvents = false;
RTC_DATA_ATTR uint16_t genreIndex = 0;
RTC_DATA_ATTR time_t lastSleepSeconds = 0;
time_t secondsAsleep = 0;
unsigned long clickEffectMillis = 30;
unsigned long clickEffectEndMillis = 0;
unsigned long inactivityMillis = 60000;
const unsigned int batteryReportIntervalMillis = 30000;
unsigned long lastBatteryReportMillis = 0;
unsigned long lastBatteryUpdateMillis = 0;
unsigned long lastInputMillis = 1;
unsigned long lastDisplayMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;
unsigned long randomizingMenuEndMillis = 0;
unsigned long randomizingMenuTicks = 0;
bool inputLocked = false;
bool randomizingMenuAutoplay = false;
bool displayInvalidated = true;
bool displayInvalidatedPartial = false;
char statusMessage[24] = "";
unsigned long statusMessageUntilMillis = 0;

enum MenuModes {
  VolumeControl = -2,
  RootMenu = -1,
  UserList = 0,
  DeviceList = 1,
  PlaylistsList = 2,
  CountryList = 3,
  AlphabeticList = 4,
  AlphabeticSuffixList = 5,
  PopularityList = 6,
  SimilarList = 7,
  NowPlaying = 8
};
RTC_DATA_ATTR MenuModes menuMode = AlphabeticList;
MenuModes lastMenuMode = AlphabeticList;
MenuModes lastPlaylistMenuMode = AlphabeticList;
MenuModes lastFullGenreMenuMode = AlphabeticList;
uint16_t lastMenuIndex = 0;
const char *rootMenuItems[] = {"users",       "devices",    "playlists", "countries",        "name",
                               "name ending", "popularity", "similar",   "now playing"};

enum NowPlayingButtons {
  VolumeButton = 0,
  ShuffleButton = 1,
  BackButton = 2,
  PlayPauseButton = 3,
  NextButton = 4
};

typedef struct {
  char playlistId[SPOTIFY_ID_SIZE + 1];
  char name[18];
} SimilarItem_t;
std::vector<SimilarItem_t> similarMenuItems;
int playingGenreIndex = -1;
int similarMenuGenreIndex = -1;

RTC_DATA_ATTR uint16_t menuSize = GENRE_COUNT;
RTC_DATA_ATTR uint16_t menuIndex = 0;
int lastKnobPosition = 0;
float lastKnobSpeed = 0.0;
const unsigned long extraLongPressMillis = 1250;
unsigned long longPressStartedMillis = 0;
bool knobRotatedWhileLongPressed = false;
int rootMenuSimilarIndex = -1;
int rootMenuNowPlayingIndex = -1;

TaskHandle_t backgroundApiTask;

void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1) {
  const uint16_t lineHeight = img.gFont.yAdvance + lineSpacing;
  // const uint16_t spriteHeight = img.gFont.yAdvance * maxLines + lineSpacing * (maxLines - 1);
  const uint16_t centerX = round(maxWidth / 2.0);
  const uint16_t len = strlen(text);

  uint16_t lineNumber = 0;
  uint16_t pos = 0;
  int16_t totalWidth = 0;
  uint16_t index = 0;
  uint16_t preferredBreakpoint = 0;
  uint16_t widthAtBreakpoint = 0;
  bool breakpointOnSpace = false;
  uint16_t lastDrawnPos = 0;

  img.createSprite(maxWidth, img.gFont.yAdvance);

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
    } else if (unicode == '-' || unicode == '/') {
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

      img.setCursor(centerX - round(lineWidth / 2.0), 0);
      img.printToSprite(line, lineLength);
      img.pushSprite(tft.getCursorX(), tft.getCursorY());
      tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);

      lastDrawnPos = preferredBreakpoint;
      // It is possible that we did not draw all letters to n so we need
      // to account for the width of the chars from `n - preferredBreakpoint`
      // by calculating the width we did not draw yet.
      totalWidth = totalWidth - widthAtBreakpoint;
      if (breakpointOnSpace) totalWidth -= img.gFont.spaceWidth + 1;
      preferredBreakpoint = 0;
      lineNumber++;
      if (lineNumber >= maxLines) break;
      img.fillSprite(TFT_BLACK);
    }
  }

  // Draw last part if needed
  if (lastDrawnPos < len && lineNumber < maxLines) {
    uint16_t lineLength = len - lastDrawnPos;
    char line[lineLength + 1] = {0};
    strncpy(line, &text[lastDrawnPos], lineLength);
    img.setCursor(centerX - round(totalWidth / 2.0), 0);
    img.printToSprite(line, lineLength);
    img.pushSprite(tft.getCursorX(), tft.getCursorY());
    tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);
    lineNumber++;
  }

  img.deleteSprite();

  while (lineNumber < maxLines) {
    tft.fillRect(tft.getCursorX(), tft.getCursorY(), maxWidth, lineHeight, TFT_BLACK);
    tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);
    lineNumber++;
  }
}

bool spotifyNeedsNewAccessToken() {
  if (spotifyAccessToken[0] == '\0') return true;
  struct timeval tod;
  gettimeofday(&tod, NULL);
  time_t currentSeconds = tod.tv_sec;
  time_t spotifyTokenAge = spotifyTokenSeconds == 0 ? 0 : currentSeconds - spotifyTokenSeconds;
  return spotifyTokenAge > spotifyTokenLifetime;
}

void updateBatteryVoltage() {
  /*
  ADC_EN is the ADC detection enable port
  If the USB port is used for power supply, it is turned on by default.
  If it is powered by battery, it needs to be set to high level
  */
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);
  delay(10);
  batteryVoltage = ((float)analogRead(ADC_PIN) / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
  digitalWrite(ADC_EN, LOW);
  lastBatteryUpdateMillis = millis();
}

int getGenreIndexByName(const char *genreName) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    if (strcmp(genreName, genres[i]) == 0) return i;
  }
  return -1;
}

int getGenreIndexByPlaylistId(const char *playlistId) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    int result = strncmp(playlistId, genrePlaylists[i], SPOTIFY_ID_SIZE);
    if (result == 0) return i;
  }
  return -1;
}

bool isGenreMenu(MenuModes mode) {
  return mode == AlphabeticList || mode == AlphabeticSuffixList || mode == PopularityList || mode == SimilarList;
}

bool isPlaylistMenu(MenuModes mode) {
  return isGenreMenu(mode) || mode == PlaylistsList || mode == CountryList;
}

unsigned long longPressedMillis() {
  return longPressStartedMillis == 0 ? 0 : millis() - longPressStartedMillis;
}

unsigned long extraLongPressedMillis() {
  long extraMillis = (long)longPressedMillis() - extraLongPressMillis;
  return extraMillis < 0 ? 0 : extraMillis;
}

bool shouldShowRandom() {
  if (randomizingMenuEndMillis > 0 || knobRotatedWhileLongPressed) return false;
  return longPressedMillis() > extraLongPressMillis && lastMenuMode != SimilarList &&
         (lastMenuMode == NowPlaying || isGenreMenu(lastMenuMode));
}

bool shouldShowSimilarMenu() {
  return lastMenuMode == SimilarList || isGenreMenu(lastMenuMode);
}

bool shouldShowNowPlaying() { return spotifyState.isPlaying || spotifyState.name[0] != '\0'; }

uint16_t getMenuSize(MenuModes mode) {
  int nextDynamicIndex = SimilarList;

  switch (mode) {
    case RootMenu:
      rootMenuSimilarIndex = shouldShowSimilarMenu() ? nextDynamicIndex++ : -1;
      rootMenuNowPlayingIndex = shouldShowNowPlaying() ? nextDynamicIndex++ : -1;
      return nextDynamicIndex;
    case UserList:
      return spotifyUsers.size();
    case DeviceList:
      return spotifyDevices.size();
    case PlaylistsList:
      return spotifyPlaylists.size();
    case CountryList:
      return COUNTRY_COUNT;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      return GENRE_COUNT;
    case SimilarList:
      return similarMenuItems.size();
    case VolumeControl:
      return 101; // 0-100%
    case NowPlaying:
      return 5; // volume, shuffle, previous, play/pause, next
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
    case PlaylistsList:
      return max(0, getGenreIndexByPlaylistId(spotifyPlaylists[index].id));
    case SimilarList:
      return max(0, getGenreIndexByPlaylistId(similarMenuItems[index].playlistId));
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

uint16_t getMenuIndexForGenreIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case AlphabeticList:
      return index;
    case AlphabeticSuffixList:
      return getIndexOfGenreIndex(genreIndexes_suffix, index);
    case PopularityList:
      return getIndexOfGenreIndex(genreIndexes_popularity, index);
    default:
      return 0;
  }
}

void setMenuIndex(uint16_t newMenuIndex) {
  menuIndex = menuSize == 0 ? 0 : newMenuIndex % menuSize;

  int newGenreIndex = -1;
  switch (menuMode) {
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      if (menuSize > 0) genreIndex = getGenreIndexForMenuIndex(menuIndex, menuMode);
      break;
    case SimilarList:
      if (menuSize > 0) {
        if (similarMenuItems[menuIndex].name[0] == '\0') {
          newGenreIndex = getGenreIndexByPlaylistId(similarMenuItems[menuIndex].playlistId);
          if (newGenreIndex < 0) {
            genreIndex = similarMenuGenreIndex;
          } else {
            genreIndex = newGenreIndex;
          }
        } else {
          genreIndex = max(0, similarMenuGenreIndex);
        }
      }
      break;
    default:
      break;
  }
  displayInvalidated = true;
  displayInvalidatedPartial = true;
}

void setMenuMode(MenuModes newMode, uint16_t newMenuIndex) {
  MenuModes oldMode = menuMode;
  menuMode = newMode;
  menuSize = getMenuSize(newMode);
  setMenuIndex(newMenuIndex);
  displayInvalidatedPartial = oldMode == newMode;
}

void setStatusMessage(const char *message, unsigned long durationMs = 1300) {
  strncpy(statusMessage, message, sizeof(statusMessage) - 1);
  statusMessageUntilMillis = millis() + durationMs;
  displayInvalidated = true;
  displayInvalidatedPartial = true;
}

void startRandomizingMenu(bool autoplay = false) {
  unsigned long now = millis();
  if (now > randomizingMenuEndMillis) {
    inputLocked = true;
    randomizingMenuEndMillis = now + 1500;
    randomizingMenuTicks = 0;
    randomizingMenuAutoplay = autoplay;
    setMenuMode(lastFullGenreMenuMode, 0);
  }
}

void playPlaylist(const char *playlistId) {
  spotifyResetProgress();
  spotifyPlayPlaylistId = playlistId;
  strncpy(spotifyState.playlistId, playlistId, SPOTIFY_ID_SIZE);
  if (spotifyAction != GetToken) spotifyAction = PlayPlaylist;
  setStatusMessage("play");
  setMenuMode(NowPlaying, PlayPauseButton);
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
    if (steps >= 2) steps /= 2;
    steps = min(steps, 10);
    int newMenuIndex = ((int)menuIndex + (positionDelta * steps));
    newMenuIndex = newMenuIndex < 0 ? 0 : min(newMenuIndex, menuSize - 1);
    setMenuIndex(newMenuIndex);
    spotifyAction = SetVolume;
    spotifySetVolumeTo = menuIndex;
    spotifySetVolumeAtMillis = now + 100;
  } else {
    int newMenuIndex = ((int)menuIndex + (positionDelta * steps)) % (int)menuSize;
    if (newMenuIndex < 0) newMenuIndex += menuSize;
    setMenuIndex(newMenuIndex);
  }
}

void knobClicked() {
  lastInputMillis = millis();
  clickEffectEndMillis = lastInputMillis + clickEffectMillis;

  switch (menuMode) {
    case UserList:
      if (!spotifyUsers.empty()) {
        spotifyAction = Idle;
        spotifyTokenLifetime = 0;
        spotifyTokenSeconds = 0;
        spotifyAccessToken[0] = '\0';
        spotifyDevices.clear();
        setActiveUser(&spotifyUsers[menuIndex]);
        writeDataJson();
        displayInvalidated = true;
      }
      break;
    case DeviceList:
      if (!spotifyDevices.empty()) {
        SpotifyDevice_t *previousDevice = activeSpotifyDevice;
        setActiveDevice(&spotifyDevices[menuIndex]);
        writeDataJson();
        if (spotifyState.isPlaying && previousDevice != nullptr && previousDevice != &spotifyDevices[menuIndex]) {
          spotifyAction = TransferPlayback;
        }
      }
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      playPlaylist(genrePlaylists[genreIndex]);
      playingGenreIndex = genreIndex;
      break;
    case SimilarList:
      if (!similarMenuItems.empty()) {
        playPlaylist(similarMenuItems[menuIndex].playlistId);
        playingGenreIndex = similarMenuGenreIndex;
      }
      break;
    case CountryList:
      playPlaylist(countryPlaylists[menuIndex]);
      break;
    case PlaylistsList:
      playPlaylist(spotifyPlaylists[menuIndex].id);
      break;
    case VolumeControl:
      spotifySetVolumeAtMillis = millis();
      setMenuMode(NowPlaying, VolumeButton);
      break;
    case NowPlaying:
      switch (menuIndex) {
        case VolumeButton:
          if (activeSpotifyDevice != nullptr) {
            setMenuMode(VolumeControl, activeSpotifyDevice->volumePercent);
          }
          break;
        case ShuffleButton:
          if (!spotifyState.disallowsTogglingShuffle) {
            spotifyAction = ToggleShuffle;
          }
          break;
        case BackButton:
          if (spotifyState.disallowsSkippingPrev || spotifyState.estimatedProgressMillis > 1000) {
            spotifySeekToMillis = 0;
            spotifyAction = Seek;
          } else {
            spotifyAction = Previous;
          }
          break;
        case PlayPauseButton:
          spotifyAction = Toggle;
          break;
        case NextButton:
          if (!spotifyState.disallowsSkippingNext) {
            spotifyAction = Next;
          }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (spotifyNeedsNewAccessToken()) return;
  spotifyAction = Next;
  setStatusMessage("next");
}

void knobLongPressStarted() {
  longPressStartedMillis = lastInputMillis = millis();
  knobRotatedWhileLongPressed = false;
  if (menuMode != RootMenu) {
    lastMenuMode = menuMode;
    lastMenuIndex = menuIndex;
    if (isPlaylistMenu(menuMode)) lastPlaylistMenuMode = menuMode;
    if (isGenreMenu(menuMode) && menuMode != SimilarList) lastFullGenreMenuMode = menuMode;
  }
  getMenuSize(RootMenu);
  if (menuMode == NowPlaying) {
    switch (lastPlaylistMenuMode) {
      case SimilarList:
        setMenuMode(RootMenu, rootMenuSimilarIndex);
        break;
      case PlaylistsList:
      case CountryList:
      case AlphabeticList:
      case AlphabeticSuffixList:
      case PopularityList:
        setMenuMode(RootMenu, (uint16_t)lastPlaylistMenuMode);
        break;
      default:
        setMenuMode(RootMenu, (uint16_t)menuMode);
        break;
    }
  } else if (menuMode == SimilarList) {
    setMenuMode(RootMenu, rootMenuSimilarIndex);
  } else if (spotifyState.isPlaying) {
    setMenuMode(RootMenu, rootMenuNowPlayingIndex);
  } else if (menuMode == VolumeControl) {
    spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
    setMenuMode(RootMenu, rootMenuNowPlayingIndex);
  } else {
    setMenuMode(RootMenu, (uint16_t)menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  getMenuSize(RootMenu);
  if (shouldShowRandom()) {
    startRandomizingMenu();
  } else if (menuIndex == rootMenuSimilarIndex) {
    if (similarMenuGenreIndex == genreIndex) {
      setMenuMode(SimilarList, lastMenuMode == SimilarList ? lastMenuIndex : 0);
    } else {
      similarMenuItems.clear();
      similarMenuGenreIndex = genreIndex;
      setMenuMode(SimilarList, 0);
      spotifyAction = GetPlaylistDescription;
    }
  } else if (menuIndex == rootMenuNowPlayingIndex) {
    nextCurrentlyPlayingMillis = lastInputMillis;
    setMenuMode(NowPlaying, PlayPauseButton);
  } else {
    uint16_t newMenuIndex = lastMenuIndex;

    switch (menuIndex) {
      case UserList:
        if (spotifyUsers.empty()) {
          Serial.printf("No users found! Visit http://%s.local to sign in.\n", nodeName.c_str());
          setMenuMode(lastMenuMode, lastMenuIndex);
        } else {
          newMenuIndex = 0;
          auto usersCount = spotifyUsers.size();
          for (auto i = 1; i < usersCount; i++) {
            if (strcmp(spotifyUsers[i].refreshToken, spotifyRefreshToken) == 0) {
              newMenuIndex = i;
              break;
            }
          }
          if (spotifyUsers[newMenuIndex].name[0] == '\0') spotifyAction = CurrentProfile;
        }
        break;
      case DeviceList:
        newMenuIndex = 0;
        spotifyAction = GetDevices;
        if (spotifyDevicesLoaded && activeSpotifyDeviceId[0] != '\0') {
          auto devicesCount = spotifyDevices.size();
          for (auto i = 0; i < devicesCount; i++) {
            if (strcmp(spotifyDevices[i].id, activeSpotifyDeviceId) == 0) {
              newMenuIndex = i;
              break;
            }
          }
        }
        break;
      case PlaylistsList:
        if (!spotifyPlaylistsLoaded) spotifyAction = GetPlaylists;
        newMenuIndex = lastMenuMode == PlaylistsList ? lastMenuIndex : 0;
        break;
      case CountryList:
        newMenuIndex = lastMenuMode == CountryList ? lastMenuIndex : random(COUNTRY_COUNT);
        break;
      case AlphabeticList:
      case AlphabeticSuffixList:
      case PopularityList:
        newMenuIndex = getMenuIndexForGenreIndex(genreIndex, (MenuModes)menuIndex);
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)menuIndex, newMenuIndex);
  }
  longPressStartedMillis = 0;
}

int formatMillis(char *output, unsigned long millis) {
  unsigned int seconds = millis / 1000 % 60;
  unsigned int minutes = millis / (1000 * 60) % 60;
  unsigned int hours = millis / (1000 * 60 * 60);

  if (hours == 0) {
    return sprintf(output, "%d:%02d", minutes, seconds);
  } else {
    return sprintf(output, "%d:%02d:%02d", hours, minutes, seconds);
  }
}

void updateDisplay() {
  displayInvalidated = false;
  if (!displayInvalidatedPartial) tft.fillScreen(TFT_BLACK);
  displayInvalidatedPartial = false;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  unsigned long now = millis();

  if (spotifyUsers.empty()) {
    const char *hostname = (nodeName + ".local").c_str();
    tft.setCursor(textPadding, lineTwo);
    drawCenteredText("setup at http://", textWidth);
    tft.setCursor(textPadding, lineThree);
    drawCenteredText(hostname, textWidth);
  } else if (now < randomizingMenuEndMillis) {
    randomizingMenuTicks += 1;
    setMenuIndex(random(getMenuSize(lastFullGenreMenuMode)));
    img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    tft.setCursor(textPadding, lineTwo);
    drawCenteredText(genres[genreIndex], textWidth, 3);
    delay(pow(randomizingMenuTicks, 2));
    if (millis() >= randomizingMenuEndMillis) {
      inputLocked = false;
      randomizingMenuEndMillis = 0;
      button.reset();
      if (randomizingMenuAutoplay) {
        playPlaylist(genrePlaylists[genreIndex]);
        playingGenreIndex = genreIndex;
      }
    } else {
      displayInvalidated = true;
      displayInvalidatedPartial = randomizingMenuTicks > 1;
    }
  } else if (menuMode == RootMenu) {
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    if (shouldShowRandom()) {
      double pressedProgress = min(1.0, (double)extraLongPressedMillis() / (double)extraLongPressMillis);
      tft.drawFastHLine(0, 0, (int)(pressedProgress * 239), TFT_WHITE);
      drawCenteredText("random", textWidth);
      delay(10);
    } else if (menuIndex == rootMenuSimilarIndex) {
      drawCenteredText(rootMenuItems[SimilarList], textWidth);
    } else if (menuIndex == rootMenuNowPlayingIndex) {
      drawCenteredText(rootMenuItems[NowPlaying], textWidth);
    } else {
      drawCenteredText(rootMenuItems[menuIndex], textWidth);
    }
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(textPadding, lineThree + LINE_HEIGHT);
    if (batteryVoltage > 0 && batteryVoltage < 3.75) {
      String voltage = String(batteryVoltage) + "V";
      drawCenteredText(voltage.c_str(), textWidth);
    }
    tft.drawRoundRect(9, lineTwo - 15, 221, 49, 5, TFT_WHITE);
  } else if (menuMode == UserList) {
    char header[14];
    sprintf(header, "%d / %d", menuIndex + 1, menuSize);
    tft.setCursor(textPadding, lineOne);
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    drawCenteredText(header, textWidth);
    tft.drawFastHLine(centerX - 40, lineOne + LINE_HEIGHT, 80, TFT_LIGHTBLACK);

    SpotifyUser_t *user = &spotifyUsers[menuIndex];
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
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
    if (spotifyDevices.empty()) {
      tft.setCursor(textPadding, lineTwo);
      drawCenteredText("loading...", textWidth);
    } else {
      char header[14];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(header, textWidth);
      tft.drawFastHLine(centerX - 40, lineOne + LINE_HEIGHT, 80, TFT_LIGHTBLACK);

      SpotifyDevice_t *device = &spotifyDevices[menuIndex];
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
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
    char label[7];
    sprintf(label, "%d%%", menuIndex);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    img.drawString(label, width / 2 - img.textWidth(label) / 2, 48);
    tft.drawRect(0, 0, x - 1, y - 1, TFT_BLACK);
    img.pushSprite(x, y);
    img.deleteSprite();
  } else if (menuMode == NowPlaying) {
    tft.setTextDatum(ML_DATUM);
    tft.setCursor(textPadding, lineOne);
    img.createSprite(90, img.gFont.yAdvance);
    if (now < statusMessageUntilMillis && statusMessage[0] != '\0') {
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      img.printToSprite(String(statusMessage));
    } else {
      char elapsed[11];
      formatMillis(elapsed, spotifyState.estimatedProgressMillis);
      img.setTextColor(spotifyState.isPlaying ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
      if (!spotifyState.isPlaying || spotifyState.durationMillis == 0) {
        img.printToSprite(String(elapsed));
      } else {
        auto remainingMillis = spotifyState.durationMillis - spotifyState.estimatedProgressMillis;
        if (remainingMillis > 0 && remainingMillis < (99 * 60 * 60 * 1000)) {
          char remaining[11];
          formatMillis(remaining, remainingMillis);
          char status[12];
          sprintf(status, "-%s", remaining);
          img.printToSprite(String(status));
        }
      }
    }
    img.pushSprite(tft.getCursorX(), tft.getCursorY());
    img.deleteSprite();

    ico.setTextDatum(MC_DATUM);
    const int width = ICON_SIZE + 2;
    const int height = ICON_SIZE;
    ico.createSprite(width, height);

    const uint16_t bg = TFT_BLACK;
    const uint16_t fg = TFT_LIGHTGREY;
    const uint16_t disabled = TFT_DARKGREY;

    bool nextClicked = menuIndex == NextButton && spotifyAction == Next && now < clickEffectEndMillis;
    tft.setCursor(TFT_HEIGHT - textPadding - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, nextClicked ? fg : bg);
    if (nextClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsSkippingNext ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SKIP_NEXT);
    if (menuIndex == NextButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    bool toggleClicked = menuIndex == PlayPauseButton && spotifyAction == Toggle && now < clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, toggleClicked ? fg : bg);
    if (toggleClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(spotifyState.isPlaying ? ICON_PAUSE : ICON_PLAY_ARROW);
    if (menuIndex == PlayPauseButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    bool backClicked = menuIndex == BackButton && (spotifyAction == Previous || (spotifyAction == Seek && spotifySeekToMillis == 0)) && now < clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, backClicked ? fg : bg);
    if (backClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsSkippingPrev ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SKIP_PREVIOUS);
    if (menuIndex == BackButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(2, 2, width - 4, height - 4, 3, spotifyState.isShuffled ? fg : bg);
    if (spotifyState.isShuffled) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsTogglingShuffle ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SHUFFLE);
    ico.drawRoundRect(0, 0, width, height, 3, menuIndex == ShuffleButton ? fg : bg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(2, 2, width - 4, height - 4, 3, bg);
    ico.setTextColor(activeSpotifyDevice == nullptr ? disabled : fg, bg);
    ico.setCursor(1, 3);
    String volumeIcon;
    if (activeSpotifyDevice != nullptr) {
      if (activeSpotifyDevice->volumePercent > 50) {
        volumeIcon = ICON_VOLUME_UP;
      } else if (activeSpotifyDevice->volumePercent >= 10) {
        volumeIcon = ICON_VOLUME_DOWN;
      } else {
        volumeIcon = ICON_VOLUME_MUTE;
      }
    } else {
      volumeIcon = ICON_VOLUME_MUTE;
    }
    ico.printToSprite(volumeIcon);
    ico.drawRoundRect(0, 0, width, height, 3, menuIndex == VolumeButton ? fg : bg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    ico.deleteSprite();
    tft.drawFastHLine(textPadding, lineOne + LINE_HEIGHT, textWidth, TFT_LIGHTBLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    bool isActivePlaylist =
        strcmp(genrePlaylists[genreIndex], spotifyState.playlistId) == 0 ||
        (spotifyPlayPlaylistId != nullptr && strcmp(genrePlaylists[genreIndex], spotifyPlayPlaylistId) == 0);
    if (isActivePlaylist &&
        (spotifyState.isPlaying || spotifyPlayPlaylistId != nullptr || spotifyAction == Previous || spotifyAction == Next) &&
        (spotifyState.durationMillis == 0 || spotifyState.estimatedProgressMillis % 6000 < 3000)) {
      img.setTextColor(genreColors[genreIndex], TFT_BLACK);
      drawCenteredText(genres[genreIndex], textWidth, 3);
    } else if (spotifyState.artistName[0] != '\0' && spotifyState.name[0] != '\0') {
      char playing[201];
      snprintf(playing, sizeof(playing) - 1, "%s – %s", spotifyState.artistName, spotifyState.name);
      if (playingGenreIndex >= 0) img.setTextColor(genreColors[playingGenreIndex], TFT_BLACK);
      drawCenteredText(playing, textWidth, 3);
    } else {
      // let the old text stay on screen until the api call completes
    }
  } else {
    const char *text;

    if (menuMode == CountryList) {
      text = countries[menuIndex];
    } else if (menuMode == PlaylistsList) {
      if (!spotifyPlaylistsLoaded) {
        text = "loading...";
      } else if (spotifyPlaylists.empty()) {
        text = "no playlists yet";
      } else {
        text = spotifyPlaylists[menuIndex].name;
      }
    } else if (menuMode == SimilarList) {
      if (similarMenuItems.empty()) {
        text = "loading...";
      } else if (similarMenuItems[menuIndex].name[0] != '\0') {
        text = similarMenuItems[menuIndex].name;
      } else {
        text = genres[genreIndex];
      }
    } else {
      text = genres[genreIndex];
    }

    tft.setCursor(textPadding, lineOne);
    if (now < statusMessageUntilMillis && statusMessage[0] != '\0') {
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      drawCenteredText(statusMessage, textWidth);
    } else if (menuSize > 0) {
      char label[14];
      sprintf(label, "%d / %d", menuIndex + 1, menuSize);
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(label, textWidth);
      tft.drawFastHLine(centerX - 40, lineOne + LINE_HEIGHT, 80, TFT_LIGHTBLACK);
    }

    if (menuMode == PlaylistsList) {
      if (!spotifyPlaylistsLoaded) {
        img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      } else {
        img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      }
    } else if (menuMode == CountryList) {
      img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    } else if (menuMode == SimilarList && similarMenuItems[menuIndex].name[0] != '\0') {
      img.setTextColor(genreColors[similarMenuGenreIndex], TFT_BLACK);
    } else if (menuSize == 0) {
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    } else {
      img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    }

    tft.setCursor(textPadding, lineTwo);
    drawCenteredText(text, textWidth, 3);
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
  if (!inputLocked) {
    knob.loop();
    button.tick();
  }

  uint32_t now = millis();
  unsigned long lastInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  struct timeval tod;
  gettimeofday(&tod, NULL);
  time_t currentSeconds = tod.tv_sec;
  bool connected = WiFi.isConnected();

  if (lastInputDelta > inactivityMillis) {
    // don't sleep on transient menus
    if (menuMode == SimilarList || menuMode == PlaylistsList || (!isPlaylistMenu(menuMode) && menuMode != NowPlaying)) setMenuMode(AlphabeticList, genreIndex);
    lastSleepSeconds = currentSeconds;
    spotifyAction = Idle;
    tft.fillScreen(TFT_BLACK);
    eventsSendLog("Entering deep sleep.");
    Serial.printf("> [%d] Entering deep sleep.\n", now);
    WiFi.disconnect(true);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
    rtc_gpio_isolate(GPIO_NUM_0); // button 1
    rtc_gpio_isolate(GPIO_NUM_35); // button 2
    rtc_gpio_isolate(GPIO_NUM_39);
    rtc_gpio_isolate((gpio_num_t)ADC_PIN);
    rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_A_PIN);
    rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_B_PIN);
    adc_power_off();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN, LOW);
    esp_deep_sleep_disable_rom_logging();
    esp_deep_sleep_start();
  }

  if (!connected && now - lastReconnectAttemptMillis > 3000) {
    Serial.printf("> [%d] Trying to connect to network %s...\n", now, ssid);
    WiFi.begin();
    lastReconnectAttemptMillis = now;
  }

  if (connected && !spotifyGettingToken && spotifyNeedsNewAccessToken()) {
    Serial.printf("> [%d] Need access token...\n", now);
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  if (shouldShowRandom() && extraLongPressedMillis() > extraLongPressMillis) {
    longPressStartedMillis = 0;
    startRandomizingMenu(true);
  }

  if (spotifyState.isPlaying) {
    spotifyState.estimatedProgressMillis = spotifyState.progressMillis + (now - spotifyState.lastUpdateMillis);
  }

  if (!displayInvalidated) {
    if (statusMessage[0] != '\0' && now > statusMessageUntilMillis) {
      statusMessageUntilMillis = 0;
      statusMessage[0] = '\0';
      displayInvalidated = true;
      displayInvalidatedPartial = true;
    } else if (clickEffectEndMillis > 0 && lastDisplayMillis > clickEffectEndMillis) {
      clickEffectEndMillis = 0;
      displayInvalidated = true;
      displayInvalidatedPartial = true;
    } else if (lastInputMillis > lastDisplayMillis || (spotifyState.isPlaying && now - lastDisplayMillis > 950) ||
               (shouldShowRandom() && lastDisplayMillis < longPressStartedMillis + extraLongPressMillis * 2)) {
      displayInvalidated = true;
      displayInvalidatedPartial = true;
    }
  }

  if (displayInvalidated) {
    updateDisplay();
  } else {
    if (spotifyAction != Idle && spotifyAction != CurrentlyPlaying && spotifyAction != SetVolume && menuMode != RootMenu) {
      tft.drawFastHLine(-now % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 20) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 40) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 60) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 80) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 100) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 120) % tft.width(), 0, 20, TFT_BLACK);
    } else {
      tft.drawFastHLine(0, 0, 239, TFT_BLACK);
    }
  }

  if (lastInputDelta > 500) delay(10);
}

void setup() {
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_A_PIN);
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_B_PIN);
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN);

  esp_pm_config_esp32_t pm_config_ls_enable = {
    .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
    .min_freq_mhz = 80,
    .light_sleep_enable = true
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
  disableCore1WDT();

  similarMenuItems.reserve(16);
  spotifyDevices.reserve(10);
  spotifyUsers.reserve(10);
  spotifyPlaylists.reserve(100);
  Serial.begin(115200);
  SPIFFS.begin(true);

  Serial.printf("Boot #%d, ", bootCount);
  if (bootCount == 0) {
    genreIndex = menuIndex = random(GENRE_COUNT);
  } else {
    struct timeval tod;
    gettimeofday(&tod, NULL);
    time_t currentSeconds = tod.tv_sec;
    secondsAsleep = currentSeconds - lastSleepSeconds;
    Serial.printf("asleep for %ld seconds, ", secondsAsleep);
    if (spotifyState.isPlaying) {
      if (spotifyState.estimatedProgressMillis + secondsAsleep * 1000 > spotifyState.durationMillis) {
        spotifyResetProgress();
      } else {
        spotifyState.estimatedProgressMillis += secondsAsleep * 1000;
      }
    } else if (secondsAsleep > 60 * 10) {
      spotifyResetProgress();
    }
    nextCurrentlyPlayingMillis = 1;
  }
  bootCount++;

  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
  //Check type of calibration value used to characterize ADC
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
      Serial.printf("eFuse Vref: %u mV\n", adc_chars.vref);
      vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
      Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
      Serial.println("Default Vref: 1100mV");
  }

  knob.setChangedHandler(knobRotated);
  button.setDebounceTicks(30);
  button.setClickTicks(350);
  button.setPressTicks(350);
  button.attachClick(knobClicked);
  button.attachDoubleClick(knobDoubleClicked);
  button.attachLongPressStart(knobLongPressStarted);
  button.attachLongPressStop(knobLongPressStopped);

  tft.init();
  tft.setRotation(3);
  tft.loadFont(FONT_NAME);
  img.loadFont(FONT_NAME);
  ico.loadFont("icomoon24");
  tft.fillScreen(TFT_BLACK);

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
    Serial.printf("> [%d] server.on /\n", ts);
    if (spotifyAccessToken[0] == '\0' && !spotifyGettingToken) {
      request->redirect("http://" + nodeName + ".local/authorize");
    } else {
      request->send(SPIFFS, "/index.html");
    }
  });

  server.on("/authorize", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    Serial.printf("> [%d] server.on /\n", ts);
    spotifyGettingToken = true;
    char authUrl[400];
    snprintf(authUrl, sizeof(authUrl),
             ("https://accounts.spotify.com/authorize/"
              "?response_type=code&scope="
              "user-read-private+user-read-currently-playing+user-read-playback-state+"
              "user-modify-playback-state+playlist-read-private+user-read-recently-played+"
              "user-library-read+user-follow-read+user-follow-modify"
              "&show_dialog=true&redirect_uri=http%%3A%%2F%%2F" +
              nodeName +
              ".local%%2Fcallback%%2F"
              "&client_id=%s")
                 .c_str(),
             SPOTIFY_CLIENT_ID);
    Serial.printf("  [%d] Redirect to: %s\n", ts, authUrl);
    request->redirect(authUrl);
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

  spotifyHttp.setUserAgent("knobby");
  spotifyHttp.setConnectTimeout(4000);
  spotifyHttp.setTimeout(4000);
  spotifyHttp.setReuse(true);

  bool holdingButton = digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW;
  if (holdingButton) {
    delay(50);
    holdingButton = digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW;
  }

  if (spotifyNeedsNewAccessToken()) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  xTaskCreate(backgroundApiLoop,   /* Function to implement the task */
              "backgroundApiLoop", /* Name of the task */
              10000,            /* Stack size in words */
              NULL,             /* Task input parameter */
              1,                /* Priority of the task */
              &backgroundApiTask); /* Task handle. */

  if (holdingButton) {
    startRandomizingMenu(true);
  } else if (secondsAsleep == 0 || secondsAsleep > 60 * 40) {
    uint16_t index = 0;
    while (millis() < 2000) {
      static int ticks = 0;
      ticks += 1;
      index = random(GENRE_COUNT);
      const char *randomGenre = genres[index];
      uint16_t randomColor = genreColors[index];
      uint8_t alpha = min(10 + ticks * 10, 255);
      uint16_t fadedColor = tft.alphaBlend(alpha, randomColor, TFT_BLACK);
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(fadedColor, TFT_BLACK);
      drawCenteredText(randomGenre, textWidth, 3);
      delay(pow(ticks, 2));
    }
    setMenuMode(AlphabeticList, index);
    displayInvalidated = true;
  }
}

void backgroundApiLoop(void *params) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t now = millis();

      switch (spotifyAction) {
        case Idle:
          if (lastBatteryReportMillis == 0 || now - lastBatteryReportMillis > batteryReportIntervalMillis) {
            float oldVoltage = batteryVoltage;
            updateBatteryVoltage();
            if (batteryVoltage != oldVoltage) reportBatteryVoltage();
          }
          break;
        case GetToken:
          if (spotifyAuthCode != "") {
            spotifyGetToken(spotifyAuthCode.c_str(), gt_authorization_code);
          } else if (spotifyRefreshToken[0] != '\0') {
            spotifyGetToken(spotifyRefreshToken, gt_refresh_token);
          }
          break;
        case CurrentlyPlaying:
          if (nextCurrentlyPlayingMillis > 0 && now >= nextCurrentlyPlayingMillis) {
            spotifyCurrentlyPlaying();
          } else if (lastBatteryReportMillis == 0 || now - lastBatteryReportMillis > batteryReportIntervalMillis) {
            float oldVoltage = batteryVoltage;
            updateBatteryVoltage();
            if (batteryVoltage != oldVoltage) reportBatteryVoltage();
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
        case Seek:
          spotifySeek();
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
          if (spotifySetVolumeAtMillis > 0 && millis() >= spotifySetVolumeAtMillis) spotifySetVolume();
          break;
        case ToggleShuffle:
          spotifyToggleShuffle();
          break;
        case TransferPlayback:
          spotifyTransferPlayback();
          break;
        case GetPlaylistDescription:
          spotifyGetPlaylistDescription();
          break;
        case GetPlaylists:
          spotifyGetPlaylists();
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

  SpotifyDevice_t *activeDevice = nullptr;
  for (SpotifyDevice_t device : spotifyDevices) {
    if (strcmp(device.id, activeSpotifyDeviceId) == 0) {
      activeDevice = &device;
      break;
    }
  }
  setActiveDevice(activeDevice);
}

void setActiveDevice(SpotifyDevice_t *device) {
  activeSpotifyDevice = device;
  if (activeSpotifyDevice != nullptr) {
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
  spotifyUsers.clear();
  for (JsonObject jsonUser : usersArray) {
    const char *name = jsonUser["name"];
    const char *token = jsonUser["token"];
    const char *country = jsonUser["country"];
    const char *selectedDeviceId = jsonUser["selectedDeviceId"];

    SpotifyUser_t user;
    strncpy(user.name, name, sizeof(user.name) - 1);
    strncpy(user.refreshToken, token, sizeof(user.refreshToken) - 1);
    strncpy(user.country, country, sizeof(user.country) - 1);
    strncpy(user.selectedDeviceId, selectedDeviceId, sizeof(user.selectedDeviceId) - 1);
    spotifyUsers.push_back(user);

    if (jsonUser["selected"].as<bool>() == true) setActiveUser(&spotifyUsers.back());
  }

  if (activeSpotifyUser == nullptr && !spotifyUsers.empty()) setActiveUser(&spotifyUsers[0]);

  return true;
}

bool writeDataJson() {
  File f = SPIFFS.open("/data.json", "w+");
  DynamicJsonDocument doc(5000);

  JsonArray usersArray = doc.createNestedArray("users");

  for (SpotifyUser_t user : spotifyUsers) {
    JsonObject obj = usersArray.createNestedObject();
    obj["name"] = user.name;
    obj["token"] = user.refreshToken;
    obj["country"] = user.country;
    obj["selectedDeviceId"] = user.selectedDeviceId;
    obj["selected"] = (bool)(strcmp(user.refreshToken, spotifyRefreshToken) == 0);
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

HTTP_response_t httpRequest(const char *host, uint16_t port, const char *headers, const char *content = "") {
  uint32_t ts = millis();
  Serial.printf("> [%d] httpRequest(%s, %d, ...)\n", ts, host, port);

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
      // } else if (totalReadSize > 0 && (millis() - lastAvailableMillis) > 100) {
      //   break;
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
  String path = String("/v1/") + endpoint;
  Serial.printf("> [%d] spotifyApiRequest(%s, %s, %s)\n", ts, method, path.c_str(), content);


  spotifyHttp.begin(client, "api.spotify.com", 443, path);
  spotifyHttp.addHeader("Authorization", "Bearer " + String(spotifyAccessToken));
    if(strlen(content) == 0) spotifyHttp.addHeader("Content-Length", "0");

  StreamString payload;
  int code;
  if (String(method) == "GET") {
    code = spotifyHttp.GET();
  } else {
    code = spotifyHttp.sendRequest(method, content);
  }

  if (code == 401) {
    Serial.println("401 Unauthorized, clearing spotifyAccessToken");
    spotifyAccessToken[0] = '\0';
  } else if (code == 204 || spotifyHttp.getSize() == 0) {
    // Serial.println("empty response, returning");
  } else {
    if (!payload.reserve(spotifyHttp.getSize() + 1)) {
      Serial.printf("not enough memory to reserve a string! need: %d", (spotifyHttp.getSize() + 1));
    }
    spotifyHttp.writeToStream(&payload);
  }
  spotifyHttp.end();

  HTTP_response_t response = {code, payload};
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
  Serial.printf("> [%d] spotifyGetToken(%s, %s)\n", ts, code,
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
    DeserializationError error = deserializeJson(json, response.payload.c_str());

    if (!error) {
      strncpy(spotifyAccessToken, json["access_token"], sizeof(spotifyAccessToken) - 1);
      if (spotifyAccessToken[0] != '\0') {
        spotifyTokenLifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval tod;
        gettimeofday(&tod, NULL);
        spotifyTokenSeconds = tod.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          const char *newRefreshToken = json["refresh_token"];
          if (strcmp(newRefreshToken, spotifyRefreshToken) != 0) {
            strncpy(spotifyRefreshToken, newRefreshToken, sizeof(spotifyRefreshToken) - 1);
            bool found = false;
            for (SpotifyUser_t user : spotifyUsers) {
              if (strcmp(user.refreshToken, spotifyRefreshToken) == 0) {
                found = true;
                break;
              }
            }
            if (!found) {
              SpotifyUser_t user;
              strncpy(user.refreshToken, spotifyRefreshToken, sizeof(user.refreshToken) - 1);
              user.selected = true;
              spotifyUsers.push_back(user);
              setActiveUser(&spotifyUsers.back());
              writeDataJson();
            };
          }
        }
      }
    } else {
      Serial.printf("  [%d] Unable to parse response payload:\n  %s\n", (int)millis(), response.payload.c_str());
      eventsSendError(500, "Unable to parse response payload", response.payload.c_str());
      delay(5000);
    }
  } else if (response.httpCode < 0) {
    // retry immediately
  } else {
    Serial.printf("  [%d] %d - %s\n", (int)millis(), response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
    delay(5000);
  }

  if (success) {
    if (grant_type == gt_authorization_code) {
      spotifyAction = CurrentProfile;
    } else if (spotifyPlayPlaylistId != nullptr) {
      spotifyAction = PlayPlaylist;
    } else {
      spotifyAction = CurrentlyPlaying;
    }
  }
  spotifyGettingToken = false;
}

void spotifyCurrentlyPlaying() {
  nextCurrentlyPlayingMillis = 0;
  if (spotifyAccessToken[0] == '\0' || !activeSpotifyUser) return;
  uint32_t ts = millis();

  char url[21];
  sprintf(url, "me/player?market=%s", activeSpotifyUser->country);
  HTTP_response_t response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(9000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());

    if (!error) {
      spotifyState.lastUpdateMillis = millis();
      spotifyState.isPlaying = json["is_playing"];
      spotifyState.isShuffled = json["shuffle_state"];
      spotifyState.progressMillis = spotifyState.estimatedProgressMillis = json["progress_ms"];

      JsonObject context = json["context"];
      if (context.isNull()) {
        spotifyState.playlistId[0] = '\0';
      } else {
        if (strcmp(context["type"], "playlist") == 0) {
          const char *id = strrchr(context["uri"], ':') + 1;
          strncpy(spotifyState.playlistId, id, SPOTIFY_ID_SIZE);
          playingGenreIndex = getGenreIndexByPlaylistId(id);
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
        auto devicesCount = spotifyDevices.size();
        for (auto i = 0; i < devicesCount; i++) {
          if (strcmp(spotifyDevices[i].id, playingDeviceId) == 0) {
            playingDeviceIndex = i;
            break;
          }
        }
        if (playingDeviceIndex == -1) playingDeviceIndex = 0;
        if (devicesCount == 0) spotifyDevices.push_back({});
        activeSpotifyDevice = &spotifyDevices[playingDeviceIndex];

        strncpy(activeSpotifyDevice->id, playingDeviceId, sizeof(activeSpotifyDevice->id) - 1);
        strncpy(activeSpotifyDevice->name, jsonDevice["name"], sizeof(activeSpotifyDevice->name) - 1);
        activeSpotifyDevice->volumePercent = jsonDevice["volume_percent"];
      }

      spotifyState.disallowsSkippingNext = false;
      spotifyState.disallowsSkippingPrev = false;
      spotifyState.disallowsTogglingShuffle = false;
      JsonObject actions = json["actions"];
      JsonObject disallows = actions["disallows"];
      for (JsonPair disallow : disallows) {
        const char *key = disallow.key().c_str();
        if (strcmp(key, "skipping_next") == 0) {
          spotifyState.disallowsSkippingNext = disallow.value();
        } else if (strcmp(key, "skipping_prev") == 0) {
          spotifyState.disallowsSkippingPrev = disallow.value();
        } else if (strcmp(key, "toggling_shuffle") == 0) {
          spotifyState.disallowsTogglingShuffle = disallow.value();
        }
      }

      if (spotifyState.isPlaying && spotifyState.durationMillis > 0) {
        inactivityMillis = 90000;
        // Check if current song is about to end
        uint32_t remainingMillis = spotifyState.durationMillis - spotifyState.progressMillis;
        if (remainingMillis < SPOTIFY_POLL_INTERVAL) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          nextCurrentlyPlayingMillis = millis() + remainingMillis + 100;
        }
      } else if (spotifyAction == CurrentlyPlaying) {
        inactivityMillis = 60000;
      }
      if (spotifyState.isPlaying && nextCurrentlyPlayingMillis == 0) {
        nextCurrentlyPlayingMillis = millis() + (spotifyState.durationMillis == 0 ? 1000 : SPOTIFY_POLL_INTERVAL);
      }
      if (spotifyState.isPlaying && lastInputMillis <= 1 && menuMode != NowPlaying && millis() < 10000) {
        setMenuMode(NowPlaying, PlayPauseButton);
      } else {
        displayInvalidated = true;
        displayInvalidatedPartial = true;
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
    nextCurrentlyPlayingMillis = 0;
  } else if (response.httpCode < 0 || response.httpCode > 500) {
    nextCurrentlyPlayingMillis = 1; // retry immediately
  } else {
    Serial.printf("  [%d] %d - %s\n", ts, response.httpCode, response.payload.c_str());
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
    delay(5000);
  }
}

/**
 * Get information about the current Spotify user
 */
void spotifyCurrentProfile() {
  if (spotifyAccessToken[0] == '\0') return;
  uint32_t ts = millis();

  HTTP_response_t response = spotifyApiRequest("GET", "me");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());

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
  HTTP_response_t response = spotifyApiRequest("POST", "me/player/next");
  spotifyResetProgress();
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingPrev = false;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyPrevious() {
  HTTP_response_t response = spotifyApiRequest("POST", "me/player/previous");
  spotifyResetProgress();
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingNext = false;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifySeek() {
  if (spotifyAccessToken[0] == '\0' || spotifySeekToMillis < 0) return;
  spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
  char path[40];
  snprintf(path, sizeof(path), "me/player/seek?position_ms=%d", spotifySeekToMillis);
  HTTP_response_t response = spotifyApiRequest("PUT", path);
  if (response.httpCode == 204) {
    spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
    spotifyState.isPlaying = true;
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
    spotifyResetProgress();
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyToggle() {
  if (spotifyAccessToken[0] == '\0') return;

  bool wasPlaying = spotifyState.isPlaying;
  spotifyState.isPlaying = !wasPlaying;

  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[68];
    snprintf(path, sizeof(path), wasPlaying ? "me/player/pause?device_id=%s" : "me/player/play?device_id=%s",
             activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path);
  } else {
    response = spotifyApiRequest("PUT", wasPlaying ? "me/player/pause" : "me/player/play");
  }

  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
    spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
    displayInvalidated = true;
    displayInvalidatedPartial = true;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
    spotifyResetProgress();
    nextCurrentlyPlayingMillis = millis();
    spotifyAction = CurrentlyPlaying;
  }
};

void spotifyPlayPlaylist() {
  if (spotifyAccessToken[0] == '\0' || spotifyPlayPlaylistId == nullptr) return;

  spotifyResetProgress();
  char requestContent[59];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"spotify:playlist:%s\"}", spotifyPlayPlaylistId);
  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[67];
    snprintf(path, sizeof(path), "me/player/play?device_id=%s", activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path, requestContent);
  } else {
    response = spotifyApiRequest("PUT", "me/player/play", requestContent);
  }

  spotifyResetProgress();
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    strncpy(spotifyState.playlistId, spotifyPlayPlaylistId, SPOTIFY_ID_SIZE);
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
  spotifyState.estimatedProgressMillis = 0;
  spotifyState.lastUpdateMillis = millis();
  spotifyState.isPlaying = false;
  nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  displayInvalidated = true;
  displayInvalidatedPartial = true;
};

void spotifyGetDevices() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response = spotifyApiRequest("GET", "me/player/devices");
  if (response.httpCode == 200) {
    DynamicJsonDocument doc(5000);
    DeserializationError error = deserializeJson(doc, response.payload.c_str());

    if (!error) {
      JsonArray jsonDevices = doc["devices"];
      unsigned int devicesCount = jsonDevices.size();
      int activeDeviceIndex = -1;

      if (devicesCount == 0) activeSpotifyDeviceId[0] = '\0';
      spotifyDevices.clear();
      for (JsonObject jsonDevice : jsonDevices) {
        const char *id = jsonDevice["id"];
        const char *name = jsonDevice["name"];
        bool isActive = jsonDevice["is_active"];
        uint8_t volume_percent = jsonDevice["volume_percent"];

        SpotifyDevice_t device;
        strncpy(device.id, id, sizeof(device.id) - 1);
        strncpy(device.name, name, sizeof(device.name) - 1);
        device.volumePercent = volume_percent;
        spotifyDevices.push_back(device);

        if (isActive || strcmp(activeSpotifyDeviceId, id) == 0) {
          activeDeviceIndex = spotifyDevices.size() - 1;
          activeSpotifyDevice = &spotifyDevices.back();
          strncpy(activeSpotifyDeviceId, id, sizeof(activeSpotifyDeviceId) - 1);
        }
      }
      spotifyDevicesLoaded = true;
      if (menuMode == DeviceList) {
        setMenuMode(DeviceList, activeDeviceIndex < 0 ? 0 : activeDeviceIndex);
      }
    }
  }
  spotifyAction = CurrentlyPlaying;
}

void spotifySetVolume() {
  spotifySetVolumeAtMillis = -1;
  if (activeSpotifyDevice == nullptr) return;
  int setpoint = spotifySetVolumeTo;
  char path[74];
  snprintf(path, sizeof(path), "me/player/volume?volume_percent=%d", setpoint);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", path);
  if (activeSpotifyDevice != nullptr) activeSpotifyDevice->volumePercent = setpoint;
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
}

void spotifyToggleShuffle() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyState.isShuffled = !spotifyState.isShuffled;
  HTTP_response_t response;

  char path[33];
  snprintf(path, sizeof(path), "me/player/shuffle?state=%s", spotifyState.isShuffled ? "true" : "false");
  response = spotifyApiRequest("PUT", path);

  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    spotifyState.isShuffled = !spotifyState.isShuffled;
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
};

void spotifyTransferPlayback() {
  if (activeSpotifyDevice == nullptr) return;
  char requestContent[61];
  snprintf(requestContent, sizeof(requestContent), "{\"device_ids\":[\"%s\"]}", activeSpotifyDeviceId);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", "me/player", requestContent);
  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyGetPlaylistDescription() {
  if (spotifyAccessToken[0] == '\0') return;
  const char *activePlaylistId = genrePlaylists[genreIndex];
  HTTP_response_t response;
  char url[53];
  snprintf(url, sizeof(url), "playlists/%s?fields=description", activePlaylistId);
  response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(3000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());
    if (!error) {
      String description = json["description"];

      similarMenuItems.clear();
      const char playlistPrefix[] = "spotify:playlist:";
      int urlPosition = description.indexOf(playlistPrefix);
      while (urlPosition > 0) {
        SimilarItem_t item;
        int idStart = urlPosition + sizeof(playlistPrefix) - 1;
        int idEnd = idStart + SPOTIFY_ID_SIZE;
        strlcpy(item.playlistId, description.c_str() + idStart, sizeof(item.playlistId));

        int nameStart = description.indexOf(">", idEnd) + 1;
        int nameEnd = description.indexOf("<", nameStart);
        if (nameStart > 0 && nameEnd > 0) {
          String name = description.substring(nameStart, nameEnd);
          int matchingGenreIndex = getGenreIndexByPlaylistId(item.playlistId);
          if (name == "Intro" || name == "Pulse" || name == "Edge" || matchingGenreIndex >= 0) {
            if(matchingGenreIndex < 0) {
              strncpy(item.name, name.c_str(), sizeof(item.name) - 1);
            } else {
              item.name[0] = '\0';
            }
            similarMenuItems.push_back(item);
          }

          urlPosition = description.indexOf(playlistPrefix, nameEnd);
        } else {
          urlPosition = description.indexOf(playlistPrefix, urlPosition + sizeof(playlistPrefix));
        }
      }
      setMenuMode(SimilarList, 0);
    }
  } else {
    eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyGetPlaylists() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response;
  int nextOffset = 0;
  unsigned int limit = 50;
  unsigned int offset = 0;
  char url[60];

  spotifyPlaylists.clear();

  while (nextOffset >= 0) {
    snprintf(url, sizeof(url), "me/playlists/?fields=items(id,name)&limit=%d&offset=%d", limit, nextOffset);
    response = spotifyApiRequest("GET", url);
    if (response.httpCode == 200) {
      DynamicJsonDocument json(6000);
      DeserializationError error = deserializeJson(json, response.payload.c_str());
      if (!error) {
        limit = json["limit"];
        offset = json["offset"];

        JsonArray items = json["items"];
        for (auto item : items) {
          const char *id = item["id"];
          const char *name = item["name"];
          SpotifyPlaylist_t playlist;
          strncpy(playlist.id, id, sizeof(playlist.id) - 1);
          strncpy(playlist.name, name, sizeof(playlist.name) - 1);
          spotifyPlaylists.push_back(playlist);
        }

        if (json["next"].isNull()) {
          spotifyPlaylistsLoaded = true;
          if (menuMode == PlaylistsList) {
            setMenuMode(PlaylistsList, 0);
          }
          nextOffset = -1;
        } else {
          nextOffset = offset + limit;
        }
      }
    } else {
      nextOffset = -1;
      eventsSendError(response.httpCode, "Spotify error", response.payload.c_str());
    }
  }

  spotifyAction = CurrentlyPlaying;
}

void reportBatteryVoltage() {
  char url[60];
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  chipId.toUpperCase();
  snprintf(url, sizeof(url), "http://192.168.1.30:8080/rest/items/Knobby_%s_Battery", chipId.c_str());
  HTTPClient http;
  http.begin(url);
  uint32_t now = millis();
  Serial.printf("> [%d] POST %s %.2f\n", now, url, batteryVoltage);
  int code = http.POST(String(batteryVoltage));
  now = millis();
  if (code <= 0) Serial.printf("> [%d] Error: %s\n", now, http.errorToString(code).c_str());
  http.end();
  lastBatteryReportMillis = now;
}