#include <list>
#include "Arduino.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "driver/rtc_io.h"
#include "mbedtls/md.h"

#define SPOTIFY_ID_SIZE 22
#define SPOTIFY_WAIT_MILLIS 1000

const uint16_t spotifyPollInterval = 10000;
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
const char *spotifyClientId = "55aee603baf641f899e5bfeba3fe05d0";

typedef struct {
  int httpCode;
  String payload;
} HTTP_response_t;

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
  char id[41] = "";
  String name;
} SpotifyPlaylist_t;

enum SpotifyRepeatModes {
  RepeatOff = 0,
  RepeatTrack = 1,
  RepeatContext = 2
};

typedef struct {
  char name[100] = "";
  char artistName[100] = "";
  char albumName[64] = "";
  char trackId[SPOTIFY_ID_SIZE + 1] = "";
  char contextName[64] = "";
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
  CheckLike,
  ToggleLike,
  ToggleShuffle,
  ToggleRepeat,
  TransferPlayback,
  GetPlaylistDescription,
  GetPlaylists
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

RTC_DATA_ATTR char spotifyAccessToken[300] = "";
RTC_DATA_ATTR char spotifyRefreshToken[150] = "";
RTC_DATA_ATTR time_t spotifyTokenLifetime = 0;
RTC_DATA_ATTR time_t spotifyTokenSeconds = 0;
RTC_DATA_ATTR char activeSpotifyDeviceId[64] = "";
RTC_DATA_ATTR SpotifyState_t spotifyState = {};

WiFiClientSecure spotifyWifiClient;
HTTPClient spotifyHttp;
long spotifyApiRequestStartedMillis = -1;
String spotifyAuthCode;
char spotifyCodeVerifier[44] = "";
char spotifyCodeChallenge[44] = "";
uint32_t nextCurrentlyPlayingMillis = 0;
bool spotifyGettingToken = false;
SpotifyActions spotifyAction = Idle;
SpotifyActions spotifyRetryAction = Idle;
const char *spotifyPlayPlaylistId = nullptr;
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
bool spotifyPlaylistsLoaded = false;

// Utility functions
bool spotifyNeedsNewAccessToken();
void spotifyResetProgress(bool keepContext = false);
bool spotifyActionIsQueued(SpotifyActions action);
bool spotifyQueueAction(SpotifyActions action);

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
void spotifyGetPlaylistDescription();
void spotifyGetPlaylists();
