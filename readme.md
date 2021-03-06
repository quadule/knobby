# knobby
**🎵 A little remote to remind you that there's a lot of music out there.**

* Spin the knob and explore over 5,400 music genres and 160 countries with playlists from [everynoise.com](http://everynoise.com) and your own library
* Control playback and see what's playing on any Spotify Connect device
* Switch between multiple Spotify accounts and devices

<img src="photos/demo.gif?raw=true" width="427" height="240" alt="demo of genre selection">

<a href="photos/knobby3.jpg?raw=true"><img src="photos/thumb.knobby3.jpg?raw=true" width="180" height="240" alt="knobby with coin"></a>
<a href="photos/knobby2.jpg?raw=true"><img src="photos/thumb.knobby2.jpg?raw=true" width="360" height="240" alt="knobby side view"></a>

## Hardware

Connect the rotary encoder A and B pins to GPIO 12 and 13 and the button pin to GPIO 15.

## Make it

* LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display
* Bourns PEC11R-4215F-S0024 rotary encoder
* MakerFocus 1100mAh LiPo battery
* [3D printed case and knob](/case)

### Flashing the firmware

To flash the firmware, you will need to have Python and [esptool.py](https://github.com/espressif/esptool) installed. If the serial device is not detected, you might need to install [the CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).

1. [Go to the latest release](https://github.com/quadule/knobby/releases/latest) and download the firmware zip for your hardware: knobby-firmware.zip for the original knobby or twatch-firmware.zip for the T-Watch encoder version.
2. With the USB cable connected, run the flash script in the directory of the extracted firmware zip:
  - on Linux or Mac: `./flash.sh`
  - on Windows: `flash.bat`

### Compiling from source

1. Edit `data/data.json` and enter your wifi network information (if you want; it can also be configured later)
2. Build and upload with [PlatformIO](https://platformio.org/): `platformio run && platformio run --target upload && platformio run --target uploadfs`

If data.json is not configured or there is a problem connecting to your network, knobby will enter configuration mode. Join the temporary wifi network displayed on screen and wait for the configuration portal to appear or visit http://192.168.4.1. Enter your wifi network information, then switch back to your normal wifi network and visit http://knobby.local to continue.

## Usage

* Rotate and click the knob to select or play
* Hold down and rotate the knob to switch menus
* Hold down the knob for a few seconds for a random genre, country, or playlist from the last open menu
  - Keep holding the knob until the progress bar reaches the end to play automatically
  - Release the knob before the progress bar reaches the end to continue browsing
  - Rotate the knob to cancel randomizing or switch menus
* When browsing genres, double click the knob to switch sort modes
* When playing something, double click the knob to skip to the next track
* Visit http://knobby.local/authorize to add additional Spotify accounts

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](http://everynoise.com).
* Some of the Spotify API integration was derived from [M5Spot](https://github.com/CosmicMac/M5Spot)
* Icons from [Google Material](https://material.io/resources/icons/) and [Typicons](https://www.s-ings.com/typicons/).
