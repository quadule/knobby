#include "ImprovSerial.h"

void ImprovSerial::setup(std::string name) {
  _deviceName = name;
  if (WiFi.getMode() == WIFI_STA && WiFi.isConnected()) {
    _state = improv::STATE_PROVISIONED;
  } else {
    _state = improv::STATE_AUTHORIZED;
  }
}

improv::State ImprovSerial::getState() { return _state; }

std::string ImprovSerial::getSSID() { return _command.ssid; }

std::string ImprovSerial::getPassword() { return _command.password; }

uint8_t ImprovSerial::readByte() {
  uint8_t data;
  Serial.readBytes(&data, 1);
  return data;
}

void ImprovSerial::writeData(std::vector<uint8_t> &data) {
  data.push_back('\n');
  Serial.write(data.data(), data.size());
}

bool ImprovSerial::loop() {
  const uint32_t now = millis();
  if (now - _lastReadByte > 50) {
    _rxBuffer.clear();
    _lastReadByte = now;
  }
  while (Serial.available()) {
    uint8_t byte = readByte();
    if (parseByte(byte)) {
      _lastReadByte = now;
    } else {
      _rxBuffer.clear();
    }
  }
  if (_state != improv::STATE_PROVISIONED) {
    if (WiFi.getMode() == WIFI_STA && WiFi.isConnected()) {
      if (_state == improv::STATE_PROVISIONING) {
        setState(improv::STATE_PROVISIONED);
        std::vector<uint8_t> url = buildResponse(improv::WIFI_SETTINGS);
        sendResponse(url);
        return true;
      } else {
        _state = improv::STATE_PROVISIONED;
      }
    } else if (now - _lastReadByte > 1000) {
      onWiFiConnectTimeout();
    }
  }
  return _lastReadByte > 0 && _rxBuffer.size() > sizeof("IMPROV");
}

std::vector<uint8_t> ImprovSerial::buildResponse(improv::Command command) {
  std::vector<std::string> urls;
  urls.push_back("http://knobby.local");
  std::vector<uint8_t> data = improv::build_rpc_response(command, urls, false);
  return data;
}

std::vector<uint8_t> ImprovSerial::buildVersionInfo() {
  std::vector<std::string> infos = {_firmwareName, _firmwareVersion, _hardwareVariant, _deviceName};
  std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
  return data;
};

bool ImprovSerial::parseByte(uint8_t byte) {
  size_t at = _rxBuffer.size();
  _rxBuffer.push_back(byte);
  log_d("Improv Serial byte: 0x%02X", byte);
  const uint8_t *raw = &_rxBuffer[0];
  if (at == 0) return byte == 'I';
  if (at == 1) return byte == 'M';
  if (at == 2) return byte == 'P';
  if (at == 3) return byte == 'R';
  if (at == 4) return byte == 'O';
  if (at == 5) return byte == 'V';
  if (at == 6) return byte == IMPROV_SERIAL_VERSION;
  if (at == 7) return true;
  uint8_t type = raw[7];
  if (at == 8) return true;
  uint8_t dataLength = raw[8];
  if (at < 8 + dataLength) return true;
  if (at == 8 + dataLength) return true;
  if (at == 8 + dataLength + 1) {
    uint8_t checksum = 0x00;
    for (uint8_t i = 0; i < at; i++) checksum += raw[i];

    if (checksum != byte) {
      log_w("Error decoding Improv payload");
      setError(improv::ERROR_INVALID_RPC);
      return false;
    }

    if (type == TYPE_RPC) {
      setError(improv::ERROR_NONE);
      auto command = improv::parse_improv_data(&raw[9], dataLength, false);
      return parsePayload(command);
    }
  }

  return false; // not an RPC command
}

bool ImprovSerial::parsePayload(improv::ImprovCommand &command) {
  switch (command.command) {
    case improv::WIFI_SETTINGS: {
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(command.ssid.c_str(), command.password.c_str());
      setState(improv::STATE_PROVISIONING);
      _command.command = command.command;
      _command.ssid = command.ssid;
      _command.password = command.password;
      log_d("Received Improv wifi settings ssid=%s, password=%s", command.ssid.c_str(), command.password.c_str());
      return true;
    }
    case improv::GET_CURRENT_STATE:
      setState(_state);
      if (_state == improv::STATE_PROVISIONED) {
        std::vector<uint8_t> url = buildResponse(improv::GET_CURRENT_STATE);
        sendResponse(url);
      }
      return true;
    case improv::GET_DEVICE_INFO: {
      std::vector<uint8_t> info = buildVersionInfo();
      sendResponse(info);
      return true;
    }
    case improv::GET_WIFI_NETWORKS: {
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      delay(100);
      int32_t networkCount = WiFi.scanNetworks();
      for (int32_t i = 0; i < networkCount; i++) {
        std::vector<uint8_t> data = improv::build_rpc_response(
            improv::GET_WIFI_NETWORKS,
            {std::string(WiFi.SSID(i).c_str()), std::string(String(WiFi.RSSI(i)).c_str()), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "YES" : "NO"}, false);
        sendResponse(data);
      }
      std::vector<uint8_t> data =
          improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
      sendResponse(data);
      WiFi.mode(WIFI_AP);
      return true;
    }
    default: {
      log_w("Unknown Improv payload");
      setError(improv::ERROR_UNKNOWN_RPC);
      return false;
    }
  }
}

void ImprovSerial::setState(improv::State state) {
  _state = state;
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V', IMPROV_SERIAL_VERSION};
  data.resize(11);
  data[7] = TYPE_CURRENT_STATE;
  data[8] = 1;
  data[9] = state;

  uint8_t checksum = 0x00;
  for (uint8_t b : data) checksum += b;
  data[10] = checksum;
  writeData(data);
}

void ImprovSerial::setError(improv::Error error) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V', IMPROV_SERIAL_VERSION};
  data.resize(11);
  data[7] = TYPE_ERROR_STATE;
  data[8] = 1;
  data[9] = error;

  uint8_t checksum = 0x00;
  for (uint8_t b : data) checksum += b;
  data[10] = checksum;
  writeData(data);
}

void ImprovSerial::sendResponse(std::vector<uint8_t> &response) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V', IMPROV_SERIAL_VERSION};
  data.resize(9);
  data[7] = TYPE_RPC_RESPONSE;
  data[8] = response.size();
  data.insert(data.end(), response.begin(), response.end());

  uint8_t checksum = 0x00;
  for (uint8_t b : data) checksum += b;
  data.push_back(checksum);
  writeData(data);
}

void ImprovSerial::onWiFiConnectTimeout() {
  setError(improv::ERROR_UNABLE_TO_CONNECT);
  setState(improv::STATE_AUTHORIZED);
  log_w("Timed out trying to connect to network %s", _command.ssid.c_str());
  WiFi.disconnect();
}

ImprovSerial improvSerial;
