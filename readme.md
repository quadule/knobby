# Knobby: a little remote for Spotify and more

[![Latest release](https://img.shields.io/github/v/release/quadule/knobby?logo=github)](https://github.com/quadule/knobby/releases/latest)
[![Build status](https://github.com/quadule/knobby/actions/workflows/main.yml/badge.svg)](https://github.com/quadule/knobby/actions/workflows/main.yml)
![Downloads](https://img.shields.io/github/downloads/quadule/knobby/total?color=orange&logo=github)

## Knobby is the remote control made for discovering music and rediscovering your own collection; _your one-click cure for musical boredom_.

<img src="images/demo.gif?raw=true" width="427" height="240" alt="example of playing a random genre with a Knobby" align="right">

### One knob controls your musical universe

* Spin and explore **6,000+** microgenre and country playlists from [everynoise.com](https://everynoise.com)
* Or hold the knob to randomize — **put all of Spotify on shuffle!**
* Enjoy fast access to your own playlists and liked songs
* See what's playing and control playback on any Spotify Connect device

<br clear="all">
<a href="images/knobby3.jpg?raw=true"><img src="images/thumb.knobby3.jpg?raw=true" width="180" height="240" alt="knobby with wood finish in hand" align="left"></a>

### Untether music from your phone

Always ready, free from distractions, and designed to be shared: Knobby is the perfect partner to any smart speaker. Show off your meticulously curated collection in your home, or pass it around a party and let your guests take control.

<br clear="all">
<a href="images/knobby6.jpg?raw=true"><img src="images/thumb.knobby6.jpg?raw=true" width="320" height="240" alt="three knobby remotes" align="right"></a>

### Make it yours(elf)

Knobby’s minimal design is easy to build and customize, with open source firmware based on the Arduino framework. The 3D printed enclosure is designed around a common ESP32 development board and a single rotary encoder. You can [buy one from me](https://www.tindie.com/products/milowinningham/knobby-a-little-remote-for-spotify-and-more/) to support this project, or [build one yourself](#build-your-own).

<a href="https://www.tindie.com/stores/milowinningham/?ref=offsite_badges&utm_source=sellers_milowinningham&utm_medium=badges&utm_campaign=badge_medium"><img src="https://d2ss6ovg47m0r5.cloudfront.net/badges/tindie-mediums.png" alt="I sell on Tindie" width="150" height="78"></a>
<br clear="all">

### See it in action

https://user-images.githubusercontent.com/15299/221440478-d7a543d5-4e82-4146-b03c-dc35ea191b35.mp4

### Read more

- [**Hackaday**: Small Spotify Remote Broadens Musical Horizons](https://hackaday.com/2020/10/21/small-spotify-remote-broadens-musical-horizons/)
- [**Hackster**: Knobby Is a Unique Handheld Remote That Controls Spotify Connect Devices](https://www.hackster.io/news/knobby-is-a-unique-handheld-remote-that-controls-spotify-connect-devices-e3428e100ab6)
- [**Tindie Blog**: Knobby! Explore Music in a New Way](https://blog.tindie.com/2022/11/knobby-explore-music-in-a-new-way/)

## Usage

* Turn the knob to navigate between menu items.
* Click the knob to select or play something.
* Push in and rotate the knob to switch menus.
* Hold the knob for couple seconds for a random genre, country, or playlist from the last open menu.
  - Keep holding the knob until the progress bar reaches the end to play immediately.
  - Release the knob before the progress bar reaches the end to keep browsing.
* When browsing genres, double click the knob to switch sort modes: _name_ (the default alphabetical order), _suffix_, _ambience_, _modernity_, and _popularity_.
* When playing something, double click the knob to skip to the next track.

### Requirements

* **Spotify Premium** is required — Knobby will not function properly on a free account.
* An always-on playback Spotify Connect device is highly recommended. Knobby can control any Spotify app or device, but a smart speaker or home audio system usually works best. If using a mobile app or some devices such as Chromecast, playback must be started before it can be controlled though Knobby.

## Build your own

### Get the parts

* [LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display](https://www.aliexpress.com/item/33048962331.html)
* [Bourns PEC11R-4215F-S0024 rotary encoder](https://www.mouser.com/ProductDetail/Bourns/PEC11R-4215F-S0024?qs=Zq5ylnUbLm5lAqmKF80wzQ%3D%3D)
* [MakerFocus 1100mAh LiPo battery](https://www.makerfocus.com/products/makerfocus-3-7v-1100mah-lithium-rechargeable-battery-1s-3c-lipo-battery-with-protection-board-pack-of-4)
* [3D printed case and knob](https://www.printables.com/model/156363)

Or, install the firmware on another supported ESP32 development board:

* [LilyGO T-Embed](https://www.lilygo.cc/products/t-embed)

### Wire it up

<img src="images/wiring-diagram.png?raw=true" width="439" height="439" alt="wiring diagram of rotary encoder with t-display board">

1. Connect the rotary encoder A and B pins to GPIO pins 12 and 13.
2. Connect one pole of the switch to GPIO pin 15.
3. Connect other pole of the switch and the encoder’s remaining middle pin to ground.

<img src="images/soldering.gif?raw=true" width="439" height="240" alt="soldering rotary encoder’s wires to the circuit board">

### Install the firmware
#### ➡️ [setup.knobby.net](https://setup.knobby.net)

The [Knobby setup tool](https://setup.knobby.net) can flash the latest firmware and configure wifi credentials over USB (requires Google Chrome or Microsoft Edge for Web Serial API support).

Alternatively, you can download [the latest release](https://github.com/quadule/knobby/releases/latest) and flash it yourself. You will need to have Python and [esptool.py](https://github.com/espressif/esptool) installed. With the USB cable connected, run the flash script in the directory of the extracted firmware zip:

  - on Linux or Mac: `./flash.sh`
  - on Windows: `flash.bat`

### Web configuration

Additional configuration is available at http://knobby.local after knobby is connected to your network. From this page, you can change hardware settings or apply a manual firmware update.

You'll need the device password for this, which is randomly generated on first boot if not configured in `data.json`. You can see the configured password in the serial console output over USB, or by double-clicking the knob on the settings > about screen.

#### Custom builds and other rotary encoders

You may need to adjust the pin assignments or pulse count to work with different hardware. If you find that the knob must be turned two clicks to move one item through the menu, try changing the pulse count from `4` to `2`. The scroll direction can be also reversed by swapping the A and B pins.

These settings can be changed from the web configuration page, or also by editing `data/data.json` when setting up a device for the first time (see below).

### Compile from source

1. Edit `data/data.json` and update the configuration if necessary.
2. Build and upload with [PlatformIO](https://platformio.org/): `pio run && pio run --target upload && pio run --target uploadfs`

If data.json is not configured or there is a problem connecting to your network, knobby will enter configuration mode. Join the temporary wifi network displayed on screen and wait for the configuration portal to appear or visit http://192.168.4.1. Enter your wifi network information, then switch back to your normal wifi network and visit http://knobby.local to continue.

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](https://everynoise.com).
* Icons from [Google Material](https://material.io/resources/icons/) and [Typicons](https://www.s-ings.com/typicons/).
