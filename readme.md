# Knobby: a little remote with a lot of possibilities

Spin the knob to browse 5,000+ genres of music along with your own playlists. Knobby is a distinctive remote control for Spotify and more.

### Explore a universe of music with just a knob
Knobby is a remote made for discovering new music and rediscovering your own collection. It’s the perfect companion to a smart speaker, allowing you to browse playlists, see what’s playing, and control playback without interrupting anything. Preloaded with thousands of genre and country playlists from [everynoise.com](https://everynoise.com), it encourages you to explore unfamiliar music and puts all of Spotify on shuffle.

### Untether music from your phone
Bring music discovery into your living room with a device that’s meant to be shared and free from distracting apps or notifications. Show off your meticulously curated collection to people in your home, or pass it around at a party and put your guests in control.

### Pop your filter bubble
Streaming services feed you new releases and top hits through algorithmic recommendations and branded playlists, but how do you dig deeper? Knobby is your musicological compendium — a vast index of musical expression that rewards deep exploration — not a media outlet engineered to compete for your attention.

## Usage
<img src="images/demo.gif?raw=true" width="427" height="240" alt="demo of genre selection">

* Turn the knob to navigate between menu items.
* Click the knob to select or play something.
* Push in and rotate the knob to switch menus.
* Hold the knob for couple seconds for a random genre, country, or playlist from the last open menu.
  - Keep holding the knob until the progress bar reaches the end to play immediately.
  - Release the knob before the progress bar reaches the end to keep browsing.
* When browsing genres, double click the knob to switch sort modes.
* When playing something, double click the knob to skip to the next track.

## Make it yourself
<a href="images/knobby3.jpg?raw=true"><img src="images/thumb.knobby3.jpg?raw=true" width="240" height="320" alt="knobby with wood finish in hand"></a>
<a href="images/knobby6.jpg?raw=true"><img src="images/thumb.knobby6.jpg?raw=true" width="427" height="320" alt="three knobby remotes"></a>

### Get the parts
* [LilyGO TTGO T-Display ESP32 board with ST7789 240x135 display](https://www.aliexpress.com/item/33048962331.html)
* [Bourns PEC11R-4215F-S0024 rotary encoder](https://www.mouser.com/ProductDetail/Bourns/PEC11R-4215F-S0024)
* [MakerFocus 1100mAh LiPo battery](https://www.makerfocus.com/products/makerfocus-3-7v-1100mah-lithium-rechargeable-battery-1s-3c-lipo-battery-with-protection-board-pack-of-4)
* [3D printed case and knob](https://www.prusaprinters.org/prints/156363)

### Wire it up
<img src="images/wiring-diagram.png?raw=true" width="439px" height="439px" alt="wiring diagram of rotary encoder with t-display board">

1. Connect the rotary encoder A and B pins to GPIO pins 12 and 13.
2. Connect one pole of the switch to GPIO pin 15.
3. Connect other pole of the switch and the encoder’s remaining middle pin to ground.

<img src="images/soldering.gif?raw=true" width="439" height="240" alt="demo of genre selection">

### Install the firmware

To flash the firmware, you will need to have Python and [esptool.py](https://github.com/espressif/esptool) installed. If the serial device is not detected, you might need to install [the CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).

1. [Go to the latest release](https://github.com/quadule/knobby/releases/latest) and download the firmware zip for your hardware: knobby-firmware.zip for the original knobby or twatch-firmware.zip for the T-Watch encoder version.
2. With the USB cable connected, run the flash script in the directory of the extracted firmware zip:
  - on Linux or Mac: `./flash.sh`
  - on Windows: `flash.bat`

### Compiling from source

1. Edit `data/data.json` and enter your wifi network information (if you want; it can also be configured later)
2. Build and upload with [PlatformIO](https://platformio.org/): `platformio run && platformio run --target upload && platformio run --target uploadfs`

If data.json is not configured or there is a problem connecting to your network, knobby will enter configuration mode. Join the temporary wifi network displayed on screen and wait for the configuration portal to appear or visit http://192.168.4.1. Enter your wifi network information, then switch back to your normal wifi network and visit http://knobby.local to continue.

## License

All code is released under the MIT license. The case design is released under the [CC-BY-NC 4.0 license](https://creativecommons.org/licenses/by-nc/4.0/).

## Credits

* Spotify playlists and inspiration from Glenn McDonald's wonderful [Every Noise at Once](https://everynoise.com).
* Icons from [Google Material](https://material.io/resources/icons/) and [Typicons](https://www.s-ings.com/typicons/).