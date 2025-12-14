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
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const char index_html_start[] asm("_binary_index_html_start");

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
  AlphabeticSuffixSort = 1
};
const int genreSortModesCount = 2;
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
  char displayName[64] = "";
  char id[64]           = "";
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
const uint16_t spotifyPollInterval = 10000;
const char spotifyClientId[] = "55aee603baf641f899e5bfeba3fe05d0";
const char spotifyDJPlaylistId[] = "37i9dQZF1EYkqdzj48dyYq";
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

RTC_DATA_ATTR char spotifyAccessToken[350] = "";
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

// Model-specific changes
#ifdef LILYGO_WATCH_2019_WITH_TOUCH
  bool knobReleaseWillCloseRootMenu = false;
  const int maxTextLines = 5;
#else
#ifdef LILYGO_TEMBED_S3
  bool knobReleaseWillCloseRootMenu = false;
  const int maxTextLines = 3;
#else
  bool knobReleaseWillCloseRootMenu = true;
  const int maxTextLines = 3;
#endif
#endif

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
void drawMenuHeader(bool selected, const char *text = "", int totalSize = -1);
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
