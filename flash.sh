#!/bin/sh
if [ $# -ne 1 ]; then
  echo Usage: flash.sh firmware-directory
else
  esptool.py --chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 "$1/bootloader.bin" 0x8000 "$1/partitions.bin" 0xe000 "$1/ota_data_initial.bin" 0x10000 "$1/firmware.bin" 0x3b0000 "$1/spiffs.bin"
fi
