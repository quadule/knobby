#include "Arduino.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "driver/rtc_io.h"

#define SPOTIFY_ID_SIZE 22
#define SPOTIFY_WAIT_MILLIS 700

const uint16_t SPOTIFY_POLL_INTERVAL = 15000;

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
} SpotifyUser_t;

typedef struct {
  char id[41] = "";
  char name[64] = "";
  uint8_t volumePercent;
} SpotifyDevice_t;

typedef struct {
  char id[41] = "";
  char name[64] = "";
} SpotifyPlaylist_t;

typedef struct {
  char name[64] = "";
  char artistName[64] = "";
  char albumName[64] = "";
  bool isPlaying = false;
  bool isShuffled = false;
  bool disallowsSkippingNext = true;
  bool disallowsSkippingPrev = true;
  bool disallowsTogglingShuffle = true;
  uint32_t progressMillis = 0;
  uint32_t durationMillis = 0;
  uint32_t estimatedProgressMillis = 0;
  uint32_t lastUpdateMillis = 0;
  char playlistId[SPOTIFY_ID_SIZE + 1] = "";
} SpotifyState_t;

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
  ToggleShuffle,
  TransferPlayback,
  GetPlaylistDescription,
  GetPlaylists
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

String spotifyAuthCode;
RTC_DATA_ATTR char spotifyAccessToken[300] = "";
RTC_DATA_ATTR time_t spotifyTokenLifetime = 0;
RTC_DATA_ATTR time_t spotifyTokenSeconds = 0;
uint32_t nextCurrentlyPlayingMillis = 1;
bool spotifyGettingToken = false;
SpotifyActions spotifyAction = CurrentlyPlaying;
const char *spotifyPlayPlaylistId = nullptr;

std::vector<SpotifyUser_t> spotifyUsers;
SpotifyUser_t *activeSpotifyUser = nullptr;
RTC_DATA_ATTR char spotifyRefreshToken[150] = "";

std::vector<SpotifyDevice_t> spotifyDevices;
bool spotifyDevicesLoaded = false;
SpotifyDevice_t *activeSpotifyDevice = nullptr;
RTC_DATA_ATTR char activeSpotifyDeviceId[41] = "";

std::vector<SpotifyPlaylist_t> spotifyPlaylists;
bool spotifyPlaylistsLoaded = false;

int spotifySeekToMillis = -1;
int spotifySetVolumeAtMillis = -1;
int spotifySetVolumeTo = -1;

WiFiClientSecure client;
HTTPClient spotifyHttp;

RTC_DATA_ATTR SpotifyState_t spotifyState = {};

void spotifyResetProgress();
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
void spotifyToggleShuffle();
void spotifyTransferPlayback();
void spotifyGetPlaylistDescription();
void spotifyGetPlaylists();
