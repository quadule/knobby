# Knobby: a little remote for Spotify and more

[![Latest release](https://img.shields.io/github/v/release/quadule/knobby?logo=github)](https://github.com/quadule/knobby/releases/latest)
[![Build status](https://github.com/quadule/knobby/actions/workflows/main.yml/badge.svg)](https://github.com/quadule/knobby/actions/workflows/main.yml)
![Downloads](https://img.shields.io/github/downloads/quadule/knobby/total?color=orange&logo=github)

<img src="images/demo.gif?raw=true" width="427" height="240" alt="demo of genre selection">

## Discover new music and rediscover your own collection with Knobby. It’s a one-click cure for musical boredom.

<a href="https://www.tindie.com/stores/milowinningham/?ref=offsite_badges&utm_source=sellers_milowinningham&utm_medium=badges&utm_campaign=badge_medium"><img src="https://d2ss6ovg47m0r5.cloudfront.net/badges/tindie-mediums.png" alt="I sell on Tindie" width="150" height="78"></a>

The perfect companion to any smart speaker, Knobby lets you browse playlists, see what’s playing, and control playback.. Preloaded with thousands of genre and country playlists from [everynoise.com](https://everynoise.com), it encourages you to explore unfamiliar music and puts all of Spotify on shuffle.

### Untether music from your phone

Don’t fumble with your phone — Knobby is always ready, free from distractions, and designed to be shared. Show off your meticulously curated collection in your home, or pass it around at a party and put your guests in control.

## Usage

* Turn the knob to navigate between menu items.
* Click the knob to select or play something.
* Push in and rotate the knob to switch menus.
* Hold the knob for couple seconds for a random genre, country, or playlist from the last open menu.
  - Keep holding the knob until the progress bar reaches the end to play immediately.
  - Release the knob before the progress bar reaches the end to keep browsing.
* When browsing genres, double click the knob to switch sort modes: _name_ (the default alphabetical order), _suffix_, _ambience_, _modernity_, and _popularity_.
* When playing something, double click the knob to skip to the next track.

## Make it yourself

<a href="images/knobby3.jpg?raw=true"><img src="images/thumb.knobby3.jpg?raw=true" width="240" height="320" alt="knobby with wood finish in hand"></a>
<a href="images/knobby6.jpg?raw=true"><img src="images/thumb.knobby6.jpg?raw=true" width="427" height="320" alt="three knobby remotes"></a>

### Get the parts

* [LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display](https://www.aliexpress.com/item/33048962331.html)
* [Bourns PEC11R-4215F-S0024 rotary encoder](https://www.mouser.com/ProductDetail/Bourns/PEC11R-4215F-S0024?qs=Zq5ylnUbLm5lAqmKF80wzQ%3D%3D)
* [MakerFocus 1100mAh LiPo battery](https://www.makerfocus.com/products/makerfocus-3-7v-1100mah-lithium-rechargeable-battery-1s-3c-lipo-battery-with-protection-board-pack-of-4)
* [3D printed case and knob](https://www.printables.com/model/156363)

#### Other rotary encoders

If you use a different rotary encoder in your build and find that the knob must be turned two clicks to move one item through the menu, you can configure the encoder’s pulse count.

1. Edit `data/data.json` and replace `null` with `2` for `pulseCount`.
2. Run `pio run --target uploadfs` to upload the configuration file.

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

### Compile from source

1. Edit `data/data.json` and enter your wifi network information (if you want; it can also be configured later)
2. Build and upload with [PlatformIO](https://platformio.org/): `pio run && pio run --target upload && pio run --target uploadfs`

If data.json is not configured or there is a problem connecting to your network, knobby will enter configuration mode. Join the temporary wifi network displayed on screen and wait for the configuration portal to appear or visit http://192.168.4.1. Enter your wifi network information, then switch back to your normal wifi network and visit http://knobby.local to continue.

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](https://everynoise.com).
* Icons from [Google Material](https://material.io/resources/icons/) and [Typicons](https://www.s-ings.com/typicons/).
