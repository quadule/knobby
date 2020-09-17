# knobby
**🎵 A little remote to remind you that there's a lot of music out there.**

* Spin the knob and explore over 4,800 microgenres and 150 countries with playlists from [everynoise.com](http://everynoise.com) and your library
* Control playback and see what's playing on any Spotify Connect device
* Switch between multiple Spotify accounts and devices

<a href="photos/knobby1.jpg?raw=true"><img src="photos/thumb.knobby1.jpg?raw=true" width="180" height="240" alt="knobby with coin"></a>
<a href="photos/knobby2.jpg?raw=true"><img src="photos/thumb.knobby2.jpg?raw=true" width="360" height="240" alt="knobby side view"></a>
<a href="photos/knobby4.jpg?raw=true"><img src="photos/thumb.knobby4.jpg?raw=true" width="180" height="240" alt="knobby playing"></a>

## Hardware

* LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display
* Bourns PEC11R-4215F-S0024 rotary encoder
* MakerFocus 1000mAh LiPo battery
* [Case and knob](/case) printed in Hatchbox wood PLA, sanded, stained and lacquered

Connect the rotary encoder A and B pins to GPIO 12 and 13 and the button pin to GPIO 15.

## Setup

1. [Register an application for the Spotify API](https://developer.spotify.com/dashboard/) and configure `http://knobby.local/callback` as a redirect URI
2. Copy `src/settings.h.example` to `settings.h` and fill in your API and wi-fi credentials
3. Build and upload with [PlatformIO](https://platformio.org/): `platformio run && platformio run --target upload && platformio run --target uploadfs`
4. Visit http://knobby.local to authorize your Spotify account

## Usage

* Rotate and click the knob to select
* Hold and rotate the knob to switch menus and sort modes
* Hold the knob for a couple seconds for a random genre
* Visit http://knobby.local/authorize to add additional Spotify accounts

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](http://everynoise.com).
* Some of the Spotify API integration was derived from [M5Spot](https://github.com/CosmicMac/M5Spot)
* [Material icons from Google](https://material.io/resources/icons/)
