#include "sdkconfig.h"
#include <Arduino.h>

#include <ESP32Encoder.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPAsyncDNSServer.h>
#include <ESPAsyncWebServer.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <list>
#include <OneButton.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFiClientSecure.h>

#include "knobby.h"

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
const char *spotifyCACertificate =
    "-----BEGIN CERTIFICATE-----\n" // DigiCert Global Root CA
    "MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
    "QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n"
    "CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n"
    "nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n"
    "43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n"
    "T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n"
    "gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n"
    "BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n"
    "TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n"
    "DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n"
    "hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n"
    "06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n"
    "PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n"
    "YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n"
    "CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n"
    "-----END CERTIFICATE-----\n";

#define ICON_SIZE 24
#define LINE_HEIGHT 27
#define TFT_LIGHTBLACK 0x1082 /*  16,  16,  16 */
#define TFT_DARKERGREY 0x4A49 /*  72,  72,  72 */
#define SPOTIFY_ID_SIZE 22
#define SPOTIFY_WAIT_MILLIS 1000

enum MenuModes {
  InitialSetup = -99,
  SeekControl = -3,
  VolumeControl = -2,
  RootMenu = -1,
  SettingsMenu = 0,
  CountryList = 1,
  GenreList = 2,
  ExploreList = 3,
  PlaylistList = 4,
  NowPlaying = 5,
  UserList = 6,
  DeviceList = 7
};
enum GenreSortModes {
  AlphabeticSort = 0,
  AlphabeticSuffixSort = 1,
  AmbienceSort = 2,
  ModernitySort = 3,
  PopularitySort = 4
};
const int genreSortModesCount = 5;
enum NowPlayingItems {
  LikeButton = 0,
  ShuffleButton = 1,
  BackButton = 2,
  PlayPauseButton = 3,
  NextButton = 4,
  RepeatButton = 5,
  VolumeButton = 6,
  SeekButton = 7
};
enum SettingsMenuModes {
  SettingsAbout = 0,
  SettingsUpdate = 1,
  SettingsOrientation = 2,
  SettingsAddUser = 3,
  SettingsRemoveUser = 4,
  SettingsReset = 5
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

enum SpotifyActions {
  Idle,
  GetToken,
  CurrentlyPlaying,
  CurrentProfile,
  Next,
  Previous,
  Seek,
  Toggle,
  PlayPlaylist,
  GetDevices,
  SetVolume,
  CheckLike,
  ToggleLike,
  ToggleShuffle,
  ToggleRepeat,
  TransferPlayback,
  GetPlaylistInformation,
  GetPlaylists,
  GetImage
};

enum SpotifyRepeatModes {
  RepeatOff = 0,
  RepeatTrack = 1,
  RepeatContext = 2
};

enum ExploreItemTypes { ExploreItemAlbum, ExploreItemArtist, ExploreItemPlaylist };

typedef struct {
  ExploreItemTypes type;
  char id[SPOTIFY_ID_SIZE + 1] = "";
  String name;
} ExploreItem_t;

typedef struct {
  char name[64] = "";
  char refreshToken[150] = "";
  bool selected = false;
  char selectedDeviceId[64] = "";
} SpotifyUser_t;

typedef struct {
  char id[64] = "";
  char name[64] = "";
  uint8_t volumePercent;
} SpotifyDevice_t;

typedef struct {
  char id[SPOTIFY_ID_SIZE + 1] = "";
  String name;
} SpotifyPlaylist_t;

typedef struct {
  char id[SPOTIFY_ID_SIZE + 1] = "";
  char name[100] = "";
} SpotifyArtist_t;

typedef struct {
  char name[100] = "";
  SpotifyArtist_t artists[3] = {};
  char artistsName[100] = "";
  char albumName[100] = "";
  char albumId[SPOTIFY_ID_SIZE + 1] = "";
  char trackId[SPOTIFY_ID_SIZE + 1] = "";
  char contextName[256] = "";
  char contextUri[100] = "";
  char imageUrl[100] = "";
  bool isLiked = false;
  bool isPlaying = false;
  bool isShuffled = false;
  SpotifyRepeatModes repeatMode = RepeatOff;
  bool checkedLike = false;
  bool disallowsSkippingNext = true;
  bool disallowsSkippingPrev = true;
  bool disallowsTogglingShuffle = true;
  bool disallowsTogglingRepeatContext = true;
  bool disallowsTogglingRepeatTrack = true;
  bool isPrivateSession = false;
  uint32_t progressMillis = 0;
  uint32_t durationMillis = 0;
  uint32_t estimatedProgressMillis = 0;
  uint32_t lastUpdateMillis = 0;
} SpotifyState_t;

template<typename C, typename T>
bool contains(C&& c, T e) {
  return std::find(std::begin(c), std::end(c), e) != std::end(c);
};

const char *hostname = "knobby";
const char *rootMenuItems[] = {"settings", "countries", "genres", "explore", "playlists", "now playing", "users", "devices"};
const char *settingsMenuItems[] = {"about", "update", "orientation", "add user", "log out", "reset settings"};
const uint8_t backlightChannel = 4;
const int screenWidth = TFT_HEIGHT;
const int screenHeight = TFT_WIDTH;
const int centerX = screenWidth / 2;
const unsigned int clickEffectMillis = 80;
const unsigned int debounceMillis = 10;
const unsigned int doubleClickMaxMillis = 360;
const unsigned int longPressMillis = 450;
const unsigned int extraLongPressMillis = 1450;
const unsigned int inactivityFadeOutMillis = 4000;
const unsigned int randomizingLengthMillis = 900;
const unsigned int waitToShowProgressMillis = 2000;
const unsigned int statusMessageMillis = 1750;
const unsigned int newSessionSeconds = 60 * 60 * 10;
const int textPadding = 9;
const int textStartX = textPadding + 1;
const int textWidth = screenWidth - textPadding * 2;
const int dividerWidth = screenWidth - 12;
const int lineOne = textPadding;
const int lineDivider = lineOne + LINE_HEIGHT + 1;
const int lineTwo = lineOne + LINE_HEIGHT + 12;
const int lineThree = lineTwo + LINE_HEIGHT;
const int lineFour = lineThree + LINE_HEIGHT;
const int lineSpacing = 3;
const int albumSize = 64;
const int albumX = screenWidth - albumSize - 6;
const int albumY = lineTwo;
#ifdef LILYGO_WATCH_2019_WITH_TOUCH
  const int maxTextLines = 5;
#else
  const int maxTextLines = 3;
#endif
const uint16_t spotifyPollInterval = 10000;
const char spotifyClientId[] = "55aee603baf641f899e5bfeba3fe05d0";
const char spotifyPlaylistContextPrefix[] = "spotify:playlist:";

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

RTC_DATA_ATTR unsigned int bootCount = 0;
RTC_DATA_ATTR time_t bootSeconds = 0;
RTC_DATA_ATTR time_t lastSleepSeconds = 0;
RTC_DATA_ATTR MenuModes menuMode = NowPlaying;
RTC_DATA_ATTR uint16_t menuIndex = PlayPauseButton;
RTC_DATA_ATTR uint16_t menuSize = 0;
RTC_DATA_ATTR uint16_t countryIndex = 0;
RTC_DATA_ATTR uint16_t genreIndex = 0;
RTC_DATA_ATTR uint16_t playlistIndex = 0;
RTC_DATA_ATTR GenreSortModes genreSort = AlphabeticSort;
RTC_DATA_ATTR MenuModes lastMenuMode = NowPlaying;
RTC_DATA_ATTR uint16_t lastMenuIndex = 0;
RTC_DATA_ATTR MenuModes lastPlaylistMenuMode = GenreList;
RTC_DATA_ATTR int playingCountryIndex = -1;
RTC_DATA_ATTR int playingGenreIndex = -1;

RTC_DATA_ATTR char spotifyAccessToken[300] = "";
RTC_DATA_ATTR char spotifyRefreshToken[150] = "";
RTC_DATA_ATTR time_t spotifyTokenLifetime = 0;
RTC_DATA_ATTR time_t spotifyTokenSeconds = 0;
RTC_DATA_ATTR char activeSpotifyDeviceId[64] = "";
RTC_DATA_ATTR SpotifyState_t spotifyState = {};
RTC_DATA_ATTR bool spotifyStateLoaded = false;

Knobby knobby;
TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite ico = TFT_eSprite(&tft);
TFT_eSprite batterySprite = TFT_eSprite(&tft);
ESP32Encoder knob;
OneButton button;

TaskHandle_t backgroundApiTask;
AsyncWebServer server(80);
AsyncDNSServer dnsServer;
ESPAsync_WiFiManager *wifiManager;
WiFiClientSecure spotifyWifiClient;
HTTPClient spotifyHttp;

TaskHandle_t jpgDecodeTask;
bool jpgDecodeReady = false;
bool jpgRenderReady = false;
uint16_t jpgBlockBuffer[16 * 16];
int32_t jpgBlockX, jpgBlockY, jpgBlockW, jpgBlockH;

bool displayInvalidated = true;
bool displayInvalidatedPartial = false;
const String defaultFirmwareURL = (KNOBBY_FIRMWARE_BUCKET KNOBBY_FIRMWARE_PATH);
String firmwareURL = defaultFirmwareURL;
bool startFirmwareUpdateFromURL = false;
int lastKnobCount = 0;
int lastKnobDirection = 0;
bool knobHeldForRandom = false;
bool knobRotatedWhileLongPressed = false;
std::array<float, 12> knobVelocity;
int knobVelocityPosition = 0;
bool randomizingMenuAutoplay = false;
time_t secondsAsleep = 0;
bool showingNetworkInfo = false;
bool showingProgressBar = false;
bool showingStatusMessage = false;
char statusMessage[24] = "";
char menuText[256] = "";
int pressedMenuIndex = -1;
int rootMenuNowPlayingIndex = -1;
int rootMenuExploreIndex = -1;
int rootMenuPlaylistsIndex = -1;
int rootMenuUsersIndex = -1;
int rootMenuDevicesIndex = -1;
std::vector<ExploreItem_t> exploreMenuItems;
int explorePlaylistsGenreIndex = -1;
std::vector<ExploreItem_t> explorePlaylists;
long lastConnectedMillis = -1;
unsigned long checkedForUpdateMillis = 0;
unsigned long clickEffectEndMillis = 0;
unsigned long inactivityMillis = 90000;
unsigned long lastBatteryUpdateMillis = 0;
unsigned long lastDelayMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastInputMillis = 0;
unsigned long lastReconnectAttemptMillis = 0;
unsigned long longPressStartedMillis = 0;
unsigned long nowPlayingDisplayMillis = 0;
unsigned long randomizingMenuEndMillis = 0;
unsigned long randomizingMenuNextMillis = 0;
unsigned long randomizingMenuTicks = 0;
unsigned long statusMessageUntilMillis = 0;
unsigned long menuClickedMillis = 0;
unsigned long menuTimeoutMillis = 15000;
unsigned long wifiConnectTimeoutMillis = 45000;
bool wifiConnectWarning = false;
String wifiSSID;
String wifiPassword;
size_t updateContentLength = 0;

long spotifyApiRequestStartedMillis = -1;
String spotifyAuthCode;
char spotifyCodeVerifier[44] = "";
char spotifyCodeChallenge[44] = "";
StreamString spotifyImage;
bool spotifyImageDrawn = false;
unsigned short emptyCurrentlyPlayingResponses = 0;
uint32_t nextCurrentlyPlayingMillis = 0;
bool spotifyGettingToken = false;
SpotifyActions spotifyAction = Idle;
SpotifyActions spotifyRetryAction = Idle;
char spotifyGetPlaylistId[SPOTIFY_ID_SIZE + 1] = "";
char spotifyPlayUri[100] = "";
int spotifyPlayAtMillis = -1;
int spotifySeekToMillis = -1;
int spotifySetVolumeTo = -1;
std::list<SpotifyActions> spotifyActionQueue;

std::vector<SpotifyUser_t> spotifyUsers;
SpotifyUser_t *activeSpotifyUser = nullptr;

std::vector<SpotifyDevice_t> spotifyDevices;
bool spotifyDevicesLoaded = false;
SpotifyDevice_t *activeSpotifyDevice = nullptr;

std::vector<SpotifyPlaylist_t> spotifyPlaylists;
unsigned int spotifyPlaylistsCount = 0;
bool spotifyPlaylistsLoaded = false;
std::vector<SpotifyPlaylist_t> spotifyLinkedPlaylists;

// Events
void setup();
void loop();
void backgroundApiLoop(void *params);
void jpgDecodeLoop(void *params);
void knobRotated();
void knobClicked();
void knobDoubleClicked();
void knobLongPressStarted();
void knobLongPressStopped();
void knobPressStarted();

// Actions
bool readDataJson();
bool writeDataJson();
bool onJpgBlockDecoded(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
void onOTAProgress(unsigned int progress, unsigned int total);
uint16_t checkMenuSize(MenuModes mode);
void drawBattery(unsigned int percent, unsigned int y, bool charging = false);
void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines = 1);
void drawDivider(bool selected);
void drawIcon(const String& icon, bool selected = false, bool clicked = false, bool disabled = false, bool filled = false);
void drawMenuHeader(bool selected, const char *text = "");
void drawSetup();
void invalidateDisplay(bool eraseDisplay = false);
void playMenuPlaylist(MenuModes mode, uint16_t index);
void playUri(const char *uri, const char *name);
void setActiveDevice(SpotifyDevice_t *device);
void setActiveUser(SpotifyUser_t *user);
void setLightSleepEnabled(bool enabled);
void setMenuIndex(uint16_t newMenuIndex);
void setMenuMode(MenuModes newMode, uint16_t newMenuIndex);
void setStatusMessage(const char *message, unsigned long durationMs = statusMessageMillis);
void setupKnob();
void shutdownIfLowBattery();
void startDeepSleep();
void startRandomizingMenu(bool autoplay = false);
void startWifiManager();
void updateDisplay();
void updateFirmware();

// Getters
int formatMillis(char *output, unsigned long millis);
void getContextName(char *name, const char *contextUri);
uint16_t getMenuIndexForGenreIndex(uint16_t index);
int getMenuIndexForPlaylist(const char *contextUri);
int getGenreIndexForMenuIndex(uint16_t index, MenuModes mode);
void getMenuText(char *name, MenuModes mode, uint16_t index);
void getContextUri(char *uri, MenuModes mode, uint16_t index);
bool isGenreMenu(MenuModes mode);
bool isPlaylistMenu(MenuModes mode);
unsigned long getLongPressedMillis();
unsigned long getExtraLongPressedMillis();
bool shouldShowProgressBar();
bool shouldShowRandom();
bool shouldShowExploreMenu();
bool shouldShowPlaylistsMenu();
bool shouldShowUsersMenu();

// Spotify Web API methods
void spotifyGetToken(const char *code, GrantTypes grant_type);
void spotifyCurrentlyPlaying();
void spotifyCurrentProfile();
void spotifyNext();
void spotifyPrevious();
void spotifySeek();
void spotifyToggle();
void spotifyPlayPlaylist();
void spotifyGetDevices();
void spotifySetVolume();
void spotifyCheckLike();
void spotifyToggleLike();
void spotifyToggleShuffle();
void spotifyToggleRepeat();
void spotifyTransferPlayback();
void spotifyGetPlaylistInformation();
void spotifyGetPlaylists();
void spotifyGetImage();

bool spotifyNeedsNewAccessToken();
void spotifyResetProgress(bool keepContext = false);
bool spotifyActionIsQueued(SpotifyActions action);
bool spotifyQueueAction(SpotifyActions action);
