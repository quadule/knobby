#include <Arduino.h>

#include "ArduinoJson.h"
#include "ArduinoOTA.h"
#include "ESP32Encoder.h"
#include "ESPAsyncWebServer.h"
#include "ESPAsync_WiFiManager.h"
#include "ESPmDNS.h"
#include "OneButton.h"
#include "SPIFFS.h"
#include "StreamString.h"
#include "TFT_eSPI.h"
#include "WiFiClientSecure.h"
#include "base64.h"
#include "driver/rtc_io.h"
#include "esp_adc_cal.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "genres.h"
#include "sdkconfig.h"
#include "settings.h"
#include "spotify.h"
#include "time.h"

enum MenuModes {
  VolumeControl = -2,
  RootMenu = -1,
  DeviceList = 0,
  PlaylistList = 1,
  CountryList = 2,
  AlphabeticList = 3,
  AlphabeticSuffixList = 4,
  PopularityList = 5,
  SimilarList = 6,
  NowPlaying = 7,
  UserList = 8
};
enum NowPlayingButtons { VolumeButton = 0, ShuffleButton = 1, BackButton = 2, PlayPauseButton = 3, NextButton = 4 };
enum EventsLogTypes { log_line, log_raw };

typedef struct {
  char playlistId[SPOTIFY_ID_SIZE + 1];
  char name[18];
} SimilarItem_t;

#define FONT_NAME "GillSans24"
#define ICON_SIZE 24
#define LINE_HEIGHT 26
#define TFT_LIGHTBLACK 0x1082 /*  16,   16,  16 */
#define startsWith(STR, SEARCH) (strncmp(STR, SEARCH, strlen(SEARCH)) == 0)

const char *rootMenuItems[] = {"devices",    "playlists", "countries",   "name", "name ending",
                               "popularity", "similar",   "now playing", "users"};
const int centerX = 120;
const unsigned long clickEffectMillis = 30;
const unsigned long extraLongPressMillis = 1150;
const int lineOne = 10;
const int lineDivider = lineOne + LINE_HEIGHT + 2;
const int lineTwo = lineOne + LINE_HEIGHT + 11;
const int lineThree = lineTwo + LINE_HEIGHT;
const int lineSpacing = 3;
const int textPadding = 10;
const int textWidth = 239 - textPadding * 2;
const String nodeName = "knobby";
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

String configPassword;
String wifiSSID;
String wifiPassword;

RTC_DATA_ATTR float batteryVoltage = 0.0;
RTC_DATA_ATTR unsigned int bootCount = 0;
RTC_DATA_ATTR time_t bootSeconds = 0;
RTC_DATA_ATTR time_t lastSleepSeconds = 0;
RTC_DATA_ATTR MenuModes menuMode = AlphabeticList;
RTC_DATA_ATTR uint16_t menuIndex = 0;
RTC_DATA_ATTR uint16_t menuSize = GENRE_COUNT;
RTC_DATA_ATTR uint16_t genreIndex = 0;
RTC_DATA_ATTR MenuModes lastMenuMode = AlphabeticList;
RTC_DATA_ATTR uint16_t lastMenuIndex = 0;
RTC_DATA_ATTR MenuModes lastFullGenreMenuMode = AlphabeticList;
RTC_DATA_ATTR MenuModes lastPlaylistMenuMode = AlphabeticList;
RTC_DATA_ATTR int playingCountryIndex = -1;
RTC_DATA_ATTR int playingGenreIndex = -1;
RTC_DATA_ATTR bool forceStartConfigPortalOnBoot = false;

TFT_eSPI tft = TFT_eSPI(135, 240);
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite ico = TFT_eSprite(&tft);
ESP32Encoder knob;
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

TaskHandle_t backgroundApiTask;
AsyncEventSource events("/events");
AsyncWebServer server(80);
DNSServer dnsServer;
ESPAsync_WiFiManager *wifiManager;
ESPAsync_WMParameter *spotifyClientIdParam;
ESPAsync_WMParameter *spotifyClientSecretParam;

bool displayInvalidated = true;
bool displayInvalidatedPartial = false;
int lastKnobCount = 0;
bool knobRotatedWhileLongPressed = false;
bool randomizingMenuAutoplay = false;
time_t secondsAsleep = 0;
bool sendLogEvents = true;
bool showingProgressBar = false;
char statusMessage[24] = "";
int rootMenuNowPlayingIndex = -1;
int rootMenuSimilarIndex = -1;
int rootMenuUsersIndex = -1;
int similarMenuGenreIndex = -1;
std::vector<SimilarItem_t> similarMenuItems;
int vref = 1100;
long lastConnectedMillis = -1;
unsigned long clickEffectEndMillis = 0;
unsigned long inactivityMillis = 90000;
unsigned long lastBatteryUpdateMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastInputMillis = 1;
unsigned long lastReconnectAttemptMillis = 0;
unsigned long longPressStartedMillis = 0;
unsigned long nowPlayingDisplayMillis = 0;
unsigned long randomizingMenuEndMillis = 0;
unsigned long randomizingMenuTicks = 0;
unsigned long statusMessageUntilMillis = 0;
unsigned long wifiConnectTimeoutMillis = 45000;
size_t updateContentLength = 0;

// Events
void setup();
void loop();
void backgroundApiLoop(void *params);
void knobRotated();
void knobClicked();
void knobDoubleClicked();
void knobLongPressStarted();
void knobLongPressStopped();

// Actions
void eventsSendLog(const char *logData, EventsLogTypes type = log_line);
bool readDataJson();
bool writeDataJson();
void onOTAProgress(unsigned int progress, unsigned int total);
uint16_t checkMenuSize(MenuModes mode);
void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1);
void drawDivider(bool selected);
void drawWifiSetup();
void playPlaylist(const char *playlistId, const char *name = "");
void saveAndSleep();
void setActiveDevice(SpotifyDevice_t *device);
void setActiveUser(SpotifyUser_t *user);
void setMenuIndex(uint16_t newMenuIndex);
void setMenuMode(MenuModes newMode, uint16_t newMenuIndex);
void setStatusMessage(const char *message, unsigned long durationMs = 1300);
void startDeepSleep();
void startRandomizingMenu(bool autoplay = false);
void updateBatteryVoltage();
void updateDisplay();

// Getters
int formatMillis(char *output, unsigned long millis);
int getGenreIndexByName(const char *genreName);
int getGenreIndexByPlaylistId(const char *playlistId);
uint16_t getMenuIndexForGenreIndex(uint16_t index, MenuModes mode);
uint16_t getGenreIndexForMenuIndex(uint16_t index, MenuModes mode);
bool isGenreMenu(MenuModes mode);
bool isPlaylistMenu(MenuModes mode);
unsigned long getLongPressedMillis();
unsigned long getExtraLongPressedMillis();
bool shouldShowProgressBar();
bool shouldShowRandom();
bool shouldShowSimilarMenu();
bool shouldShowUsersMenu();
