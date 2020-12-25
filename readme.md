# knobby
**🎵 A little remote to remind you that there's a lot of music out there.**

* Spin the knob and explore over 5,000 music genres and 150+ countries with playlists from [everynoise.com](http://everynoise.com) and your own library
* Control playback and see what's playing on any Spotify Connect device
* Switch between multiple Spotify accounts and devices

<img src="photos/demo.gif?raw=true" width="427" height="240" alt="demo of genre selection">

<a href="photos/knobby3.jpg?raw=true"><img src="photos/thumb.knobby3.jpg?raw=true" width="180" height="240" alt="knobby with coin"></a>
<a href="photos/knobby2.jpg?raw=true"><img src="photos/thumb.knobby2.jpg?raw=true" width="360" height="240" alt="knobby side view"></a>

## Hardware

* LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display
* Bourns PEC11R-4215F-S0024 rotary encoder
* MakerFocus 1000mAh LiPo battery
* [3D printed case and knob](/case)

Connect the rotary encoder A and B pins to GPIO 12 and 13 and the button pin to GPIO 15.

## Setup

1. [Register an application for the Spotify API](https://developer.spotify.com/dashboard/) and configure `http://knobby.local/callback` as a redirect URI
2. Edit `data/data.json` and enter your wifi network information and Spotify app details (if you want; it can also be configured later)
3. Build and upload with [PlatformIO](https://platformio.org/): `platformio run && platformio run --target upload && platformio run --target uploadfs`
4. Visit http://knobby.local to authorize your Spotify account

If data.json is not configured or there is a problem connecting to your network, knobby will enter configuration mode. Join the temporary wifi network displayed on screen and wait for the configuration portal to appear or visit http://192.168.4.1. Enter your wifi network information plus Spotify client ID and secret, then switch back to your normal wifi network and visit http://knobby.local to continue.

## Usage

* Rotate and click the knob to select or play
* Hold down and rotate the knob to switch menus and sort modes
* Hold down the knob for a few seconds for a random genre, country, or playlist from the last open menu
  - Keep holding the knob until the progress bar reaches the end to play automatically
  - Release the knob before the progress bar reaches the end to continue browsing
  - Rotate the knob to cancel randomizing or switch menus
* Double click the knob to skip to the next track
* Visit http://knobby.local/authorize to add additional Spotify accounts

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](http://everynoise.com).
* Some of the Spotify API integration was derived from [M5Spot](https://github.com/CosmicMac/M5Spot)
* Icons from [Google Material](https://material.io/resources/icons/) and [Typicons](https://www.s-ings.com/typicons/).
