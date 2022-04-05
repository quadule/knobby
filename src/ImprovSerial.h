#pragma once

#include <HardwareSerial.h>
#include <WiFi.h>
#include <improv.h>

enum ImprovSerialType : uint8_t {
  TYPE_CURRENT_STATE = 0x01,
  TYPE_ERROR_STATE = 0x02,
  TYPE_RPC = 0x03,
  TYPE_RPC_RESPONSE = 0x04
};

static const uint8_t IMPROV_SERIAL_VERSION = 1;

class ImprovSerial {
 public:
  void setup(String firmware, String version, String variant, String name);
  bool loop(bool timeout = false);
  improv::State getState();
  String getSSID();
  String getPassword();

 protected:
  bool parseByte(uint8_t byte);
  bool parsePayload(improv::ImprovCommand &command);

  void setState(improv::State state);
  void setError(improv::Error error);
  void sendResponse(std::vector<uint8_t> &response);
  void onWiFiConnectTimeout();

  std::vector<uint8_t> buildResponse(improv::Command command);
  std::vector<uint8_t> buildVersionInfo();

  uint8_t readByte();
  void writeData(std::vector<uint8_t> &data);

  std::vector<uint8_t> _rxBuffer;
  uint32_t _lastReadByte{0};
  improv::State _state{improv::STATE_AUTHORIZED};
  improv::ImprovCommand _command{improv::Command::UNKNOWN, "", ""};

  String _firmwareName;
  String _firmwareVersion;
  String _hardwareVariant;
  String _deviceName;
};

extern ImprovSerial improvSerial;
