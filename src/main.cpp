#include "main.h"

void setup() {
  Serial.begin(115200);

  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_A_PIN);
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_B_PIN);
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN);

  esp_pm_config_esp32_t pm_config_ls_enable = {
    .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
    .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
    .light_sleep_enable = true
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

  similarMenuItems.reserve(16);
  spotifyDevices.reserve(10);
  spotifyUsers.reserve(10);
  spotifyPlaylists.reserve(100);
  SPIFFS.begin(true);
  tft.init();
  tft.setRotation(3);
  img.loadFont(FONT_NAME);
  ico.loadFont("icomoon24");
  batterySprite.loadFont("icomoon31");
  tft.fillScreen(TFT_BLACK);
  gpio_hold_en((gpio_num_t)TFT_BL);
  ledcSetup(TFT_BL, 5000, 8);

  if (bootCount == 0) {
    delay(1000);
    Serial.print("\n\r\n");
    Serial.flush();
    log_i("Boot #%d", bootCount);
    genreIndex = random(GENRE_COUNT);
    setMenuMode(AlphabeticList, genreIndex);
  } else {
    struct timeval tod;
    gettimeofday(&tod, NULL);
    time_t currentSeconds = tod.tv_sec;
    secondsAsleep = currentSeconds - lastSleepSeconds;
    log_i("Boot #%d, asleep for %ld seconds", bootCount, secondsAsleep);
    if (secondsAsleep > 60 * 10 ||
        (spotifyState.isPlaying &&
         (spotifyState.estimatedProgressMillis + secondsAsleep * 1000 > spotifyState.durationMillis))) {
      spotifyResetProgress();
      strcpy(spotifyState.contextName, "loading...");
    }
    nextCurrentlyPlayingMillis = 1;
  }
  bootCount++;

  // Restore battery level from RTC memory after sleep first
  if (lastBatteryVoltage > 0.0) knobby.setBatteryVoltage(lastBatteryVoltage);
  knobby.setup();

  ESP32Encoder::useInternalWeakPullResistors = UP;
  knob.attachFullQuad(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN);
  button.setDebounceTicks(debounceMillis);
  button.setClickTicks(doubleClickMaxMillis);
  button.setPressTicks(longPressMillis);
  button.attachClick(knobClicked);
  button.attachDoubleClick(knobDoubleClicked);
  button.attachLongPressStart(knobLongPressStarted);
  button.attachLongPressStop(knobLongPressStopped);

  readDataJson();

  if (configPassword.isEmpty()) {
    unsigned char randomBytes[8];
    for (auto i=0; i<8; i++) randomBytes[i] = random(256);
    configPassword = base64::encode((const uint8_t *)&randomBytes, 8).substring(0, 8);
    configPassword.toLowerCase();
    configPassword.replace('1', '!');
    configPassword.replace('l', '@');
    configPassword.replace('+', '?');
    configPassword.replace('/', '&');
    writeDataJson();
  }

  if (forceStartConfigPortalOnBoot || wifiSSID.isEmpty() || spotifyClientId.isEmpty() || spotifyClientSecret.isEmpty()) {
    if (forceStartConfigPortalOnBoot) {
      forceStartConfigPortalOnBoot = false;
    } else {
      log_w("Missing required configuration, starting portal.");
    }
    drawWifiSetup();
    disableCore1WDT();
    wifiManager = new ESPAsync_WiFiManager(&server, &dnsServer, nodeName.c_str());
    spotifyClientIdParam = new ESPAsync_WMParameter("spotifyClientId", "Spotify Client ID", spotifyClientId.c_str(), 40);
    spotifyClientSecretParam = new ESPAsync_WMParameter("spotifyClientSecret", "Spotify Client Secret", spotifyClientSecret.c_str(), 40);
    wifiManager->addParameter(spotifyClientIdParam);
    wifiManager->addParameter(spotifyClientSecretParam);
    wifiManager->setBreakAfterConfig(true);
    wifiManager->setConfigPortalTimeout(180);
    wifiManager->setSaveConfigCallback(saveAndSleep);
    if (spotifyClientId.isEmpty() || spotifyClientSecret.isEmpty()) {
      wifiManager->startConfigPortal("knobby", configPassword.c_str());
    } else {
      wifiManager->autoConnect("knobby", configPassword.c_str());
    }
    tft.fillScreen(TFT_BLACK);
  } else {
    log_i("Connecting to saved wifi SSID: %s...", wifiSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    WiFi.setHostname(nodeName.c_str());
  }

  Update.onProgress(onOTAProgress);
  ArduinoOTA.setHostname(nodeName.c_str());
  ArduinoOTA.setPassword(configPassword.c_str());
  ArduinoOTA.setTimeout(10000);
  ArduinoOTA.onStart([]() {
      int cmd = ArduinoOTA.getCommand();
      if (cmd == U_FLASH) {
        log_i("OTA: updating firmware");
      } else if (cmd == U_SPIFFS) {
        log_i("OTA: updating SPIFFS");
        SPIFFS.end();
      } else {
        log_e("OTA: unknown command %d", cmd);
      }
    })
    .onEnd([]() {
      log_i("OTA: update complete");
      lastInputMillis = millis();
    })
    .onError([](ota_error_t error) {
      log_e("OTA: error code %u", error);
      setStatusMessage("update failed");
    });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  // Initialize HTTP server handlers
  events.onConnect([](AsyncEventSourceClient *client) { log_i("[%d] events.onConnect", (int)millis()); });
  server.addHandler(&events);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    inactivityMillis = 1000 * 60 * 3;
    uint32_t ts = millis();
    log_i("[%d] server.on /", ts);
    if (spotifyUsers.empty()) {
      request->redirect("http://" + nodeName + ".local/authorize");
    } else {
      request->send(SPIFFS, "/index.html");
    }
  });

  server.on("/authorize", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    log_i("[%d] server.on /authorize", ts);
    spotifyGettingToken = true;
    const String authUrl =
        "https://accounts.spotify.com/authorize/?response_type=code&scope="
        "user-read-private+user-read-currently-playing+user-read-playback-state+"
        "user-modify-playback-state+playlist-read-private+"
        "user-library-read+user-follow-read+user-follow-modify"
        "&show_dialog=true&redirect_uri=http%3A%2F%2F" +
        nodeName + ".local%2Fcallback&client_id=" + spotifyClientId;
    request->redirect(authUrl);
  });

  server.on("/callback", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAuthCode = "";
    uint8_t paramsNr = request->params();
    for (uint8_t i = 0; i < paramsNr; i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "code") {
        spotifyAuthCode = p->value();
        spotifyAction = GetToken;
        spotifyGettingToken = true;
        break;
      }
    }
    if (spotifyAction == GetToken) {
      setStatusMessage("connecting");
      request->send(200, "text/plain", "Authorized! Getting token...");
    } else {
      request->send(204);
    }
  });

  server.on("/heap", HTTP_GET,
            [](AsyncWebServerRequest *request) { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    log_i("[%d] server.on /reset", ts);
    forceStartConfigPortalOnBoot = true;
    SPIFFS.remove("/data.json");
    request->send(200, "text/plain", "Tokens deleted, restarting.");
    delay(1000);
    WiFi.disconnect(true);
    gpio_hold_dis((gpio_num_t)TFT_BL);
    esp_sleep_enable_timer_wakeup(1000);
    esp_deep_sleep_start();
  });

  server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t ts = millis();
    log_i("[%d] server.on /sleep", ts);
    startDeepSleep();
  });

  server.on("/toggleevents", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendLogEvents = !sendLogEvents;
    request->send(200, "text/plain", sendLogEvents ? "1" : "0");
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      AsyncWebParameter *passwordParam = request->getParam("password", true);
      if (!passwordParam || passwordParam->value() != configPassword) {
        request->send(403, "text/plain", "Incorrect password");
      } else {
        request->send(400, "text/plain", "Missing update file");
      }
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
      AsyncWebParameter *passwordParam = request->getParam("password", true);
      if (!passwordParam || passwordParam->value() != configPassword) {
        request->send(403, "text/plain", "Incorrect password");
        return;
      }

      if (!index){
        updateContentLength = request->contentLength();
        int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
        if (cmd == U_SPIFFS) SPIFFS.end();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
          Update.printError(Serial);
        }
      }

      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }

      if (final) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Rebooting, please wait...");
        response->addHeader("Refresh", "20");
        response->addHeader("Location", "/");
        request->send(response);
        if (!Update.end(true)){
          updateContentLength = 0;
          Update.printError(Serial);
          setStatusMessage("update failed");
        } else {
          log_i("OTA: update complete");
          onOTAProgress(100, 100);
          Serial.flush();
          delay(100);
          ESP.restart();
        }
      }
    }
  );

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "404 Not Found");
  });

  server.begin();

  spotifyWifiClient.setCACert(spotifyCACertificate);
  spotifyHttp.setUserAgent("Knobby/1.0");
  spotifyHttp.setConnectTimeout(4000);
  spotifyHttp.setTimeout(4000);
  spotifyHttp.setReuse(true);

  bool holdingKnob = digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW;
  if (holdingKnob) {
    delay(10);
    holdingKnob = digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW;
  }

  if (spotifyNeedsNewAccessToken()) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  if (!spotifyClientId.isEmpty() && !spotifyClientSecret.isEmpty()) {
    xTaskCreatePinnedToCore(backgroundApiLoop, "backgroundApiLoop", 10000, NULL, 1, &backgroundApiTask, 1);
  }

  if (holdingKnob) {
    knobHeldForRandom = true;
    startRandomizingMenu(true);
  } else if (secondsAsleep == 0 || secondsAsleep > 60 * 40) {
    startRandomizingMenu(false);
  }
}

void loop() {
  uint32_t now = millis();
  unsigned long previousInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;

  if (knob.getCount() != lastKnobCount) knobRotated();
  button.tick();
  knobby.loop();
  lastBatteryVoltage = knobby.batteryVoltage();
  shutdownIfLowBattery();

  now = millis();
  unsigned long inputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  bool connected = WiFi.isConnected();

  if (inputDelta < inactivityMillis && previousInputDelta > inactivityMillis) {
    ledcDetachPin(TFT_BL);
    esp_pm_config_esp32_t pm_config_ls_enable = {
      .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
      .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
      .light_sleep_enable = true
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
    gpio_hold_en((gpio_num_t)TFT_BL);
    digitalWrite(TFT_BL, HIGH);
  } else if (inputDelta > inactivityMillis + inactivityFadeOutMillis) {
    startDeepSleep();
  } else if (inputDelta > inactivityMillis) {
    double fadeProgress = 1.0 - (inputDelta - inactivityMillis) / (double)inactivityFadeOutMillis;
    uint32_t duty = min(max((int)round(fadeProgress * 255.0), 0), 255);
    if (duty >= 250) {
      ledcAttachPin(TFT_BL, TFT_BL);
      esp_pm_config_esp32_t pm_config_ls_enable = {
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = false
      };
      ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
      gpio_hold_dis((gpio_num_t)TFT_BL);
    }
    ledcWrite(TFT_BL, duty);
  } else if (menuMode == VolumeControl && inputDelta > volumeMenuTimeoutMillis) {
    setMenuMode(NowPlaying, VolumeButton);
  }

  if (connected && (lastConnectedMillis < 0 || lastReconnectAttemptMillis > lastConnectedMillis)) {
    if (lastConnectedMillis >= 0) {
      setStatusMessage("reconnected", 1000);
    } else {
      setStatusMessage("", 0);
    }
    lastConnectedMillis = now;

    wifi_config_t current_conf;
    esp_wifi_get_config(WIFI_IF_STA, &current_conf);
    current_conf.sta.listen_interval = 5;
    esp_wifi_set_config(WIFI_IF_STA, &current_conf);

    if (wifiSSID.isEmpty()) {
      wifiSSID = WiFi.SSID();
      wifiPassword = WiFi.psk();
      writeDataJson();
    }
  } else if (!connected && now - lastReconnectAttemptMillis > 3000) {
    lastReconnectAttemptMillis = now;
    if (lastConnectedMillis < 0 && now > 5000) setStatusMessage("connecting to wifi", 1000);
    if (lastConnectedMillis < 0 && now >= wifiConnectTimeoutMillis) {
      log_w("[%d] No wifi after %d seconds, starting config portal.", now, (int)wifiConnectTimeoutMillis / 1000);
      forceStartConfigPortalOnBoot = true;
      gpio_hold_dis((gpio_num_t)TFT_BL);
      esp_sleep_enable_timer_wakeup(100);
      esp_deep_sleep_start();
    } else {
      WiFi.begin();
    }
  }

  if (connected && !spotifyGettingToken && spotifyNeedsNewAccessToken()) {
    spotifyGettingToken = true;
    spotifyAction = GetToken;
  }

  if (!knobHeldForRandom && shouldShowRandom() && getExtraLongPressedMillis() >= extraLongPressMillis) {
    knobHeldForRandom = true;
    longPressStartedMillis = 0;
    startRandomizingMenu(true);
  }

  if (spotifyState.isPlaying && now > spotifyState.lastUpdateMillis) {
    spotifyState.estimatedProgressMillis = spotifyState.progressMillis + (now - spotifyState.lastUpdateMillis);
  }

  if (!displayInvalidated) {
    if (statusMessage[0] != '\0' && now > statusMessageUntilMillis) {
      statusMessageUntilMillis = 0;
      statusMessage[0] = '\0';
      invalidateDisplay();
    } else if (clickEffectEndMillis > 0 && lastDisplayMillis > clickEffectEndMillis) {
      clickEffectEndMillis = 0;
      invalidateDisplay();
    } else if ((randomizingMenuEndMillis > 0 && now < randomizingMenuEndMillis) || lastInputMillis > lastDisplayMillis ||
               (spotifyState.isPlaying && now - lastDisplayMillis > 950) ||
               (shouldShowRandom() && lastDisplayMillis < longPressStartedMillis + extraLongPressMillis * 2)) {
      invalidateDisplay();
    }
  }

  if (displayInvalidated && updateContentLength == 0) {
    updateDisplay();
  } else {
    if (shouldShowProgressBar()) {
      showingProgressBar = true;
      tft.drawFastHLine(-now % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 20) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 40) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 60) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 80) % tft.width(), 0, 20, TFT_BLACK);
      tft.drawFastHLine(-(now + 100) % tft.width(), 0, 20, TFT_DARKGREY);
      tft.drawFastHLine(-(now + 120) % tft.width(), 0, 20, TFT_BLACK);
    } else if (showingProgressBar) {
      tft.drawFastHLine(0, 0, 239, TFT_BLACK);
      showingProgressBar = false;
    }
  }

  ArduinoOTA.handle();

  if ((spotifyAction == Idle || spotifyAction == CurrentlyPlaying) && inputDelta > 500 &&
      randomizingMenuEndMillis == 0 && !shouldShowRandom()) {
    delay(30);
  } else if (inputDelta > 500) {
    delay(10);
  } else {
    yield();
  }
}

void backgroundApiLoop(void *params) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t now = millis();

      switch (spotifyAction) {
        case Idle:
          break;
        case GetToken:
          if (spotifyAuthCode != "") {
            spotifyGetToken(spotifyAuthCode.c_str(), gt_authorization_code);
          } else if (spotifyRefreshToken[0] != '\0') {
            spotifyGetToken(spotifyRefreshToken, gt_refresh_token);
          } else {
            spotifyGettingToken = false;
            spotifyAction = Idle;
          }
          break;
        case CurrentlyPlaying:
          if (nextCurrentlyPlayingMillis > 0 && now >= nextCurrentlyPlayingMillis) {
            spotifyCurrentlyPlaying();
          }
          break;
        case CurrentProfile:
          spotifyCurrentProfile();
          break;
        case Next:
          spotifyNext();
          break;
        case Previous:
          spotifyPrevious();
          break;
        case Seek:
          spotifySeek();
          break;
        case Toggle:
          spotifyToggle();
          break;
        case PlayPlaylist:
          spotifyPlayPlaylist();
          break;
        case GetDevices:
          spotifyGetDevices();
          break;
        case SetVolume:
          if (spotifySetVolumeAtMillis > 0 && millis() >= spotifySetVolumeAtMillis) spotifySetVolume();
          break;
        case ToggleShuffle:
          spotifyToggleShuffle();
          break;
        case TransferPlayback:
          spotifyTransferPlayback();
          break;
        case GetPlaylistDescription:
          spotifyGetPlaylistDescription();
          break;
        case GetPlaylists:
          spotifyGetPlaylists();
          break;
      }
    }
    if (spotifyAction == Idle || spotifyAction == CurrentlyPlaying) {
      delay(50);
    } else {
      delay(10);
    }
  }
}

void knobRotated() {
  int newCount = knob.getCount();
  int knobDelta = newCount - lastKnobCount;
  int positionDelta = knobDelta / 4;
  if (positionDelta == 0) return;
  lastKnobCount = newCount;

  lastInputMillis = millis();
  if (button.isLongPressed()) knobRotatedWhileLongPressed = true;

  menuSize = checkMenuSize(menuMode);
  if (menuSize == 0) return;

  if (menuMode == VolumeControl) {
    int newMenuIndex = ((int)menuIndex + positionDelta);
    newMenuIndex = newMenuIndex < 0 ? 0 : min(newMenuIndex, menuSize - 1);
    setMenuIndex(newMenuIndex);
    spotifyAction = SetVolume;
    spotifySetVolumeTo = menuIndex;
    spotifySetVolumeAtMillis = lastInputMillis + 100;
  } else {
    int newMenuIndex = ((int)menuIndex + positionDelta) % (int)menuSize;
    if (newMenuIndex < 0) newMenuIndex += menuSize;
    setMenuIndex(newMenuIndex);
  }
}

void knobClicked() {
  lastInputMillis = millis();
  clickEffectEndMillis = lastInputMillis + clickEffectMillis;
  if (randomizingMenuEndMillis > 0) randomizingMenuEndMillis = 0;

  switch (menuMode) {
    case UserList:
      if (!spotifyUsers.empty() && activeSpotifyUser != &spotifyUsers[menuIndex]) {
        spotifyAction = Idle;
        spotifyTokenLifetime = 0;
        spotifyTokenSeconds = 0;
        spotifyAccessToken[0] = '\0';
        spotifyDevicesLoaded = false;
        spotifyDevices.clear();
        spotifyResetProgress();
        setActiveUser(&spotifyUsers[menuIndex]);
        writeDataJson();
        invalidateDisplay();
      }
      break;
    case DeviceList:
      if (spotifyDevicesLoaded && !spotifyDevices.empty() && activeSpotifyDevice != &spotifyDevices[menuIndex]) {
        setActiveDevice(&spotifyDevices[menuIndex]);
        writeDataJson();
        if (spotifyState.isPlaying && !spotifyGettingToken) {
          spotifyAction = TransferPlayback;
        }
      }
      break;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      playPlaylist(genrePlaylists[genreIndex]);
      playingGenreIndex = genreIndex;
      break;
    case SimilarList:
      if (!similarMenuItems.empty()) {
        playPlaylist(similarMenuItems[menuIndex].playlistId);
        playingGenreIndex = similarMenuGenreIndex;
      }
      break;
    case CountryList:
      playPlaylist(countryPlaylists[menuIndex], countries[menuIndex]);
      playingCountryIndex = lastMenuIndex;
      break;
    case PlaylistList:
      playPlaylist(spotifyPlaylists[menuIndex].id, spotifyPlaylists[menuIndex].name);
      break;
    case VolumeControl:
      spotifySetVolumeAtMillis = millis();
      setMenuMode(NowPlaying, VolumeButton);
      break;
    case NowPlaying:
      switch (menuIndex) {
        case VolumeButton:
          if (activeSpotifyDevice != nullptr) {
            setMenuMode(VolumeControl, activeSpotifyDevice->volumePercent);
          }
          break;
        case ShuffleButton:
          if (!spotifyState.disallowsTogglingShuffle) {
            spotifyAction = ToggleShuffle;
          }
          break;
        case BackButton:
          if (spotifyState.disallowsSkippingPrev || spotifyState.estimatedProgressMillis > 3000) {
            spotifySeekToMillis = 0;
            spotifyAction = Seek;
          } else {
            spotifyAction = Previous;
          }
          break;
        case PlayPauseButton:
          spotifyAction = Toggle;
          break;
        case NextButton:
          if (!spotifyState.disallowsSkippingNext) {
            spotifyAction = Next;
          }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (knobHeldForRandom || randomizingMenuEndMillis > 0) return;
  spotifyAction = Next;
  setStatusMessage("next");
}

void knobLongPressStarted() {
  lastInputMillis = millis();
  if (knobHeldForRandom || randomizingMenuEndMillis > 0) return;
  longPressStartedMillis = lastInputMillis;
  knobRotatedWhileLongPressed = false;
  if (menuMode != RootMenu) {
    lastMenuMode = menuMode;
    lastMenuIndex = menuIndex;
    if (isPlaylistMenu(menuMode)) lastPlaylistMenuMode = menuMode;
  }
  checkMenuSize(RootMenu);
  if (menuMode == NowPlaying) {
    switch (lastPlaylistMenuMode) {
      case SimilarList:
        setMenuMode(RootMenu, rootMenuSimilarIndex);
        break;
      case PlaylistList:
      case CountryList:
      case AlphabeticList:
      case AlphabeticSuffixList:
      case PopularityList:
        setMenuMode(RootMenu, (uint16_t)lastPlaylistMenuMode);
        break;
      default:
        setMenuMode(RootMenu, (uint16_t)menuMode);
        break;
    }
  } else if (menuMode == SimilarList) {
    setMenuMode(RootMenu, rootMenuSimilarIndex);
  } else if (spotifyState.isPlaying || menuMode == VolumeControl) {
    setMenuMode(RootMenu, rootMenuNowPlayingIndex);
  } else if (menuMode == UserList) {
    setMenuMode(RootMenu, rootMenuUsersIndex);
  } else {
    setMenuMode(RootMenu, (uint16_t)menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  if (knobHeldForRandom || randomizingMenuEndMillis > 0) {
    knobHeldForRandom = false;
    return;
  }
  checkMenuSize(RootMenu);
  if (shouldShowRandom()) {
    startRandomizingMenu();
  } else if (menuIndex == rootMenuSimilarIndex) {
    if (similarMenuGenreIndex == genreIndex) {
      setMenuMode(SimilarList, lastMenuMode == SimilarList ? lastMenuIndex : 0);
    } else {
      similarMenuItems.clear();
      if (isGenreMenu(lastMenuMode)) {
        similarMenuGenreIndex = genreIndex;
      } else if (playingGenreIndex >= 0) {
        similarMenuGenreIndex = playingGenreIndex;
      } else {
        log_e("missing genre index for similar menu");
        similarMenuGenreIndex = 0;
      }
      setMenuMode(SimilarList, 0);
      spotifyAction = GetPlaylistDescription;
    }
  } else if (menuIndex == rootMenuNowPlayingIndex) {
    nextCurrentlyPlayingMillis = lastInputMillis;
    setMenuMode(NowPlaying, lastMenuMode == NowPlaying ? lastMenuIndex : (uint16_t)PlayPauseButton);
    if (spotifyAction != GetToken) spotifyAction = CurrentlyPlaying;
  } else if (menuIndex == rootMenuUsersIndex) {
    if (!spotifyUsers.empty()) {
      uint16_t newMenuIndex = lastMenuMode == UserList ? lastMenuIndex : 0;
      auto usersCount = spotifyUsers.size();
      for (auto i = 1; i < usersCount; i++) {
        if (strcmp(spotifyUsers[i].refreshToken, spotifyRefreshToken) == 0) {
          newMenuIndex = i;
          break;
        }
      }
      if (spotifyUsers[newMenuIndex].name[0] == '\0') spotifyAction = CurrentProfile;
      setMenuMode(UserList, newMenuIndex);
    }
  } else {
    uint16_t newMenuIndex = lastMenuIndex;
    switch (menuIndex) {
      case DeviceList:
        if (lastMenuMode != DeviceList) newMenuIndex = 0;
        if (spotifyAction != GetToken && lastMenuMode != DeviceList) {
          spotifyDevicesLoaded = false;
          spotifyAction = GetDevices;
        }
        break;
      case PlaylistList:
        if (!spotifyPlaylistsLoaded) spotifyAction = GetPlaylists;
        if (lastMenuMode != PlaylistList) newMenuIndex = 0;
        if (spotifyState.playlistId[0] != '\0') {
          auto playlistsCount = spotifyPlaylists.size();
          for (auto i = 1; i < playlistsCount; i++) {
            if (strcmp(spotifyPlaylists[i].id, spotifyState.playlistId) == 0) {
              newMenuIndex = i;
              break;
            }
          }
        }
        break;
      case CountryList:
        if (playingCountryIndex >= 0) {
          newMenuIndex = playingCountryIndex;
        } else if (lastMenuMode != CountryList) {
          newMenuIndex = random(COUNTRY_COUNT);
        }
        break;
      case AlphabeticList:
      case AlphabeticSuffixList:
      case PopularityList:
        if (lastMenuMode == NowPlaying && playingGenreIndex >= 0) {
          newMenuIndex = getMenuIndexForGenreIndex(playingGenreIndex, (MenuModes)menuIndex);
        } else {
          newMenuIndex = getMenuIndexForGenreIndex(genreIndex, (MenuModes)menuIndex);
        }
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)menuIndex, newMenuIndex);
  }
  longPressStartedMillis = 0;
}

void drawBattery(unsigned int percent, unsigned int y) {
  const unsigned int batterySize = 31;
  batterySprite.setTextDatum(MC_DATUM);
  batterySprite.createSprite(batterySize, batterySize);
  batterySprite.setTextColor(TFT_DARKERGREY, TFT_BLACK);
  batterySprite.setCursor(1, 3);
  if (knobby.powerStatus() == PowerStatusOnBattery) {
    if (percent >= 85) {
      batterySprite.printToSprite(ICON_BATTERY_FULL);
    } else if (percent >= 60) {
      batterySprite.printToSprite(ICON_BATTERY_HIGH);
    } else if (percent >= 35) {
      batterySprite.printToSprite(ICON_BATTERY_MID);
    } else {
      batterySprite.setTextColor(TFT_RED, TFT_BLACK);
      batterySprite.printToSprite(ICON_BATTERY_LOW);
    }
  } else  {
    batterySprite.setTextColor(TFT_GREEN, TFT_BLACK);
    batterySprite.printToSprite(ICON_BATTERY_CHARGE);
  }
  tft.setCursor(centerX - batterySize / 2, y);
  batterySprite.pushSprite(tft.getCursorX(), tft.getCursorY());
  batterySprite.deleteSprite();
}

void shutdownIfLowBattery() {
  if (knobby.shouldUpdateBattery() || knobby.batteryVoltage() >= 3.2) return;
  tft.fillScreen(TFT_BLACK);
  drawBattery(0, 50);
  delay(333);
  tft.fillScreen(TFT_BLACK);
  delay(333);
  drawBattery(0, 50);
  delay(333);
  tft.fillScreen(TFT_BLACK);
  delay(333);
  drawBattery(0, 50);
  delay(333);
  startDeepSleep();
}

void drawDivider(bool selected) {
  int width = 110;
  if (selected) {
    width += 30;
    tft.drawFastHLine(centerX - width / 2 - 1, lineDivider, width, TFT_LIGHTGREY);
  } else {
    tft.drawFastHLine(textPadding, lineDivider, textWidth, TFT_BLACK);
    tft.drawFastHLine(centerX - width / 2 - 1, lineDivider, width, TFT_LIGHTBLACK);
  }
}

void drawWifiSetup() {
  tft.setCursor(textPadding, lineOne);
  drawCenteredText("join to setup", textWidth, 1);
  drawDivider(false);
  tft.setCursor(textPadding, lineTwo);
  drawCenteredText("wifi: knobby", textWidth, 1);
  tft.setCursor(textPadding, lineThree);
  String line = "pass: " + configPassword;
  drawCenteredText(line.c_str(), textWidth, 1);
}

void invalidateDisplay(bool eraseDisplay) {
  displayInvalidated = true;
  displayInvalidatedPartial = displayInvalidatedPartial && !eraseDisplay;
}

void updateDisplay() {
  displayInvalidated = false;
  if (!displayInvalidatedPartial) {
    tft.fillScreen(TFT_BLACK);
    displayInvalidatedPartial = true;
  }
  tft.setTextDatum(MC_DATUM);
  unsigned long now = millis();

  if (spotifyUsers.empty() || spotifyRefreshToken[0] == '\0') {
    tft.setCursor(textPadding, lineOne);
    drawCenteredText("spotify log in", textWidth);
    drawDivider(true);
    tft.setCursor(0, lineTwo);
    drawCenteredText(("http://" + nodeName + ".local").c_str(), 239);
  } else if (now < randomizingMenuEndMillis) {
    randomizingMenuTicks++;
    setMenuIndex(random(checkMenuSize(lastPlaylistMenuMode)));
    tft.setCursor(textPadding, lineTwo);
    if (isGenreMenu(lastPlaylistMenuMode)) {
      img.setTextColor(genreColors[genreIndex], TFT_BLACK);
      drawCenteredText(genres[genreIndex], textWidth, 3);
    } else if (lastPlaylistMenuMode == CountryList) {
      drawCenteredText(countries[menuIndex], textWidth, 3);
    } else if (lastPlaylistMenuMode == PlaylistList) {
      drawCenteredText(spotifyPlaylists[menuIndex].name, textWidth, 3);
    }
    auto endTickMillis = millis() + pow(randomizingMenuTicks, 3);
    while (millis() < endTickMillis) delay(10);
    if (millis() >= randomizingMenuEndMillis) {
      randomizingMenuEndMillis = 0;
      if (randomizingMenuAutoplay) {
        if (isGenreMenu(lastPlaylistMenuMode)) {
          playPlaylist(genrePlaylists[genreIndex]);
          playingGenreIndex = genreIndex;
        } else if (lastPlaylistMenuMode == CountryList) {
          playPlaylist(countryPlaylists[menuIndex], countries[menuIndex]);
          playingCountryIndex = lastMenuIndex;
        } else if (lastPlaylistMenuMode == PlaylistList) {
          playPlaylist(spotifyPlaylists[menuIndex].id, spotifyPlaylists[menuIndex].name);
        }
        displayInvalidatedPartial = true;
      }
    }
  } else if (menuMode == RootMenu) {
    tft.setCursor(textPadding, lineTwo);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    if (shouldShowRandom()) {
      double pressedProgress = min(1.0, (double)getExtraLongPressedMillis() / (double)extraLongPressMillis);
      tft.drawFastHLine(0, 0, (int)(pressedProgress * 239), TFT_WHITE);
      drawCenteredText("random", textWidth);
    } else {
      tft.drawFastHLine(0, 0, 239, TFT_BLACK);
      if (menuIndex == rootMenuSimilarIndex) {
        drawCenteredText(rootMenuItems[SimilarList], textWidth);
      } else if (menuIndex == rootMenuNowPlayingIndex) {
        drawCenteredText(rootMenuItems[NowPlaying], textWidth);
      } else if (menuIndex == rootMenuUsersIndex) {
        drawCenteredText(rootMenuItems[UserList], textWidth);
      } else {
        drawCenteredText(rootMenuItems[menuIndex], textWidth);
      }
    }

    drawBattery(knobby.batteryPercentage(), lineThree + 18);

    tft.drawRoundRect(9, lineTwo - 15, 221, 49, 5, TFT_WHITE);
  } else if (menuMode == UserList) {
    SpotifyUser_t *user = &spotifyUsers[menuIndex];
    bool selected = user == activeSpotifyUser;
    char header[14];
    sprintf(header, "%d / %d", menuIndex + 1, menuSize);
    tft.setCursor(textPadding, lineOne);
    img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
    drawCenteredText(header, textWidth);
    drawDivider(selected);

    if (user != nullptr) {
      tft.setCursor(textPadding, lineTwo);
      if (user->name[0] == '\0') {
        drawCenteredText("loading...", textWidth);
      } else {
        if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        drawCenteredText(user->name, textWidth, 3);
      }
    }
  } else if (menuMode == DeviceList) {
    if (spotifyDevicesLoaded && !spotifyDevices.empty()) {
      SpotifyDevice_t *device = &spotifyDevices[menuIndex];
      bool selected = device == activeSpotifyDevice;
      char header[14];
      sprintf(header, "%d / %d", menuIndex + 1, menuSize);
      tft.setCursor(textPadding, lineOne);
      img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
      drawCenteredText(header, textWidth);
      drawDivider(selected);

      if (device != nullptr) {
        tft.setCursor(textPadding, lineTwo);
        if (!spotifyDevicesLoaded || device->name[0] == '\0') {
          drawCenteredText("loading...", textWidth, 3);
        } else {
          if (strcmp(device->id, activeSpotifyDeviceId) == 0) img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          drawCenteredText(device->name, textWidth, 3);
        }
      }
    } else if (!spotifyDevicesLoaded || spotifyAction == GetDevices) {
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      drawCenteredText("loading...", textWidth, 3);
    } else if (spotifyDevicesLoaded && spotifyDevices.empty()) {
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      drawCenteredText("no devices found", textWidth, 3);
    }
  } else if (menuMode == VolumeControl) {
    uint8_t x = 10;
    uint8_t y = 30;
    uint8_t width = 220;
    img.createSprite(width, 82);
    img.fillSprite(TFT_BLACK);
    img.drawRoundRect(0, 0, width, 32, 5, TFT_WHITE);
    img.fillRoundRect(4, 4, round(2.12 * menuIndex), 24, 3, TFT_DARKGREY);
    char label[7];
    sprintf(label, "%d%%", menuIndex);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    img.drawString(label, width / 2 - img.textWidth(label) / 2, 48);
    tft.drawRect(0, 0, x - 1, y - 1, TFT_BLACK);
    img.pushSprite(x, y);
    img.deleteSprite();
  } else if (menuMode == NowPlaying) {
    tft.setTextDatum(ML_DATUM);
    tft.setCursor(textPadding, lineOne);
    img.createSprite(90, img.gFont.yAdvance);
    if (now < statusMessageUntilMillis && statusMessage[0] != '\0') {
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      img.printToSprite(String(statusMessage));
    } else {
      char elapsed[11];
      if (spotifyState.durationMillis == 0) {
        formatMillis(elapsed, 0);
      } else {
        formatMillis(elapsed, spotifyState.estimatedProgressMillis);
      }
      img.setTextColor(spotifyState.isPlaying ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
      img.printToSprite(String(elapsed));
    }
    img.pushSprite(tft.getCursorX(), tft.getCursorY());
    img.deleteSprite();

    ico.setTextDatum(MC_DATUM);
    const int width = ICON_SIZE + 2;
    const int height = ICON_SIZE;
    ico.createSprite(width, height);

    const uint16_t bg = TFT_BLACK;
    const uint16_t fg = TFT_LIGHTGREY;
    const uint16_t disabled = TFT_DARKGREY;

    bool nextClicked = menuIndex == NextButton && spotifyAction == Next && now < clickEffectEndMillis;
    tft.setCursor(TFT_HEIGHT - textPadding - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, nextClicked ? fg : bg);
    if (nextClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsSkippingNext ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SKIP_NEXT);
    if (menuIndex == NextButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    bool toggleClicked = menuIndex == PlayPauseButton && spotifyAction == Toggle && now < clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, toggleClicked ? fg : bg);
    if (toggleClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(spotifyState.isPlaying ? ICON_PAUSE : ICON_PLAY_ARROW);
    if (menuIndex == PlayPauseButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    bool backClicked = menuIndex == BackButton && (spotifyAction == Previous || (spotifyAction == Seek && spotifySeekToMillis == 0)) && now < clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillRoundRect(0, 0, width, height, 3, backClicked ? fg : bg);
    if (backClicked) {
      ico.setTextColor(bg, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsSkippingPrev ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SKIP_PREVIOUS);
    if (menuIndex == BackButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillSprite(bg);
    if (spotifyState.isShuffled) {
      ico.setTextColor(bg, fg);
      ico.fillRoundRect(2, 2, width - 4, height - 4, 3, fg);
    } else {
      ico.setTextColor(spotifyState.disallowsTogglingShuffle ? disabled : fg, bg);
    }
    ico.setCursor(1, 3);
    ico.printToSprite(ICON_SHUFFLE);
    if (menuIndex == ShuffleButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    tft.setCursor(tft.getCursorX() - width, lineOne - 2);
    ico.fillSprite(bg);
    ico.setTextColor(activeSpotifyDevice == nullptr ? disabled : fg, bg);
    ico.setCursor(1, 3);
    String volumeIcon;
    if (activeSpotifyDevice != nullptr) {
      if (activeSpotifyDevice->volumePercent > 50) {
        volumeIcon = ICON_VOLUME_UP;
      } else if (activeSpotifyDevice->volumePercent >= 10) {
        volumeIcon = ICON_VOLUME_DOWN;
      } else {
        volumeIcon = ICON_VOLUME_MUTE;
      }
    } else {
      volumeIcon = ICON_VOLUME_MUTE;
    }
    ico.printToSprite(volumeIcon);
    if (menuIndex == VolumeButton) ico.drawRoundRect(0, 0, width, height, 3, fg);
    ico.pushSprite(tft.getCursorX(), tft.getCursorY());

    ico.deleteSprite();
    tft.drawFastHLine(textPadding, lineDivider, textWidth, TFT_LIGHTBLACK);

    if (millis() - nowPlayingDisplayMillis >= 50) {
      tft.setTextDatum(MC_DATUM);
      tft.setCursor(textPadding, lineTwo);
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
      bool isActivePlaylist =
          playingGenreIndex >= 0 &&
          (strcmp(genrePlaylists[playingGenreIndex], spotifyState.playlistId) == 0 ||
           (spotifyPlayPlaylistId != nullptr && strcmp(genrePlaylists[playingGenreIndex], spotifyPlayPlaylistId) == 0));
      if (isActivePlaylist &&
          (spotifyState.isPlaying || spotifyPlayPlaylistId != nullptr || spotifyAction == Previous || spotifyAction == Next) &&
          (spotifyState.durationMillis == 0 || spotifyState.estimatedProgressMillis % 6000 > 3000)) {
        img.setTextColor(genreColors[playingGenreIndex], TFT_BLACK);
        drawCenteredText(genres[playingGenreIndex], textWidth, 3);
      } else if (spotifyState.contextName[0] != '\0' &&
                 (spotifyState.durationMillis == 0 || spotifyState.estimatedProgressMillis % 6000 > 3000)) {
        if (spotifyAction == PlayPlaylist && now < clickEffectEndMillis) img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        drawCenteredText(spotifyState.contextName, textWidth, 3);
      } else if (spotifyState.artistName[0] != '\0' && spotifyState.name[0] != '\0') {
        char playing[201];
        snprintf(playing, sizeof(playing) - 1, "%s – %s", spotifyState.artistName, spotifyState.name);
        if (playingGenreIndex >= 0) img.setTextColor(genreColors[playingGenreIndex], TFT_BLACK);
        drawCenteredText(playing, textWidth, 3);
      } else if (spotifyApiRequestStartedMillis < 0 && !spotifyState.isPlaying) {
        img.setTextColor(TFT_LIGHTBLACK, TFT_BLACK);
        drawCenteredText("- none -", textWidth, 3);
      }
      nowPlayingDisplayMillis = millis();
    }
  } else {
    const char *text;
    bool selected = playingGenreIndex == genreIndex;

    if (menuMode == CountryList) {
      text = countries[menuIndex];
      selected = playingCountryIndex == menuIndex;
    } else if (menuMode == PlaylistList) {
      if (!spotifyPlaylistsLoaded) {
        text = "loading...";
      } else if (spotifyPlaylists.empty()) {
        text = "no playlists yet";
      } else {
        text = spotifyPlaylists[menuIndex].name;
        selected = strcmp(spotifyState.playlistId, spotifyPlaylists[menuIndex].id) == 0;
      }
    } else if (menuMode == SimilarList) {
      if (similarMenuItems.empty()) {
        text = "loading...";
      } else if (similarMenuItems[menuIndex].name[0] != '\0') {
        text = similarMenuItems[menuIndex].name;
        selected = strcmp(spotifyState.playlistId, similarMenuItems[menuIndex].playlistId) == 0;
      } else {
        text = genres[genreIndex];
      }
    } else {
      text = genres[genreIndex];
    }

    tft.setCursor(textPadding, lineOne);
    if (now < statusMessageUntilMillis && statusMessage[0] != '\0') {
      img.setTextColor(TFT_WHITE, TFT_BLACK);
      drawCenteredText(statusMessage, textWidth);
    } else if (menuSize > 0) {
      char label[14];
      sprintf(label, "%d / %d", menuIndex + 1, menuSize);
      img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
      tft.setCursor(textPadding, lineOne);
      drawCenteredText(label, textWidth);
      drawDivider(selected);
    }

    if (menuMode == PlaylistList || menuMode == CountryList) {
      img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
    } else if (menuMode == SimilarList && similarMenuItems[menuIndex].name[0] != '\0') {
      img.setTextColor(genreColors[similarMenuGenreIndex], TFT_BLACK);
    } else if (menuSize == 0) {
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    } else {
      img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    }

    tft.setCursor(textPadding, lineTwo);
    drawCenteredText(text, textWidth, 3);
  }
  lastDisplayMillis = millis();
}

void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines) {
  const uint16_t lineHeight = img.gFont.yAdvance + lineSpacing;
  const uint16_t centerX = round(maxWidth / 2.0);
  const uint16_t len = strlen(text);

  uint16_t lineNumber = 0;
  uint16_t pos = 0;
  int16_t totalWidth = 0;
  uint16_t index = 0;
  uint16_t preferredBreakpoint = 0;
  uint16_t widthAtBreakpoint = 0;
  bool breakpointOnSpace = false;
  uint16_t lastDrawnPos = 0;

  img.createSprite(maxWidth, img.gFont.yAdvance);

  while (pos < len) {
    uint16_t lastPos = pos;
    uint16_t unicode = img.decodeUTF8((uint8_t *)text, &pos, len - pos);
    int16_t width = 0;

    if (img.getUnicodeIndex(unicode, &index)) {
      if (pos == 0) width = -img.gdX[index];
      if (pos == len - 1) {
        width = (img.gWidth[index] + img.gdX[index]);
      } else {
        width = img.gxAdvance[index];
      }
    } else {
      width = img.gFont.spaceWidth + 1;
    }
    totalWidth += width;

    // Always try to break on a space or dash
    if (unicode == ' ') {
      preferredBreakpoint = pos;
      widthAtBreakpoint = totalWidth - width;
      breakpointOnSpace = true;
    } else if (unicode == '-' || unicode == '/') {
      preferredBreakpoint = pos;
      widthAtBreakpoint = totalWidth;
      breakpointOnSpace = false;
    }

    if (totalWidth >= maxWidth - width) {
      if (preferredBreakpoint == 0) {
        preferredBreakpoint = lastPos;
        widthAtBreakpoint = totalWidth;
        breakpointOnSpace = false;
      }
      uint16_t lineLength = preferredBreakpoint - lastDrawnPos;
      uint16_t lineWidth = widthAtBreakpoint;

      char line[lineLength + 1] = {0};
      strncpy(line, &text[lastDrawnPos], lineLength);

      img.setCursor(centerX - round(lineWidth / 2.0), 0);
      img.printToSprite(line, lineLength);
      img.pushSprite(tft.getCursorX(), tft.getCursorY());
      tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);

      lastDrawnPos = preferredBreakpoint;
      // It is possible that we did not draw all letters to n so we need
      // to account for the width of the chars from `n - preferredBreakpoint`
      // by calculating the width we did not draw yet.
      totalWidth = totalWidth - widthAtBreakpoint;
      if (breakpointOnSpace) totalWidth -= img.gFont.spaceWidth + 1;
      preferredBreakpoint = 0;
      lineNumber++;
      if (lineNumber >= maxLines) break;
      img.fillSprite(TFT_BLACK);
    }
  }

  // Draw last part if needed
  if (lastDrawnPos < len && lineNumber < maxLines) {
    uint16_t lineLength = len - lastDrawnPos;
    char line[lineLength + 1] = {0};
    strncpy(line, &text[lastDrawnPos], lineLength);
    img.setCursor(centerX - round(totalWidth / 2.0), 0);
    img.printToSprite(line, lineLength);
    img.pushSprite(tft.getCursorX(), tft.getCursorY());
    tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);
    lineNumber++;
  }

  img.deleteSprite();

  while (lineNumber < maxLines) {
    tft.fillRect(tft.getCursorX(), tft.getCursorY(), maxWidth, lineHeight, TFT_BLACK);
    tft.setCursor(tft.getCursorX(), tft.getCursorY() + lineHeight);
    lineNumber++;
  }
}

int getGenreIndexByName(const char *genreName) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    if (strcmp(genreName, genres[i]) == 0) return i;
  }
  return -1;
}

int getGenreIndexByPlaylistId(const char *playlistId) {
  for (size_t i = 0; i < GENRE_COUNT; i++) {
    int result = strncmp(playlistId, genrePlaylists[i], SPOTIFY_ID_SIZE);
    if (result == 0) return i;
  }
  return -1;
}

// is genreIndex tracked in this menu?
bool isGenreMenu(MenuModes mode) {
  return mode == AlphabeticList || mode == AlphabeticSuffixList || mode == PopularityList || mode == SimilarList;
}

bool isPlaylistMenu(MenuModes mode) {
  return isGenreMenu(mode) || mode == PlaylistList || mode == CountryList;
}

unsigned long getLongPressedMillis() {
  return longPressStartedMillis == 0 ? 0 : millis() - longPressStartedMillis;
}

unsigned long getExtraLongPressedMillis() {
  long extraMillis = (long)getLongPressedMillis() - extraLongPressMillis;
  return extraMillis < 0 ? 0 : extraMillis;
}

bool shouldShowProgressBar() {
  return spotifyAction == GetToken || spotifyAction == PlayPlaylist || spotifyAction == GetDevices ||
         spotifyAction == GetPlaylists || spotifyAction == GetPlaylistDescription ||
         (spotifyApiRequestStartedMillis > 0 && millis() - spotifyApiRequestStartedMillis > 950);
}

bool shouldShowRandom() {
  if (randomizingMenuEndMillis > 0 || knobRotatedWhileLongPressed) return false;
  return (knobHeldForRandom || getLongPressedMillis() > extraLongPressMillis) &&
         (isPlaylistMenu(lastMenuMode) || lastMenuMode == NowPlaying || lastMenuMode == VolumeControl);
}

bool shouldShowSimilarMenu() {
  return isGenreMenu(lastMenuMode) || playingGenreIndex >= 0;
}

bool shouldShowUsersMenu() {
  return spotifyUsers.size() > 1;
}

uint16_t checkMenuSize(MenuModes mode) {
  int nextDynamicIndex = SimilarList;

  switch (mode) {
    case RootMenu:
      rootMenuSimilarIndex = shouldShowSimilarMenu() ? nextDynamicIndex++ : -1;
      rootMenuNowPlayingIndex = nextDynamicIndex++;
      rootMenuUsersIndex = shouldShowUsersMenu() ? nextDynamicIndex++ : -1;
      return nextDynamicIndex;
    case UserList:
      return spotifyUsers.size();
    case DeviceList:
      return spotifyDevices.size();
    case PlaylistList:
      return spotifyPlaylists.size();
    case CountryList:
      return COUNTRY_COUNT;
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      return GENRE_COUNT;
    case SimilarList:
      return similarMenuItems.size();
    case VolumeControl:
      return 101; // 0-100%
    case NowPlaying:
      return 5; // volume, shuffle, previous, play/pause, next
    default:
      return 0;
  }
}

uint16_t getGenreIndexForMenuIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case AlphabeticList:
      return index;
    case AlphabeticSuffixList:
      return genreIndexes_suffix[index];
    case PopularityList:
      return genreIndexes_popularity[index];
    case PlaylistList:
      return max(0, getGenreIndexByPlaylistId(spotifyPlaylists[index].id));
    case SimilarList:
      return max(0, getGenreIndexByPlaylistId(similarMenuItems[index].playlistId));
    default:
      return 0;
  }
}

uint16_t getIndexOfGenreIndex(const uint16_t indexes[], uint16_t indexToFind) {
  for (uint16_t i = 0; i < GENRE_COUNT; i++) {
    if (indexes[i] == indexToFind) return i;
  }
  return 0;
}

uint16_t getMenuIndexForGenreIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case AlphabeticList:
      return index;
    case AlphabeticSuffixList:
      return getIndexOfGenreIndex(genreIndexes_suffix, index);
    case PopularityList:
      return getIndexOfGenreIndex(genreIndexes_popularity, index);
    default:
      return 0;
  }
}

void setMenuIndex(uint16_t newMenuIndex) {
  menuIndex = menuSize == 0 ? 0 : newMenuIndex % menuSize;

  int newGenreIndex = -1;
  switch (menuMode) {
    case AlphabeticList:
    case AlphabeticSuffixList:
    case PopularityList:
      if (menuSize > 0) genreIndex = getGenreIndexForMenuIndex(menuIndex, menuMode);
      break;
    case SimilarList:
      if (menuSize > 0) {
        if (similarMenuItems[menuIndex].name[0] == '\0') {
          newGenreIndex = getGenreIndexByPlaylistId(similarMenuItems[menuIndex].playlistId);
          if (newGenreIndex < 0) {
            genreIndex = similarMenuGenreIndex;
          } else {
            genreIndex = newGenreIndex;
          }
        } else {
          genreIndex = max(0, similarMenuGenreIndex);
        }
      }
      break;
    default:
      break;
  }
  invalidateDisplay();
}

void setMenuMode(MenuModes newMode, uint16_t newMenuIndex) {
  MenuModes oldMode = menuMode;
  menuMode = newMode;
  menuSize = checkMenuSize(newMode);
  setMenuIndex(newMenuIndex);
  invalidateDisplay(oldMode != newMode);
}

void setStatusMessage(const char *message, unsigned long durationMs) {
  strncpy(statusMessage, message, sizeof(statusMessage) - 1);
  statusMessageUntilMillis = millis() + durationMs;
  invalidateDisplay();
}

void saveAndSleep() {
  forceStartConfigPortalOnBoot = false;
  spotifyClientId = spotifyClientIdParam->getValue();
  spotifyClientSecret = spotifyClientSecretParam->getValue();
  wifiSSID = wifiManager->WiFi_SSID();
  wifiPassword = wifiManager->WiFi_Pass();
  writeDataJson();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(100);
  esp_deep_sleep_start();
}

void startDeepSleep() {
  // don't sleep on transient menus
  if (menuMode == SimilarList || menuMode == PlaylistList || (!isPlaylistMenu(menuMode) && menuMode != NowPlaying)) setMenuMode(AlphabeticList, genreIndex);
  struct timeval tod;
  gettimeofday(&tod, NULL);
  lastSleepSeconds = tod.tv_sec;
  spotifyAction = Idle;
  tft.fillScreen(TFT_BLACK);
  log_i("[%d] Entering deep sleep.", (uint32_t)millis());
  WiFi.disconnect(true);
  gpio_hold_dis((gpio_num_t)TFT_BL);
  digitalWrite(TFT_BL, LOW);
  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(TFT_SLPIN);
  rtc_gpio_isolate(GPIO_NUM_0); // button 1
  rtc_gpio_isolate(GPIO_NUM_35); // button 2
  rtc_gpio_isolate(GPIO_NUM_39);
  rtc_gpio_isolate((gpio_num_t)ADC_PIN);
  rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_A_PIN);
  rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_B_PIN);
  adc_power_off();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN, LOW);
  esp_deep_sleep_disable_rom_logging();
  esp_deep_sleep_start();
}

void startRandomizingMenu(bool autoplay) {
  unsigned long now = millis();
  if (now > randomizingMenuEndMillis) {
    randomizingMenuEndMillis = now + 850;
    randomizingMenuTicks = 0;
    randomizingMenuAutoplay = autoplay;
    if (randomizingMenuAutoplay) {
      if (spotifyAction != GetToken) spotifyAction = Idle;
      spotifyResetProgress();
    }
    setMenuMode(lastPlaylistMenuMode, 0);
  }
}

void playPlaylist(const char *playlistId, const char *name) {
  spotifyResetProgress();
  spotifyState.isPlaying = true;
  spotifyPlayPlaylistId = playlistId;
  strncpy(spotifyState.playlistId, playlistId, SPOTIFY_ID_SIZE);
  strncpy(spotifyState.contextName, name, sizeof(spotifyState.contextName) - 1);
  if (!spotifyGettingToken && spotifyAction != GetToken) spotifyAction = PlayPlaylist;

  lastMenuMode = menuMode;
  lastMenuIndex = menuIndex;
  if (isPlaylistMenu(menuMode)) lastPlaylistMenuMode = menuMode;
  setMenuMode(NowPlaying, PlayPauseButton);
  setStatusMessage("play");
}

int formatMillis(char *output, unsigned long millis) {
  unsigned int seconds = millis / 1000 % 60;
  unsigned int minutes = millis / (1000 * 60) % 60;
  unsigned int hours = millis / (1000 * 60 * 60);

  if (hours == 0) {
    return sprintf(output, "%d:%02d", minutes, seconds);
  } else {
    return sprintf(output, "%d:%02d:%02d", hours, minutes, seconds);
  }
}

void eventsSendLog(const char *logData, EventsLogTypes type) {
  if (!sendLogEvents) return;
  events.send(logData, type == log_line ? "line" : "raw");
}

void setActiveUser(SpotifyUser_t *user) {
  activeSpotifyUser = user;
  strncpy(spotifyRefreshToken, activeSpotifyUser->refreshToken, sizeof(spotifyRefreshToken) - 1);
  if (user->selectedDeviceId[0] != '\0')
    strncpy(activeSpotifyDeviceId, user->selectedDeviceId, sizeof(activeSpotifyDeviceId) - 1);

  SpotifyDevice_t *activeDevice = nullptr;
  for (SpotifyDevice_t device : spotifyDevices) {
    if (strcmp(device.id, activeSpotifyDeviceId) == 0) {
      activeDevice = &device;
      break;
    }
  }
  setActiveDevice(activeDevice);
}

void setActiveDevice(SpotifyDevice_t *device) {
  activeSpotifyDevice = device;
  if (activeSpotifyDevice != nullptr) {
    strncpy(activeSpotifyDeviceId, activeSpotifyDevice->id, sizeof(activeSpotifyDeviceId) - 1);
    strncpy(activeSpotifyUser->selectedDeviceId, activeSpotifyDevice->id, sizeof(activeSpotifyUser->selectedDeviceId) - 1);
  }
}

bool readDataJson() {
  File f = SPIFFS.open("/data.json", "r");
  DynamicJsonDocument doc(5000);
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (error) {
    log_e("Failed to read data.json: %s", error.c_str());
    return false;
#ifdef DEBUG
  } else {
    Serial.print("Loading data.json: ");
    serializeJson(doc, Serial);
    Serial.println();
#endif
  }

  configPassword = doc["configPassword"] | "";
  spotifyClientId = doc["spotifyClientId"] | "";
  spotifyClientSecret = doc["spotifyClientSecret"] | "";
  wifiSSID = doc["wifiSSID"] | WiFi.SSID();
  wifiPassword = doc["wifiPassword"] | WiFi.psk();

  JsonArray usersArray = doc["users"];
  spotifyUsers.clear();
  for (JsonObject jsonUser : usersArray) {
    const char *name = jsonUser["name"];
    const char *token = jsonUser["token"];
    const char *country = jsonUser["country"];
    const char *selectedDeviceId = jsonUser["selectedDeviceId"];

    SpotifyUser_t user;
    strncpy(user.name, name, sizeof(user.name) - 1);
    strncpy(user.refreshToken, token, sizeof(user.refreshToken) - 1);
    strncpy(user.country, country, sizeof(user.country) - 1);
    strncpy(user.selectedDeviceId, selectedDeviceId, sizeof(user.selectedDeviceId) - 1);
    spotifyUsers.push_back(user);

    if (jsonUser["selected"].as<bool>() == true) setActiveUser(&spotifyUsers.back());
  }

  if (activeSpotifyUser == nullptr && !spotifyUsers.empty()) setActiveUser(&spotifyUsers[0]);

  return true;
}

bool writeDataJson() {
  File f = SPIFFS.open("/data.json", "w+");
  DynamicJsonDocument doc(5000);

  doc["configPassword"] = configPassword;
  doc["spotifyClientId"] = spotifyClientId;
  doc["spotifyClientSecret"] = spotifyClientSecret;
  doc["wifiSSID"] = wifiSSID;
  doc["wifiPassword"] = wifiPassword;

  JsonArray usersArray = doc.createNestedArray("users");

  for (SpotifyUser_t user : spotifyUsers) {
    JsonObject obj = usersArray.createNestedObject();
    obj["name"] = user.name;
    obj["token"] = user.refreshToken;
    obj["country"] = user.country;
    obj["selectedDeviceId"] = user.selectedDeviceId;
    obj["selected"] = (bool)(strcmp(user.refreshToken, spotifyRefreshToken) == 0);
  }

  size_t bytes = serializeJson(doc, f);
  f.close();
  if (bytes <= 0) {
    log_e("Failed to write data.json: %d", bytes);
    return false;
  }
#ifdef DEBUG
  Serial.print("Saving data.json: ");
  serializeJson(doc, Serial);
  Serial.println();
#endif
  return true;
}

void onOTAProgress(unsigned int progress, unsigned int total) {
  if (progress == 0) {
    inactivityMillis = 1000 * 60 * 5;
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(textPadding, lineOne);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    drawCenteredText("updating", textWidth, 1);
    drawDivider(true);
  }
  char status[5];
  sprintf(status, "%u%%", (progress / (total / 100)));
  tft.setCursor(textPadding, lineTwo);
  drawCenteredText(status, textWidth, 1);
  esp_task_wdt_reset();
}

HTTP_response_t spotifyApiRequest(const char *method, const char *endpoint, const char *content = "") {
  uint32_t ts = millis();
  spotifyApiRequestStartedMillis = ts;
  String path = String("/v1/") + endpoint;
  log_i("[%d] %s %s %s", ts, method, path.c_str(), content);

  spotifyHttp.begin(spotifyWifiClient, "api.spotify.com", 443, path);
  spotifyHttp.addHeader("Authorization", "Bearer " + String(spotifyAccessToken));
  if (strlen(content) == 0) spotifyHttp.addHeader("Content-Length", "0");

  StreamString payload;
  int code;
  if (String(method) == "GET") {
    code = spotifyHttp.GET();
  } else {
    code = spotifyHttp.sendRequest(method, content);
  }

  if (code == 401) {
    log_e("401 Unauthorized, clearing spotifyAccessToken");
    spotifyAccessToken[0] = '\0';
  } else if (code == 204 || spotifyHttp.getSize() == 0) {
    // log_e("empty response, returning");
  } else {
    if (!payload.reserve(spotifyHttp.getSize() + 1)) {
      log_e("not enough memory to reserve a string! need: %d", (spotifyHttp.getSize() + 1));
    }
    spotifyHttp.writeToStream(&payload);
  }
  spotifyHttp.end();
  spotifyApiRequestStartedMillis = -1;

  HTTP_response_t response = {code, payload};
  return response;
}

bool spotifyNeedsNewAccessToken() {
  if (spotifyAccessToken[0] == '\0') return true;
  struct timeval tod;
  gettimeofday(&tod, NULL);
  time_t currentSeconds = tod.tv_sec;
  time_t spotifyTokenAge = spotifyTokenSeconds == 0 ? 0 : currentSeconds - spotifyTokenSeconds;
  return spotifyTokenAge >= spotifyTokenLifetime;
}

/**
 * Get Spotify token
 *
 * @param code          Either an authorization code or a refresh token
 * @param grant_type    [gt_authorization_code|gt_refresh_token]
 */
void spotifyGetToken(const char *code, GrantTypes grant_type) {
  uint32_t ts = millis();
  const char *method = "POST";
  const char *path = "/api/token";
  char requestContent[768];
  bool success = false;

  if (grant_type == gt_authorization_code) {
    snprintf(requestContent, sizeof(requestContent),
             "grant_type=authorization_code&redirect_uri=http%%3A%%2F%%2F%s.local%%2Fcallback&code=%s",
             nodeName.c_str(), code);
  } else {
    snprintf(requestContent, sizeof(requestContent),
             "grant_type=refresh_token&refresh_token=%s",
             code);
  }
  log_i("[%d] %s %s", ts, method, path);

  HTTPClient http;
  StreamString payload;
  http.setUserAgent("Knobby/1.0");
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  http.setReuse(false);
  http.begin(spotifyWifiClient, "accounts.spotify.com", 443, path);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setAuthorization(spotifyClientId.c_str(), spotifyClientSecret.c_str());

  int httpCode = http.sendRequest(method, (uint8_t *)requestContent, strlen(requestContent));
  if (httpCode == 401) {
    log_e("401 Unauthorized, clearing spotifyAccessToken");
    spotifyAccessToken[0] = '\0';
  } else if (httpCode == 204 || http.getSize() == 0) {
    log_e("empty response, returning");
  } else {
    if (!payload.reserve(http.getSize() + 1)) {
      log_e("not enough memory to reserve a string! need: %d", (http.getSize() + 1));
    }
    http.writeToStream(&payload);
  }
  http.end();

  if (httpCode == 200) {
    DynamicJsonDocument json(1536);
    DeserializationError error = deserializeJson(json, payload.c_str());

    if (!error) {
      spotifyAuthCode = "";
      strncpy(spotifyAccessToken, json["access_token"], sizeof(spotifyAccessToken) - 1);
      if (spotifyAccessToken[0] != '\0') {
        spotifyTokenLifetime = (json["expires_in"].as<uint32_t>() - 300);
        struct timeval tod;
        gettimeofday(&tod, NULL);
        spotifyTokenSeconds = tod.tv_sec;
        success = true;
        if (json.containsKey("refresh_token")) {
          const char *newRefreshToken = json["refresh_token"];
          if (strcmp(newRefreshToken, spotifyRefreshToken) != 0) {
            strncpy(spotifyRefreshToken, newRefreshToken, sizeof(spotifyRefreshToken) - 1);
            if (grant_type == gt_authorization_code) {
              SpotifyUser_t user;
              strncpy(user.refreshToken, spotifyRefreshToken, sizeof(user.refreshToken) - 1);
              user.selected = true;
              spotifyUsers.push_back(user);
              setActiveUser(&spotifyUsers.back());
              activeSpotifyDevice = nullptr;
              activeSpotifyDeviceId[0] = '\0';
              spotifyDevicesLoaded = false;
              spotifyDevices.clear();
            } else if (grant_type == gt_refresh_token && activeSpotifyUser != nullptr &&
                       strcmp(activeSpotifyUser->refreshToken, spotifyRefreshToken) != 0) {
              strncpy(activeSpotifyUser->refreshToken, spotifyRefreshToken, sizeof(activeSpotifyUser->refreshToken));
              writeDataJson();
            }
          }
        }
      }
    } else {
      log_e("[%d] Unable to parse response payload: %s", (int)millis(), payload.c_str());
      delay(4000);
    }
  } else if (httpCode < 0) {
    // retry immediately
  } else {
    log_e("[%d] %d - %s", (int)millis(), httpCode, payload.c_str());
    Serial.flush();
    inactivityMillis = 15000;
    delay(8000);
  }

  if (success) {
    if (grant_type == gt_authorization_code) {
      spotifyAction = CurrentProfile;
    } else if (menuMode == DeviceList) {
      spotifyAction = GetDevices;
    } else if (menuMode == PlaylistList) {
      spotifyAction = GetPlaylists;
    } else if (spotifyPlayPlaylistId != nullptr) {
      spotifyAction = PlayPlaylist;
    } else {
      spotifyAction = CurrentlyPlaying;
    }
  }
  spotifyGettingToken = false;
}

void spotifyCurrentlyPlaying() {
  nextCurrentlyPlayingMillis = 0;
  if (spotifyAccessToken[0] == '\0' || !activeSpotifyUser) return;
  uint32_t ts = millis();

  char url[21];
  sprintf(url, "me/player?market=%s", activeSpotifyUser->country);
  HTTP_response_t response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(9000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());

    if (!error) {
      spotifyState.lastUpdateMillis = millis();
      spotifyState.isPlaying = json["is_playing"];
      spotifyState.isShuffled = json["shuffle_state"];
      spotifyState.progressMillis = spotifyState.estimatedProgressMillis = json["progress_ms"];

      JsonObject context = json["context"];
      if (context.isNull() || strcmp(context["type"], "playlist") != 0) {
        spotifyState.contextName[0] = '\0';
        spotifyState.playlistId[0] = '\0';
        playingCountryIndex = -1;
        playingGenreIndex = -1;
      } else {
        const char *id = strrchr(context["uri"], ':') + 1;
        if (strcmp(spotifyState.playlistId, id) != 0) {
          spotifyState.contextName[0] = '\0';
          playingCountryIndex = -1;
        }
        strncpy(spotifyState.playlistId, id, SPOTIFY_ID_SIZE);
        playingGenreIndex = getGenreIndexByPlaylistId(id);
      }

      JsonObject item = json["item"];
      if (!item.isNull()) {
        spotifyState.durationMillis = item["duration_ms"];
        strncpy(spotifyState.name, item["name"], sizeof(spotifyState.name) - 1);
        strncpy(spotifyState.trackId, item["id"], SPOTIFY_ID_SIZE);

        JsonObject album = item["album"];
        if (!album.isNull()) {
          strncpy(spotifyState.albumName, album["name"], sizeof(spotifyState.albumName) - 1);
        } else {
          spotifyState.albumName[0] = '\0';
        }

        JsonArray artists = item["artists"];
        if (artists.size() > 0) {
          strncpy(spotifyState.artistName, artists[0]["name"], sizeof(spotifyState.artistName) - 1);
        } else {
          spotifyState.artistName[0] = '\0';
        }
      }

      if (json.containsKey("device")) {
        JsonObject jsonDevice = json["device"];
        const char *playingDeviceId = jsonDevice["id"];
        strncpy(activeSpotifyDeviceId, playingDeviceId, sizeof(activeSpotifyDeviceId) - 1);

        int playingDeviceIndex = -1;
        auto devicesCount = spotifyDevices.size();
        for (auto i = 0; i < devicesCount; i++) {
          if (strcmp(spotifyDevices[i].id, playingDeviceId) == 0) {
            playingDeviceIndex = i;
            break;
          }
        }
        if (playingDeviceIndex == -1) playingDeviceIndex = 0;
        if (devicesCount == 0) spotifyDevices.push_back({});
        activeSpotifyDevice = &spotifyDevices[playingDeviceIndex];

        strncpy(activeSpotifyDevice->id, playingDeviceId, sizeof(activeSpotifyDevice->id) - 1);
        strncpy(activeSpotifyDevice->name, jsonDevice["name"], sizeof(activeSpotifyDevice->name) - 1);
        activeSpotifyDevice->volumePercent = jsonDevice["volume_percent"];
      }

      spotifyState.disallowsSkippingNext = false;
      spotifyState.disallowsSkippingPrev = false;
      spotifyState.disallowsTogglingShuffle = false;
      JsonObject actions = json["actions"];
      JsonObject disallows = actions["disallows"];
      for (JsonPair disallow : disallows) {
        const char *key = disallow.key().c_str();
        if (strcmp(key, "skipping_next") == 0) {
          spotifyState.disallowsSkippingNext = disallow.value();
        } else if (strcmp(key, "skipping_prev") == 0) {
          spotifyState.disallowsSkippingPrev = disallow.value();
        } else if (strcmp(key, "toggling_shuffle") == 0) {
          spotifyState.disallowsTogglingShuffle = disallow.value();
        }
      }

      if (spotifyState.isPlaying && spotifyState.durationMillis > 0) {
        // Check if current song is about to end
        uint32_t remainingMillis = spotifyState.durationMillis - spotifyState.progressMillis;
        if (remainingMillis < SPOTIFY_POLL_INTERVAL) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          nextCurrentlyPlayingMillis = millis() + remainingMillis + 100;
        }
      }
      if (spotifyState.isPlaying && nextCurrentlyPlayingMillis == 0) {
        nextCurrentlyPlayingMillis = millis() + (spotifyState.durationMillis == 0 ? 2000 : SPOTIFY_POLL_INTERVAL);
      }
      if (spotifyState.isPlaying && lastInputMillis <= 1 && menuMode != NowPlaying && millis() < 10000) {
        setMenuMode(NowPlaying, PlayPauseButton);
      } else {
        invalidateDisplay();
      }
    } else {
      log_e("[%d] Heap free: %d", ts, ESP.getFreeHeap());
      log_e("[%d] Error %s parsing response: %s", ts, error.c_str(), response.payload.c_str());
    }
  } else if (response.httpCode == 204) {
    spotifyAction = Idle;
    spotifyState.isShuffled = false;
    spotifyResetProgress();
    nextCurrentlyPlayingMillis = 0;
  } else if (response.httpCode < 0 || response.httpCode > 500) {
    nextCurrentlyPlayingMillis = 1; // retry immediately
  } else {
    log_e("[%d] %d - %s", ts, response.httpCode, response.payload.c_str());
    delay(4000);
  }
}

/**
 * Get information about the current Spotify user
 */
void spotifyCurrentProfile() {
  if (spotifyAccessToken[0] == '\0') return;
  uint32_t ts = millis();

  HTTP_response_t response = spotifyApiRequest("GET", "me");

  if (response.httpCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());

    if (!error) {
      const char *displayName = json["display_name"];
      const char *country = json["country"];
      strncpy(activeSpotifyUser->name, displayName, sizeof(activeSpotifyUser->name) - 1);
      strncpy(activeSpotifyUser->country, country, sizeof(activeSpotifyUser->country) - 1);
      writeDataJson();
      if (menuMode == UserList) setMenuMode(UserList, spotifyUsers.size() - 1);
    } else {
      log_e("[%d] Unable to parse response payload:\n  %s", ts, response.payload.c_str());
    }
  } else {
    log_e("[%d] %d - %s", ts, response.httpCode, response.payload.c_str());
  }

  spotifyAction = spotifyDevicesLoaded ? CurrentlyPlaying : GetDevices;
}

void spotifyNext() {
  HTTP_response_t response = spotifyApiRequest("POST", "me/player/next");
  spotifyResetProgress(true);
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingPrev = false;
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyPrevious() {
  HTTP_response_t response = spotifyApiRequest("POST", "me/player/previous");
  spotifyResetProgress(true);
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingNext = false;
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifySeek() {
  if (spotifyAccessToken[0] == '\0' || spotifySeekToMillis < 0) return;
  spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
  char path[40];
  snprintf(path, sizeof(path), "me/player/seek?position_ms=%d", spotifySeekToMillis);
  HTTP_response_t response = spotifyApiRequest("PUT", path);
  if (response.httpCode == 204) {
    spotifyState.lastUpdateMillis = millis();
    spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
    nextCurrentlyPlayingMillis = spotifyState.lastUpdateMillis + SPOTIFY_WAIT_MILLIS;
    spotifyState.isPlaying = true;
    invalidateDisplay();
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
    spotifyResetProgress(true);
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyToggle() {
  if (spotifyAccessToken[0] == '\0') return;

  bool wasPlaying = spotifyState.isPlaying;
  spotifyState.isPlaying = !wasPlaying;
  spotifyState.lastUpdateMillis = millis();
  spotifyState.progressMillis = spotifyState.estimatedProgressMillis;

  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[68];
    snprintf(path, sizeof(path), wasPlaying ? "me/player/pause?device_id=%s" : "me/player/play?device_id=%s",
             activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path);
  } else {
    response = spotifyApiRequest("PUT", wasPlaying ? "me/player/pause" : "me/player/play");
  }

  if (response.httpCode == 204) {
    spotifyState.lastUpdateMillis = millis();
    nextCurrentlyPlayingMillis = spotifyState.lastUpdateMillis + SPOTIFY_WAIT_MILLIS;
    spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
    invalidateDisplay();
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
    spotifyResetProgress(true);
    nextCurrentlyPlayingMillis = millis();
    spotifyAction = CurrentlyPlaying;
  }
};

void spotifyPlayPlaylist() {
  if (spotifyAccessToken[0] == '\0' || spotifyPlayPlaylistId == nullptr) return;

  char requestContent[59];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"spotify:playlist:%s\"}", spotifyPlayPlaylistId);
  HTTP_response_t response;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[67];
    snprintf(path, sizeof(path), "me/player/play?device_id=%s", activeSpotifyDeviceId);
    response = spotifyApiRequest("PUT", path, requestContent);
  } else {
    response = spotifyApiRequest("PUT", "me/player/play", requestContent);
  }

  spotifyResetProgress(true);
  if (response.httpCode == 204) {
    spotifyState.isPlaying = true;
    strncpy(spotifyState.playlistId, spotifyPlayPlaylistId, SPOTIFY_ID_SIZE);
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
  spotifyPlayPlaylistId = nullptr;
};

void spotifyResetProgress(bool keepContext) {
  spotifyState.name[0] = '\0';
  spotifyState.artistName[0] = '\0';
  spotifyState.albumName[0] = '\0';
  spotifyState.trackId[0] = '\0';
  spotifyState.durationMillis = 0;
  spotifyState.progressMillis = 0;
  spotifyState.estimatedProgressMillis = 0;
  spotifyState.lastUpdateMillis = millis();
  spotifyState.isPlaying = false;
  nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  if (!keepContext) {
    nowPlayingDisplayMillis = 0;
    playingCountryIndex = -1;
    playingGenreIndex = -1;
    spotifyState.contextName[0] = '\0';
    spotifyState.playlistId[0] = '\0';
  }
  invalidateDisplay();
};

void spotifyGetDevices() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response = spotifyApiRequest("GET", "me/player/devices");
  if (response.httpCode == 200) {
    DynamicJsonDocument doc(5000);
    DeserializationError error = deserializeJson(doc, response.payload.c_str());

    if (!error) {
      JsonArray jsonDevices = doc["devices"];
      unsigned int devicesCount = jsonDevices.size();
      int activeDeviceIndex = -1;

      if (devicesCount == 0) activeSpotifyDeviceId[0] = '\0';
      spotifyDevicesLoaded = false;
      spotifyDevices.clear();
      for (JsonObject jsonDevice : jsonDevices) {
        const char *id = jsonDevice["id"];
        const char *name = jsonDevice["name"];
        bool isActive = jsonDevice["is_active"];
        uint8_t volume_percent = jsonDevice["volume_percent"];

        SpotifyDevice_t device;
        strncpy(device.id, id, sizeof(device.id) - 1);
        strncpy(device.name, name, sizeof(device.name) - 1);
        device.volumePercent = volume_percent;
        spotifyDevices.push_back(device);

        if (isActive || activeSpotifyDeviceId[0] == '\0' || strcmp(activeSpotifyDeviceId, id) == 0) {
          activeDeviceIndex = spotifyDevices.size() - 1;
          activeSpotifyDevice = &spotifyDevices.back();
          strncpy(activeSpotifyDeviceId, id, sizeof(activeSpotifyDeviceId) - 1);
        }
      }
      spotifyDevicesLoaded = true;
      // save the new default device when adding a user
      if (activeSpotifyDevice != nullptr && activeSpotifyUser->selectedDeviceId[0] == '\0') {
        setActiveDevice(activeSpotifyDevice);
        writeDataJson();
        delay(100);
        WiFi.disconnect(true);
        gpio_hold_dis((gpio_num_t)TFT_BL);
        esp_sleep_enable_timer_wakeup(100);
        esp_deep_sleep_start();
      } else if (menuMode == DeviceList) {
        setMenuMode(DeviceList, activeDeviceIndex < 0 ? 0 : activeDeviceIndex);
      }
    }
  }
  spotifyAction = CurrentlyPlaying;
  invalidateDisplay();
}

void spotifySetVolume() {
  spotifySetVolumeAtMillis = -1;
  if (activeSpotifyDevice == nullptr) return;
  int setpoint = spotifySetVolumeTo;
  char path[74];
  snprintf(path, sizeof(path), "me/player/volume?volume_percent=%d", setpoint);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", path);
  if (activeSpotifyDevice != nullptr) activeSpotifyDevice->volumePercent = setpoint;
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
}

void spotifyToggleShuffle() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyState.isShuffled = !spotifyState.isShuffled;
  invalidateDisplay();

  HTTP_response_t response;
  char path[33];
  snprintf(path, sizeof(path), "me/player/shuffle?state=%s", spotifyState.isShuffled ? "true" : "false");
  response = spotifyApiRequest("PUT", path);

  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
    spotifyState.isShuffled = !spotifyState.isShuffled;
    invalidateDisplay();
  }
  spotifyAction = spotifyState.isPlaying ? CurrentlyPlaying : Idle;
};

void spotifyTransferPlayback() {
  if (activeSpotifyDevice == nullptr) return;
  char requestContent[61];
  snprintf(requestContent, sizeof(requestContent), "{\"device_ids\":[\"%s\"]}", activeSpotifyDeviceId);
  HTTP_response_t response;
  response = spotifyApiRequest("PUT", "me/player", requestContent);
  if (response.httpCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyGetPlaylistDescription() {
  if (spotifyAccessToken[0] == '\0') return;
  const char *activePlaylistId = genrePlaylists[genreIndex];
  HTTP_response_t response;
  char url[53];
  snprintf(url, sizeof(url), "playlists/%s?fields=description", activePlaylistId);
  response = spotifyApiRequest("GET", url);

  if (response.httpCode == 200) {
    DynamicJsonDocument json(3000);
    DeserializationError error = deserializeJson(json, response.payload.c_str());
    if (!error) {
      String description = json["description"];

      similarMenuItems.clear();
      const char playlistPrefix[] = "spotify:playlist:";
      int urlPosition = description.indexOf(playlistPrefix);
      while (urlPosition > 0) {
        SimilarItem_t item;
        int idStart = urlPosition + sizeof(playlistPrefix) - 1;
        int idEnd = idStart + SPOTIFY_ID_SIZE;
        strlcpy(item.playlistId, description.c_str() + idStart, sizeof(item.playlistId));

        int nameStart = description.indexOf(">", idEnd) + 1;
        int nameEnd = description.indexOf("<", nameStart);
        if (nameStart > 0 && nameEnd > 0) {
          String name = description.substring(nameStart, nameEnd);
          int matchingGenreIndex = getGenreIndexByPlaylistId(item.playlistId);
          if (name == "Intro" || name == "Pulse" || name == "Edge" || matchingGenreIndex >= 0) {
            if(matchingGenreIndex < 0) {
              strncpy(item.name, name.c_str(), sizeof(item.name) - 1);
            } else {
              item.name[0] = '\0';
            }
            similarMenuItems.push_back(item);
          }

          urlPosition = description.indexOf(playlistPrefix, nameEnd);
        } else {
          urlPosition = description.indexOf(playlistPrefix, urlPosition + sizeof(playlistPrefix));
        }
      }
      setMenuMode(SimilarList, 0);
    }
  } else {
    log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
  }
  spotifyAction = CurrentlyPlaying;
};

void spotifyGetPlaylists() {
  if (spotifyAccessToken[0] == '\0') return;
  HTTP_response_t response;
  int nextOffset = 0;
  unsigned int limit = 50;
  unsigned int offset = 0;
  char url[60];

  spotifyPlaylists.clear();

  while (nextOffset >= 0) {
    snprintf(url, sizeof(url), "me/playlists/?fields=items(id,name)&limit=%d&offset=%d", limit, nextOffset);
    response = spotifyApiRequest("GET", url);
    if (response.httpCode == 200) {
      DynamicJsonDocument json(6000);
      DeserializationError error = deserializeJson(json, response.payload.c_str());
      if (!error) {
        limit = json["limit"];
        offset = json["offset"];

        JsonArray items = json["items"];
        for (auto item : items) {
          const char *id = item["id"];
          const char *name = item["name"];
          SpotifyPlaylist_t playlist;
          strncpy(playlist.id, id, sizeof(playlist.id) - 1);
          strncpy(playlist.name, name, sizeof(playlist.name) - 1);
          spotifyPlaylists.push_back(playlist);
        }

        if (json["next"].isNull()) {
          spotifyPlaylistsLoaded = true;
          if (menuMode == PlaylistList) {
            setMenuMode(PlaylistList, 0);
          }
          nextOffset = -1;
        } else {
          nextOffset = offset + limit;
        }
      }
    } else {
      nextOffset = -1;
      log_e("[%d] %d - %s", (uint32_t)millis(), response.httpCode, response.payload.c_str());
    }
  }

  spotifyAction = CurrentlyPlaying;
}
