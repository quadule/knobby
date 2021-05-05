#include "Arduino.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "driver/rtc_io.h"
#include "mbedtls/md.h"

#define SPOTIFY_ID_SIZE 22
#define SPOTIFY_WAIT_MILLIS 900

const uint16_t spotifyPollInterval = 18000;
const char *spotifyCACertificate =
    "-----BEGIN CERTIFICATE-----\n" // DigiCert Global Root CA
    "MIIE6jCCA9KgAwIBAgIQCjUI1VwpKwF9+K1lwA/35DANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
    "QTAeFw0yMDA5MjQwMDAwMDBaFw0zMDA5MjMyMzU5NTlaME8xCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxKTAnBgNVBAMTIERpZ2lDZXJ0IFRMUyBS\n"
    "U0EgU0hBMjU2IDIwMjAgQ0ExMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
    "AQEAwUuzZUdwvN1PWNvsnO3DZuUfMRNUrUpmRh8sCuxkB+Uu3Ny5CiDt3+PE0J6a\n"
    "qXodgojlEVbbHp9YwlHnLDQNLtKS4VbL8Xlfs7uHyiUDe5pSQWYQYE9XE0nw6Ddn\n"
    "g9/n00tnTCJRpt8OmRDtV1F0JuJ9x8piLhMbfyOIJVNvwTRYAIuE//i+p1hJInuW\n"
    "raKImxW8oHzf6VGo1bDtN+I2tIJLYrVJmuzHZ9bjPvXj1hJeRPG/cUJ9WIQDgLGB\n"
    "Afr5yjK7tI4nhyfFK3TUqNaX3sNk+crOU6JWvHgXjkkDKa77SU+kFbnO8lwZV21r\n"
    "eacroicgE7XQPUDTITAHk+qZ9QIDAQABo4IBrjCCAaowHQYDVR0OBBYEFLdrouqo\n"
    "qoSMeeq02g+YssWVdrn0MB8GA1UdIwQYMBaAFAPeUDVW0Uy7ZvCj4hsbw5eyPdFV\n"
    "MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIw\n"
    "EgYDVR0TAQH/BAgwBgEB/wIBADB2BggrBgEFBQcBAQRqMGgwJAYIKwYBBQUHMAGG\n"
    "GGh0dHA6Ly9vY3NwLmRpZ2ljZXJ0LmNvbTBABggrBgEFBQcwAoY0aHR0cDovL2Nh\n"
    "Y2VydHMuZGlnaWNlcnQuY29tL0RpZ2lDZXJ0R2xvYmFsUm9vdENBLmNydDB7BgNV\n"
    "HR8EdDByMDegNaAzhjFodHRwOi8vY3JsMy5kaWdpY2VydC5jb20vRGlnaUNlcnRH\n"
    "bG9iYWxSb290Q0EuY3JsMDegNaAzhjFodHRwOi8vY3JsNC5kaWdpY2VydC5jb20v\n"
    "RGlnaUNlcnRHbG9iYWxSb290Q0EuY3JsMDAGA1UdIAQpMCcwBwYFZ4EMAQEwCAYG\n"
    "Z4EMAQIBMAgGBmeBDAECAjAIBgZngQwBAgMwDQYJKoZIhvcNAQELBQADggEBAHer\n"
    "t3onPa679n/gWlbJhKrKW3EX3SJH/E6f7tDBpATho+vFScH90cnfjK+URSxGKqNj\n"
    "OSD5nkoklEHIqdninFQFBstcHL4AGw+oWv8Zu2XHFq8hVt1hBcnpj5h232sb0HIM\n"
    "ULkwKXq/YFkQZhM6LawVEWwtIwwCPgU7/uWhnOKK24fXSuhe50gG66sSmvKvhMNb\n"
    "g0qZgYOrAKHKCjxMoiWJKiKnpPMzTFuMLhoClw+dj20tlQj7T9rxkTgl4ZxuYRiH\n"
    "as6xuwAwapu3r9rxxZf+ingkquqTgLozZXq8oXfpf2kUCwA/d5KxTVtzhwoT0JzI\n"
    "8ks5T1KESaZMkE4f97Q=\n"
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
  char selectedDeviceId[41] = "";
} SpotifyUser_t;

typedef struct {
  char id[41] = "";
  char name[64] = "";
  uint8_t volumePercent;
} SpotifyDevice_t;

typedef struct {
  char id[41] = "";
  String name;
} SpotifyPlaylist_t;

typedef struct {
  char name[100] = "";
  char artistName[100] = "";
  char albumName[64] = "";
  char trackId[SPOTIFY_ID_SIZE + 1] = "";
  char contextName[64] = "";
  bool isLiked = false;
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
  CheckLike,
  ToggleLike,
  ToggleShuffle,
  TransferPlayback,
  GetPlaylistDescription,
  GetPlaylists
};

enum GrantTypes { gt_authorization_code, gt_refresh_token };

RTC_DATA_ATTR char spotifyAccessToken[300] = "";
RTC_DATA_ATTR char spotifyRefreshToken[150] = "";
RTC_DATA_ATTR time_t spotifyTokenLifetime = 0;
RTC_DATA_ATTR time_t spotifyTokenSeconds = 0;
RTC_DATA_ATTR char activeSpotifyDeviceId[41] = "";
RTC_DATA_ATTR SpotifyState_t spotifyState = {};

WiFiClientSecure spotifyWifiClient;
HTTPClient spotifyHttp;
long spotifyApiRequestStartedMillis = -1;
String spotifyAuthCode;
char spotifyCodeVerifier[44] = "";
char spotifyCodeChallenge[44] = "";
uint32_t nextCurrentlyPlayingMillis = 1;
bool spotifyGettingToken = false;
SpotifyActions spotifyAction = CurrentlyPlaying;
const char *spotifyPlayPlaylistId = nullptr;
int spotifySeekToMillis = -1;
int spotifySetVolumeTo = -1;

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
void spotifyTransferPlayback();
void spotifyGetPlaylistDescription();
void spotifyGetPlaylists();
