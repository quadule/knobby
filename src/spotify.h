#include "Arduino.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "driver/rtc_io.h"

#define SPOTIFY_ID_SIZE 22
#define SPOTIFY_WAIT_MILLIS 700

const uint16_t SPOTIFY_POLL_INTERVAL = 18000;
const char *spotifyCACertificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIElDCCA3ygAwIBAgIQAf2j627KdciIQ4tyS8+8kTANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
    "QTAeFw0xMzAzMDgxMjAwMDBaFw0yMzAzMDgxMjAwMDBaME0xCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxJzAlBgNVBAMTHkRpZ2lDZXJ0IFNIQTIg\n"
    "U2VjdXJlIFNlcnZlciBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n"
    "ANyuWJBNwcQwFZA1W248ghX1LFy949v/cUP6ZCWA1O4Yok3wZtAKc24RmDYXZK83\n"
    "nf36QYSvx6+M/hpzTc8zl5CilodTgyu5pnVILR1WN3vaMTIa16yrBvSqXUu3R0bd\n"
    "KpPDkC55gIDvEwRqFDu1m5K+wgdlTvza/P96rtxcflUxDOg5B6TXvi/TC2rSsd9f\n"
    "/ld0Uzs1gN2ujkSYs58O09rg1/RrKatEp0tYhG2SS4HD2nOLEpdIkARFdRrdNzGX\n"
    "kujNVA075ME/OV4uuPNcfhCOhkEAjUVmR7ChZc6gqikJTvOX6+guqw9ypzAO+sf0\n"
    "/RR3w6RbKFfCs/mC/bdFWJsCAwEAAaOCAVowggFWMBIGA1UdEwEB/wQIMAYBAf8C\n"
    "AQAwDgYDVR0PAQH/BAQDAgGGMDQGCCsGAQUFBwEBBCgwJjAkBggrBgEFBQcwAYYY\n"
    "aHR0cDovL29jc3AuZGlnaWNlcnQuY29tMHsGA1UdHwR0MHIwN6A1oDOGMWh0dHA6\n"
    "Ly9jcmwzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RDQS5jcmwwN6A1\n"
    "oDOGMWh0dHA6Ly9jcmw0LmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RD\n"
    "QS5jcmwwPQYDVR0gBDYwNDAyBgRVHSAAMCowKAYIKwYBBQUHAgEWHGh0dHBzOi8v\n"
    "d3d3LmRpZ2ljZXJ0LmNvbS9DUFMwHQYDVR0OBBYEFA+AYRyCMWHVLyjnjUY4tCzh\n"
    "xtniMB8GA1UdIwQYMBaAFAPeUDVW0Uy7ZvCj4hsbw5eyPdFVMA0GCSqGSIb3DQEB\n"
    "CwUAA4IBAQAjPt9L0jFCpbZ+QlwaRMxp0Wi0XUvgBCFsS+JtzLHgl4+mUwnNqipl\n"
    "5TlPHoOlblyYoiQm5vuh7ZPHLgLGTUq/sELfeNqzqPlt/yGFUzZgTHbO7Djc1lGA\n"
    "8MXW5dRNJ2Srm8c+cftIl7gzbckTB+6WohsYFfZcTEDts8Ls/3HB40f/1LkAtDdC\n"
    "2iDJ6m6K7hQGrn2iWZiIqBtvLfTyyRRfJs8sjX7tN8Cp1Tm5gr8ZDOo0rwAhaPit\n"
    "c+LJMto4JQtV05od8GiG7S5BNO98pVAdvzr508EIDObtHopYJeS4d60tbvVS3bR0\n"
    "j6tJLp07kzQoH3jOlOrHvdPJbRzeXDLz\n"
    "-----END CERTIFICATE-----\n";

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
  char name[64] = "";
} SpotifyPlaylist_t;

typedef struct {
  char name[64] = "";
  char artistName[64] = "";
  char albumName[64] = "";
  char trackId[SPOTIFY_ID_SIZE + 1] = "";
  char contextName[64] = "";
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
String spotifyClientId;
String spotifyClientSecret;
uint32_t nextCurrentlyPlayingMillis = 1;
bool spotifyGettingToken = false;
SpotifyActions spotifyAction = CurrentlyPlaying;
const char *spotifyPlayPlaylistId = nullptr;
int spotifySeekToMillis = -1;
int spotifySetVolumeAtMillis = -1;
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
void spotifyToggleShuffle();
void spotifyTransferPlayback();
void spotifyGetPlaylistDescription();
void spotifyGetPlaylists();
