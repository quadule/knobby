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
#include "knobby.h"
#include "sdkconfig.h"
#include "spotify.h"
#include "time.h"

extern const uint8_t GillSans24_vlw_start[] asm("_binary_GillSans24_vlw_start");
extern const uint8_t icomoon24_vlw_start[] asm("_binary_icomoon24_vlw_start");
extern const uint8_t icomoon31_vlw_start[] asm("_binary_icomoon31_vlw_start");
extern const char index_html_start[] asm("_binary_index_html_start");

const char *s3CACertificates =
    "-----BEGIN CERTIFICATE-----\n" // Baltimore CyberTrust Root
    "MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ\n"
    "RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD\n"
    "VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTAwMDUxMjE4NDYwMFoX\n"
    "DTI1MDUxMjIzNTkwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y\n"
    "ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy\n"
    "VHJ1c3QgUm9vdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr\n"
    "mD1X6CZymrV51Cni4eiVgLGw41uOKymaZN+hXe2wCQVt2yguzmKiYv60iNoS6zjr\n"
    "IZ3AQSsBUnuId9Mcj8e6uYi1agnnc+gRQKfRzMpijS3ljwumUNKoUMMo6vWrJYeK\n"
    "mpYcqWe4PwzV9/lSEy/CG9VwcPCPwBLKBsua4dnKM3p31vjsufFoREJIE9LAwqSu\n"
    "XmD+tqYF/LTdB1kC1FkYmGP1pWPgkAx9XbIGevOF6uvUA65ehD5f/xXtabz5OTZy\n"
    "dc93Uk3zyZAsuT3lySNTPx8kmCFcB5kpvcY67Oduhjprl3RjM71oGDHweI12v/ye\n"
    "jl0qhqdNkNwnGjkCAwEAAaNFMEMwHQYDVR0OBBYEFOWdWTCCR1jMrPoIVDaGezq1\n"
    "BE3wMBIGA1UdEwEB/wQIMAYBAf8CAQMwDgYDVR0PAQH/BAQDAgEGMA0GCSqGSIb3\n"
    "DQEBBQUAA4IBAQCFDF2O5G9RaEIFoN27TyclhAO992T9Ldcw46QQF+vaKSm2eT92\n"
    "9hkTI7gQCvlYpNRhcL0EYWoSihfVCr3FvDB81ukMJY2GQE/szKN+OMY3EU/t3Wgx\n"
    "jkzSswF07r51XgdIGn9w/xZchMB5hbgF/X++ZRGjD8ACtPhSNzkE1akxehi/oCr0\n"
    "Epn3o0WC4zxe9Z2etciefC7IpJ5OCBRLbf1wbWsaY71k5h+3zvDyny67G7fyUIhz\n"
    "ksLi4xaNmjICq44Y3ekQEe5+NauQrz4wlHrQMz2nZQ/1/I6eYs9HRCwBXbsdtTLS\n"
    "R9I4LtD+gdwyah617jzV/OeBHRnDJELqYzmp\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n" // Starfield Services Root Certificate Authority - G2
    "MIIEdTCCA12gAwIBAgIJAKcOSkw0grd/MA0GCSqGSIb3DQEBCwUAMGgxCzAJBgNV\n"
    "BAYTAlVTMSUwIwYDVQQKExxTdGFyZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTIw\n"
    "MAYDVQQLEylTdGFyZmllbGQgQ2xhc3MgMiBDZXJ0aWZpY2F0aW9uIEF1dGhvcml0\n"
    "eTAeFw0wOTA5MDIwMDAwMDBaFw0zNDA2MjgxNzM5MTZaMIGYMQswCQYDVQQGEwJV\n"
    "UzEQMA4GA1UECBMHQXJpem9uYTETMBEGA1UEBxMKU2NvdHRzZGFsZTElMCMGA1UE\n"
    "ChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjE7MDkGA1UEAxMyU3RhcmZp\n"
    "ZWxkIFNlcnZpY2VzIFJvb3QgQ2VydGlmaWNhdGUgQXV0aG9yaXR5IC0gRzIwggEi\n"
    "MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDVDDrEKvlO4vW+GZdfjohTsR8/\n"
    "y8+fIBNtKTrID30892t2OGPZNmCom15cAICyL1l/9of5JUOG52kbUpqQ4XHj2C0N\n"
    "Tm/2yEnZtvMaVq4rtnQU68/7JuMauh2WLmo7WJSJR1b/JaCTcFOD2oR0FMNnngRo\n"
    "Ot+OQFodSk7PQ5E751bWAHDLUu57fa4657wx+UX2wmDPE1kCK4DMNEffud6QZW0C\n"
    "zyyRpqbn3oUYSXxmTqM6bam17jQuug0DuDPfR+uxa40l2ZvOgdFFRjKWcIfeAg5J\n"
    "Q4W2bHO7ZOphQazJ1FTfhy/HIrImzJ9ZVGif/L4qL8RVHHVAYBeFAlU5i38FAgMB\n"
    "AAGjgfAwge0wDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0O\n"
    "BBYEFJxfAN+qAdcwKziIorhtSpzyEZGDMB8GA1UdIwQYMBaAFL9ft9HO3R+G9FtV\n"
    "rNzXEMIOqYjnME8GCCsGAQUFBwEBBEMwQTAcBggrBgEFBQcwAYYQaHR0cDovL28u\n"
    "c3MyLnVzLzAhBggrBgEFBQcwAoYVaHR0cDovL3guc3MyLnVzL3guY2VyMCYGA1Ud\n"
    "HwQfMB0wG6AZoBeGFWh0dHA6Ly9zLnNzMi51cy9yLmNybDARBgNVHSAECjAIMAYG\n"
    "BFUdIAAwDQYJKoZIhvcNAQELBQADggEBACMd44pXyn3pF3lM8R5V/cxTbj5HD9/G\n"
    "VfKyBDbtgB9TxF00KGu+x1X8Z+rLP3+QsjPNG1gQggL4+C/1E2DUBc7xgQjB3ad1\n"
    "l08YuW3e95ORCLp+QCztweq7dp4zBncdDQh/U90bZKuCJ/Fp1U1ervShw3WnWEQt\n"
    "8jxwmKy6abaVd38PMV4s/KCHOkdp8Hlf9BRUpJVeEXgSYCfOn8J3/yNTd126/+pZ\n"
    "59vPr5KW7ySaNRB6nJHGDn2Z9j8Z3/VyVOEVqQdZe4O/Ui5GjLIAZHYcSNPYeehu\n"
    "VsyuLAOQ1xk4meTKCRlb/weWsKh/NEnfVqn3sF/tM+2MR7cwA130A4w=\n"
    "-----END CERTIFICATE-----\n";

enum MenuModes {
  NoMenu = -99,
  VolumeControl = -2,
  RootMenu = -1,
  DeviceList = 0,
  SettingsMenu = 1,
  PlaylistList = 2,
  CountryList = 3,
  GenreList = 4,
  SimilarList = 5,
  NowPlaying = 6,
  UserList = 7
};
enum GenreSortModes { AlphabeticSort, AlphabeticSuffixSort };
enum NowPlayingButtons {
  LikeButton = 0,
  ShuffleButton = 1,
  BackButton = 2,
  PlayPauseButton = 3,
  NextButton = 4,
  RepeatButton = 5,
  VolumeButton = 6
};
enum SettingsMenuModes {
  SettingsAbout = 0,
  SettingsUpdate = 1,
  SettingsOrientation = 2,
  SettingsAddUser = 3,
  SettingsRemoveUser = 4,
  SettingsReset = 5
};

typedef struct {
  char playlistId[SPOTIFY_ID_SIZE + 1];
  char name[18];
} SimilarItem_t;

#define ICON_SIZE 24
#define LINE_HEIGHT 27
#define TFT_LIGHTBLACK 0x1082 /*  16,  16,  16 */
#define TFT_DARKERGREY 0x4A49 /*  72,  72,  72 */

template<typename C, typename T>
bool contains(C&& c, T e) {
  return std::find(std::begin(c), std::end(c), e) != std::end(c);
};

const char *rootMenuItems[] = {"devices", "settings", "playlists", "countries", "genres", "similar", "now playing", "users"};
const char *settingsMenuItems[] = {"about", "update", "orientation", "add user", "log out", "reset settings"};
const int screenWidth = TFT_HEIGHT - 1;
const int screenHeight = TFT_WIDTH - 1;
const int centerX = screenWidth / 2;
const unsigned int clickEffectMillis = 30;
const unsigned int debounceMillis = 20;
const unsigned int doubleClickMaxMillis = 360;
const unsigned int longPressMillis = 450;
const unsigned int extraLongPressMillis = 1250;
const unsigned int inactivityFadeOutMillis = 4000;
const unsigned int randomizingLengthMillis = 900;
const unsigned int statusMessageMillis = 2000;
const unsigned int newSessionSeconds = 60 * 60 * 10;
const int lineOne = 9;
const int lineDivider = lineOne + LINE_HEIGHT;
const int lineTwo = lineOne + LINE_HEIGHT + 11;
const int lineThree = lineTwo + LINE_HEIGHT;
const int lineFour = lineThree + LINE_HEIGHT;
const int lineSpacing = 3;
const int textPadding = 9;
const int textWidth = screenWidth + 1 - textPadding * 2;
const int dividerWidth = screenWidth - 12;
#ifdef LILYGO_WATCH_2019_WITH_TOUCH
  const int maxTextLines = 5;
#else
  const int maxTextLines = 3;
#endif
const String nodeName = "knobby";

// icomoon23.vlw
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
const String ICON_REPEAT = "\uE90F";
const String ICON_REPEAT_ON = "\uE910";
const String ICON_REPEAT_ONE_ON = "\uE911";
const String ICON_WIFI_OFF = "\uE912";
const String ICON_REPEAT_ONE = "\uE913";
const String ICON_BLUETOOTH = "\uE914";
const String ICON_BLUETOOTH_CONNECTED = "\uE915";
const String ICON_BLUETOOTH_DISABLED = "\uE916";
const String ICON_BLUETOOTH_SEARCHING = "\uE917";
const String ICON_SPOTIFY = "\uEA94";

// icomoon31.vlw
const String ICON_BATTERY_CHARGE = "\uE90F";
const String ICON_BATTERY_HIGH = "\uE910";
const String ICON_BATTERY_MID = "\uE911";
const String ICON_BATTERY_LOW = "\uE912";
const String ICON_BATTERY_FULL = "\uE913";

String configPassword;
String firmwareURL;
String wifiSSID;
String wifiPassword;

RTC_DATA_ATTR unsigned int bootCount = 0;
RTC_DATA_ATTR time_t bootSeconds = 0;
RTC_DATA_ATTR time_t lastSleepSeconds = 0;
RTC_DATA_ATTR MenuModes menuMode = GenreList;
RTC_DATA_ATTR uint16_t menuIndex = 0;
RTC_DATA_ATTR uint16_t menuSize = 0;
RTC_DATA_ATTR uint16_t genreIndex = 0;
RTC_DATA_ATTR GenreSortModes genreSort = AlphabeticSort;
RTC_DATA_ATTR MenuModes lastMenuMode = GenreList;
RTC_DATA_ATTR uint16_t lastMenuIndex = 0;
RTC_DATA_ATTR MenuModes lastPlaylistMenuMode = GenreList;
RTC_DATA_ATTR int playingCountryIndex = -1;
RTC_DATA_ATTR int playingGenreIndex = -1;
RTC_DATA_ATTR bool forceStartConfigPortalOnBoot = false;

Knobby knobby;
TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite ico = TFT_eSprite(&tft);
TFT_eSprite batterySprite = TFT_eSprite(&tft);
ESP32Encoder knob;
OneButton button(ROTARY_ENCODER_BUTTON_PIN, true, true);

TaskHandle_t backgroundApiTask;
AsyncWebServer server(80);
DNSServer dnsServer;
ESPAsync_WiFiManager *wifiManager;
ESPAsync_WMParameter *spotifyClientIdParam;
ESPAsync_WMParameter *spotifyClientSecretParam;

bool displayInvalidated = true;
bool displayInvalidatedPartial = true;
bool flipDisplay = false;
int lastKnobCount = 0;
bool knobHeldForRandom = false;
bool knobRotatedWhileLongPressed = false;
std::array<float, 16> knobVelocity;
int knobVelocityPosition = 0;
bool randomizingMenuAutoplay = false;
time_t secondsAsleep = 0;
bool showingProgressBar = false;
char statusMessage[24] = "";
int pressedMenuIndex = -1;
int rootMenuNowPlayingIndex = -1;
int rootMenuSimilarIndex = -1;
int rootMenuUsersIndex = -1;
int similarMenuGenreIndex = -1;
std::vector<SimilarItem_t> similarMenuItems;
long lastConnectedMillis = -1;
unsigned long checkedForUpdateMillis = 0;
unsigned long clickEffectEndMillis = 0;
unsigned long inactivityMillis = 90000;
unsigned long lastBatteryUpdateMillis = 0;
unsigned long lastDelayMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastInputMillis = 1;
unsigned long lastReconnectAttemptMillis = 0;
unsigned long longPressStartedMillis = 0;
unsigned long nowPlayingDisplayMillis = 0;
unsigned long randomizingMenuEndMillis = 0;
unsigned long randomizingMenuNextMillis = 0;
unsigned long randomizingMenuTicks = 0;
unsigned long statusMessageUntilMillis = 0;
unsigned long volumeMenuTimeoutMillis = 15000;
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
void knobPressStarted();

// Actions
bool readDataJson();
bool writeDataJson();
void onOTAProgress(unsigned int progress, unsigned int total);
uint16_t checkMenuSize(MenuModes mode);
void drawBattery(unsigned int percent, unsigned int y, bool charging = false);
void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1);
void drawDivider(bool selected);
void drawIcon(const String& icon, bool selected = false, bool clicked = false, bool disabled = false, bool filled = false);
void drawMenuHeader(bool selected, const char *text = "");
void drawWifiSetup();
void invalidateDisplay(bool eraseDisplay = false);
void playPlaylist(const char *playlistId, const char *name = "");
void saveAndSleep();
void setActiveDevice(SpotifyDevice_t *device);
void setActiveUser(SpotifyUser_t *user);
void setMenuIndex(uint16_t newMenuIndex);
void setMenuMode(MenuModes newMode, uint16_t newMenuIndex);
void setStatusMessage(const char *message, unsigned long durationMs = statusMessageMillis);
void shutdownIfLowBattery();
void startDeepSleep();
void startRandomizingMenu(bool autoplay = false);
void updateDisplay();
void updateFirmware();

// Getters
int formatMillis(char *output, unsigned long millis);
int getGenreIndexByName(const char *genreName);
int getGenreIndexByPlaylistId(const char *playlistId);
uint16_t getMenuIndexForGenreIndex(uint16_t index);
uint16_t getGenreIndexForMenuIndex(uint16_t index, MenuModes mode);
bool isGenreMenu(MenuModes mode);
bool isPlaylistMenu(MenuModes mode);
unsigned long getLongPressedMillis();
unsigned long getExtraLongPressedMillis();
bool shouldShowProgressBar();
bool shouldShowRandom();
bool shouldShowSimilarMenu();
bool shouldShowUsersMenu();
