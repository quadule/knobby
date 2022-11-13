#include "main.h"

void setup() {
  similarMenuItems.reserve(16);
  spotifyDevices.reserve(10);
  spotifyUsers.reserve(10);
  spotifyPlaylists.reserve(100);

  Serial.begin(115200);

  knobby.setup();
  rtc_gpio_hold_dis((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN);
  esp_pm_config_esp32_t pm_config_ls_enable = {
    .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
    .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
    .light_sleep_enable = true
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    ttgo = TTGOClass::getWatch();
    ttgo->setTftExternal(tft);
    ttgo->begin();
    ttgo->openBL();
    ttgo->setBrightness(255);

    // Turn on the IRQ used
    ttgo->power->adc1Enable(AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1, AXP202_ON);
    ttgo->power->enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ, AXP202_ON);
    ttgo->power->clearIRQ();

    // Turn off unused power
    ttgo->power->setPowerOutPut(AXP202_EXTEN, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_DCDC2, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_LDO3, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_LDO4, AXP202_OFF);
  #else
    ledcSetup(TFT_BL, 12000, 8);
    ledcAttachPin(TFT_BL, TFT_BL);
    ledcWrite(TFT_BL, 255);
    tft.init();
  #endif

  SPIFFS.begin(true);
  readDataJson();
  WiFi.setHostname(selectHostname());
  if (wifiSSID.isEmpty()) {
    WiFi.begin();
  } else {
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  }
  improvSerial.setup(std::string(knobby.name().c_str()));

  ESP32Encoder::useInternalWeakPullResistors = UP;
  knob.attachFullQuad(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN);
  button.setDebounceTicks(debounceMillis);
  button.setClickTicks(doubleClickMaxMillis);
  button.setPressTicks(longPressMillis);
  button.attachClick(knobClicked);
  button.attachDoubleClick(knobDoubleClicked);
  button.attachLongPressStart(knobLongPressStarted);
  button.attachLongPressStop(knobLongPressStopped);
  button.attachPressStart(knobPressStarted);

  tft.setRotation(flipDisplay ? 1 : 3);
  tft.loadFont(GillSans24_vlw_start);
  img.loadFont(GillSans24_vlw_start);
  ico.loadFont(icomoon24_vlw_start);
  batterySprite.loadFont(icomoon31_vlw_start);

  if (bootCount == 0) {
    log_d("Boot #%d", bootCount);
    countryIndex = random(COUNTRY_COUNT);
    genreIndex = random(GENRE_COUNT);
  } else {
    struct timeval tod;
    gettimeofday(&tod, NULL);
    time_t currentSeconds = tod.tv_sec;
    secondsAsleep = currentSeconds - lastSleepSeconds;
    log_d("Boot #%d, asleep for %ld seconds", bootCount, secondsAsleep);
    uint32_t millisPassed = secondsAsleep * 1000 + millis();
    if (secondsAsleep > newSessionSeconds ||
        (spotifyState.isPlaying && (spotifyState.estimatedProgressMillis + millisPassed > spotifyState.durationMillis))) {
      spotifyState.isShuffled = false;
      spotifyState.repeatMode = RepeatOff;
      spotifyResetProgress();
      spotifyStateLoaded = false;
    } else if (spotifyState.isPlaying) {
      spotifyState.progressMillis = spotifyState.estimatedProgressMillis =
          spotifyState.estimatedProgressMillis + millisPassed;
    }
  }
  bootCount++;

  if (spotifyUsers.empty()) {
    setMenuMode(InitialSetup, 0);
    menuSize = 0;
    wifiConnectWarning = true;
    tft.fillScreen(TFT_BLACK);
    drawMenuHeader(true, "setup knobby");
    startWifiManager();
  } else {
    if (secondsAsleep > newSessionSeconds) {
      genreSort = AlphabeticSort;
      setMenuMode(GenreList, getMenuIndexForGenreIndex(genreIndex));
    }
    knobHeldForRandom = digitalRead(ROTARY_ENCODER_BUTTON_PIN) == LOW;
    if (knobHeldForRandom) {
      startRandomizingMenu(true);
    } else {
      nextCurrentlyPlayingMillis = 1;
    }
  }

  Update.onProgress(onOTAProgress);
  ArduinoOTA.setHostname(selectHostname());
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.setPassword(knobby.password().c_str());
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

  // Initialize HTTP server handlers
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    inactivityMillis = 1000 * 60 * 3;
    log_i("server.on /");
    if (spotifyUsers.empty() || (menuMode == SettingsMenu && menuIndex == SettingsAddUser)) {
      request->redirect("http://knobby.local/authorize");
    } else {
      request->send(200, "text/html", index_html_start);
    }
  });

  server.on("/authorize", HTTP_GET, [](AsyncWebServerRequest *request) {
    log_i("server.on /authorize");
    spotifyGettingToken = true;

    unsigned char verifier[32];
    for (auto i=0; i<32; i++) verifier[i] = random(256);
    String encoded = base64::encode((const uint8_t *)&verifier, 32);
    encoded.replace('+', '-');
    encoded.replace('/', '_');
    strncpy(spotifyCodeVerifier, encoded.c_str(), 43);

    unsigned char codeVerifierSha[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *)&spotifyCodeVerifier, 43);
    mbedtls_md_finish(&ctx, codeVerifierSha);

    encoded = base64::encode(codeVerifierSha, sizeof(codeVerifierSha));
    encoded.replace('+', '-');
    encoded.replace('/', '_');
    strncpy(spotifyCodeChallenge, encoded.c_str(), 43);

    const String authUrl =
        "https://accounts.spotify.com/authorize/?response_type=code&scope="
        "user-read-private+user-read-currently-playing+user-read-playback-state+"
        "user-modify-playback-state+playlist-read-private+"
        "user-library-read+user-library-modify+user-follow-read+user-follow-modify"
        "&code_challenge_method=S256&code_challenge=" + String(spotifyCodeChallenge) +
        "&show_dialog=true&redirect_uri=http%3A%2F%2Fknobby.local%2Fcallback&client_id=" + String(spotifyClientId);
    request->redirect(authUrl);
  });

  server.on("/callback", HTTP_GET, [](AsyncWebServerRequest *request) {
    spotifyAuthCode = "";
    uint8_t paramsNr = request->params();
    for (uint8_t i = 0; i < paramsNr; i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "code") {
        spotifyAuthCode = p->value();
        spotifyGettingToken = true;
        spotifyQueueAction(GetToken);
        break;
      }
    }
    if (spotifyActionIsQueued(GetToken)) {
      setStatusMessage("connecting...");
      request->send(200, "text/plain",
                    "Authorized! Knobby should be ready to use in a moment. You can close this page now.");
    } else {
      request->send(204);
    }
  });

  server.on("/heap", HTTP_GET,
            [](AsyncWebServerRequest *request) { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

  server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebParameter *passwordParam = request->getParam("pass");
    if (!passwordParam || passwordParam->value() != knobby.password()) {
      request->send(403, "text/plain", "Incorrect password");
    } else {
      request->send(200, "text/plain", "OK, sleeping");
      delay(100);
      startDeepSleep();
    }
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      AsyncWebParameter *passwordParam = request->getParam("pass", true);
      if (!passwordParam || passwordParam->value() != knobby.password()) {
        request->send(403, "text/plain", "Incorrect password");
      } else {
        request->send(400, "text/plain", "Missing update file");
      }
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
      AsyncWebParameter *passwordParam = request->getParam("pass", true);
      if (!passwordParam || passwordParam->value() != knobby.password()) {
        request->send(403, "text/plain", "Incorrect password");
        return;
      }

      if (!index){
        updateContentLength = request->contentLength();
        int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
        if (cmd == U_SPIFFS) SPIFFS.end();
        if (!Update.begin(updateContentLength > 0 ? updateContentLength : UPDATE_SIZE_UNKNOWN, cmd)) {
          Update.printError(Serial);
        }
      }

      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }

      if (final) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Rebooting, please wait...");
        response->addHeader("Refresh", "10");
        response->addHeader("Location", "/");
        request->send(response);
        if (!Update.end(true)){
          updateContentLength = 0;
          Update.printError(Serial);
          setStatusMessage("update failed");
        } else {
          log_i("OTA: update complete");
          Serial.flush();
          delay(100);
          tft.fillScreen(TFT_BLACK);
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

  if (spotifyNeedsNewAccessToken()) {
    spotifyGettingToken = true;
    spotifyActionQueue.push_front(GetToken);
  }

  xTaskCreatePinnedToCore(backgroundApiLoop, "backgroundApiLoop", 10000, NULL, 1, &backgroundApiTask, 1);

  if (knobby.powerStatus() != PowerStatusPowered) knobby.updateBattery();
  if (knobby.powerStatus() == PowerStatusPowered) knobby.printHeader();
}

const char* selectHostname() {
  const char *hostname = nullptr;
  if (wifiSSID.isEmpty() && WiFi.SSID().isEmpty() && spotifyUsers.empty()) {
    hostname = "knobby"; // spotify oauth needs static redirect uri
  } else {
    hostname = knobby.name().c_str();
  }
  return hostname;
}

void startWifiManager() {
  if (wifiManager) return;
  if (knobby.powerStatus() == PowerStatusPowered) {
    inactivityMillis = 1000 * 60 * 10;
  } else {
    inactivityMillis = 1000 * 60 * 5;
  }
  wifiManager = new ESPAsync_WiFiManager(&server, &dnsServer, knobby.name().c_str());
  wifiManager->setBreakAfterConfig(true);
  wifiManager->setSaveConfigCallback(saveAndSleep);
  wifiManager->startConfigPortalModeless(knobby.name().c_str(), knobby.password().c_str(), false);
  dnsServer.setErrorReplyCode(AsyncDNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
}

bool isIdle() {
  return millis() - lastInputMillis > 5000;
}

void delayIfIdle() {
  auto now = millis();
  if (!displayInvalidated && knobby.powerStatus() == PowerStatusOnBattery &&
      (isIdle() || now - lastDelayMillis > 2000)) {
    delay(10);
    lastDelayMillis = now;
  } else if (randomizingMenuEndMillis == 0) {
    delay(5);
  }
}

void loop() {
  uint32_t now = millis();
  unsigned long previousInputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  if (knob.getCount() != lastKnobCount) knobRotated();
  button.tick();
  knobby.loop();
  shutdownIfLowBattery();

  now = millis();
  unsigned long inputDelta = (now == lastInputMillis) ? 1 : now - lastInputMillis;
  bool connected = WiFi.isConnected();

  if ((wifiConnectWarning || spotifyRefreshToken[0] == '\0') && menuMode != InitialSetup && inputDelta > menuTimeoutMillis) {
    setMenuMode(InitialSetup, 0);
  } else if (inputDelta < previousInputDelta && previousInputDelta > inactivityMillis) {
    if (menuMode == NowPlaying && now - spotifyState.lastUpdateMillis > 5000) {
      nextCurrentlyPlayingMillis = 1;
    }
    #ifdef LILYGO_WATCH_2019_WITH_TOUCH
      ttgo->bl->begin();
      ttgo->bl->adjust(255);
    #else
      ledcDetachPin(TFT_BL);
      digitalWrite(TFT_BL, HIGH);
      esp_pm_config_esp32_t pm_config_ls_enable = {
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = true
      };
      ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
    #endif
  } else if (inputDelta > inactivityMillis + 2 * inactivityFadeOutMillis) {
    startDeepSleep();
  } else if (inputDelta > inactivityMillis) {
    double fadeProgress = 1.0 - (inputDelta - inactivityMillis) / (double)inactivityFadeOutMillis;
    #ifdef LILYGO_WATCH_2019_WITH_TOUCH
      const int minimumDuty = 99;
    #else
      const int minimumDuty = 5;
    #endif
    uint32_t duty = min(max((int)round(fadeProgress * 255.0), minimumDuty), 255);
    if (duty >= 250) {
      esp_pm_config_esp32_t pm_config_ls_enable = {
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = false
      };
      ESP_ERROR_CHECK(esp_pm_configure(&pm_config_ls_enable));
    }
    #ifdef LILYGO_WATCH_2019_WITH_TOUCH
      ttgo->setBrightness(duty);
    #else
      if (duty >= 250) ledcAttachPin(TFT_BL, TFT_BL);
      ledcWrite(TFT_BL, duty);
    #endif
  } else if (menuMode == VolumeControl && inputDelta > menuTimeoutMillis) {
    setMenuMode(NowPlaying, VolumeButton);
    if (nextCurrentlyPlayingMillis == 0) nextCurrentlyPlayingMillis = 1;
  }

  if (connected && (lastConnectedMillis < 0 || lastReconnectAttemptMillis > lastConnectedMillis)) {
    if (lastConnectedMillis >= 0) {
      setStatusMessage("reconnected");
    } else {
      WiFi.setHostname(selectHostname());
      MDNS.begin(selectHostname());
      MDNS.addService("http", "tcp", 80);
      setStatusMessage("", 0);
    }
    lastConnectedMillis = now;
    wifiConnectWarning = false;
    if (spotifyUsers.empty() || spotifyRefreshToken[0] == '\0') {
      setMenuMode(InitialSetup, 0);
      invalidateDisplay(true);
    }
    log_i("Connected to wifi with IP address %s", WiFi.localIP().toString().c_str());

    wifi_config_t current_conf;
    esp_wifi_get_config(WIFI_IF_STA, &current_conf);
    current_conf.sta.listen_interval = 5;
    esp_wifi_set_config(WIFI_IF_STA, &current_conf);
  } else if (!connected && !WiFi.SSID().isEmpty() && now - lastReconnectAttemptMillis > wifiConnectTimeoutMillis) {
    lastReconnectAttemptMillis = now;
    if (lastConnectedMillis < 0) {
      if (now > 5000) setStatusMessage("connecting to wifi");
      if (!wifiConnectWarning && now >= wifiConnectTimeoutMillis) {
        log_w("No wifi after %d seconds, starting config portal.", (int)wifiConnectTimeoutMillis / 1000);
        drawSetup();
        setMenuMode(InitialSetup, 0);
        wifiConnectWarning = true;
        startWifiManager();
      }
    }
  }

  now = millis();

  if (menuMode == InitialSetup && !spotifyUsers.empty() && !wifiConnectWarning) {
    setMenuMode(GenreList, 0);
    startRandomizingMenu(false);
  } else if (!knobHeldForRandom && shouldShowRandom() && getExtraLongPressedMillis() > extraLongPressMillis) {
    knobHeldForRandom = true;
    longPressStartedMillis = 0;
    startRandomizingMenu(true);
  }

  if (randomizingMenuEndMillis > 0 && now >= randomizingMenuEndMillis) {
    randomizingMenuEndMillis = 0;
    randomizingMenuNextMillis = 0;
    if (randomizingMenuAutoplay) {
      spotifyActionQueue.clear();
      playMenuPlaylist(lastPlaylistMenuMode, menuIndex);
      nowPlayingDisplayMillis = 0;
    } else {
      invalidateDisplay();
    }
  }

  if (spotifyState.isPlaying && now > spotifyState.lastUpdateMillis) {
    spotifyState.estimatedProgressMillis =
        min(spotifyState.durationMillis, spotifyState.progressMillis + (now - spotifyState.lastUpdateMillis));
  }

  if (statusMessageUntilMillis > 0 && now >= statusMessageUntilMillis) {
    statusMessageUntilMillis = 0;
    statusMessage[0] = '\0';
    tft.fillRect(0, 1, screenWidth, 31, TFT_BLACK);
    showingStatusMessage = false;
    updateDisplay();
  } else if (clickEffectEndMillis > 0 && now > clickEffectEndMillis) {
    clickEffectEndMillis = 0;
    updateDisplay();
  } else if ((randomizingMenuNextMillis > 0 && now >= randomizingMenuNextMillis) ||
              lastInputMillis > lastDisplayMillis || (now - lastDisplayMillis > 950) ||
              (shouldShowRandom() && lastDisplayMillis < longPressStartedMillis + extraLongPressMillis * 2)) {
    updateDisplay();
  } else if (displayInvalidated && updateContentLength == 0) {
    updateDisplay();
  }

  auto top = menuMode == NowPlaying ? lineDivider : 0;
  if (shouldShowProgressBar()) {
    unsigned long margin = menuMode == NowPlaying ? 4 : 0;
    unsigned long maxWidth = tft.width() - margin * 2;
    unsigned long width = 59;
    unsigned long t = millis() / 2;
    showingProgressBar = true;
    for (unsigned long segment = 0; segment < 4; segment++) {
      unsigned long x = max(margin, maxWidth - (t + width * segment) % maxWidth);
      if (x + width > maxWidth) width -= (x + width) - maxWidth;
      tft.drawFastHLine(x, top, width, segment % 2 == 0 ? TFT_DARKERGREY : TFT_DARKGREY);
    }
  } else if (showingProgressBar) {
    tft.drawFastHLine(0, top, tft.width(), TFT_BLACK);
    showingProgressBar = false;
    updateDisplay();
  }

  ArduinoOTA.handle();
  improvSerial.loop();
  if (wifiManager) wifiManager->loop();
  delayIfIdle();
}

void backgroundApiLoop(void *params) {
  for (;;) {
    delayIfIdle();
    auto now = millis();
    if (WiFi.status() != WL_CONNECTED) continue;

    if (!spotifyGettingToken && spotifyNeedsNewAccessToken()) {
      spotifyGettingToken = true;
      spotifyActionQueue.push_front(GetToken);
    }

    if (spotifyActionQueue.size() > 0) {
      spotifyAction = spotifyActionQueue.front();
      spotifyActionQueue.pop_front();

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
            spotifyActionQueue.clear();
          }
          break;
        case CurrentlyPlaying:
          spotifyCurrentlyPlaying();
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
          if (spotifySetVolumeTo >= 0) spotifySetVolume();
          break;
        case CheckLike:
          spotifyCheckLike();
          break;
        case ToggleLike:
          spotifyToggleLike();
          break;
        case ToggleShuffle:
          spotifyToggleShuffle();
          break;
        case ToggleRepeat:
          spotifyToggleRepeat();
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
      now = millis();
      spotifyAction = Idle;
    }

    if (nextCurrentlyPlayingMillis > 0 && now >= nextCurrentlyPlayingMillis) {
      nextCurrentlyPlayingMillis = 0;
      spotifyQueueAction(CurrentlyPlaying);
    } else if (spotifyState.trackId[0] != '\0') {
      if (!spotifyState.checkedLike) spotifyQueueAction(CheckLike);
    }
  }
}

void selectRootMenuItem(uint16_t index) {
  checkMenuSize(RootMenu);
  if (index == rootMenuSimilarIndex) {
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
      spotifyQueueAction(GetPlaylistDescription);
    }
  } else if (index == rootMenuNowPlayingIndex) {
    nextCurrentlyPlayingMillis = lastInputMillis;
    if (lastMenuMode == NowPlaying) {
      setMenuMode(NowPlaying, lastMenuIndex);
    } else if (lastMenuMode == SeekControl) {
      setMenuMode(NowPlaying, SeekButton);
    } else if (lastMenuMode == VolumeControl) {
      setMenuMode(NowPlaying, VolumeButton);
    } else {
      setMenuMode(NowPlaying, PlayPauseButton);
    }
  } else if (index == rootMenuUsersIndex) {
    if (!spotifyUsers.empty()) {
      uint16_t newMenuIndex = lastMenuMode == UserList ? lastMenuIndex : 0;
      auto usersCount = spotifyUsers.size();
      for (auto i = 1; i < usersCount; i++) {
        if (strcmp(spotifyUsers[i].refreshToken, spotifyRefreshToken) == 0) {
          newMenuIndex = i;
          break;
        }
      }
      if (spotifyUsers[newMenuIndex].name[0] == '\0') spotifyQueueAction(CurrentProfile);
      setMenuMode(UserList, newMenuIndex);
    }
  } else if (index == rootMenuDevicesIndex) {
    if (lastMenuMode != DeviceList) {
      spotifyDevicesLoaded = false;
      spotifyQueueAction(GetDevices);
      setMenuMode(DeviceList, 0);
    } else {
      setMenuMode(DeviceList, lastMenuIndex);
    }
  } else {
    uint16_t newMenuIndex = lastMenuIndex;
    switch (index) {
      case SettingsMenu:
        if (lastMenuMode != SettingsMenu) newMenuIndex = 0;
        break;
      case PlaylistList:
        if (spotifyPlaylistsLoaded) {
          auto playingIndex = getMenuIndexForPlaylist(spotifyState.contextUri);
          newMenuIndex = playingIndex < 0 ? playlistIndex : playingIndex;
        } else {
          spotifyQueueAction(GetPlaylists);
          newMenuIndex = 0;
        }
        break;
      case CountryList:
        if (playingCountryIndex >= 0) {
          newMenuIndex = playingCountryIndex;
        } else if (lastMenuMode != CountryList) {
          newMenuIndex = countryIndex;
        }
        break;
      case GenreList:
        if (lastMenuMode == NowPlaying && playingGenreIndex >= 0) {
          newMenuIndex = getMenuIndexForGenreIndex(playingGenreIndex);
        } else {
          newMenuIndex = getMenuIndexForGenreIndex(genreIndex);
        }
        break;
      default:
        break;
    }

    setMenuMode((MenuModes)index, newMenuIndex);
  }
}

void knobRotated() {
  int newCount = knob.getCount();
  int knobDelta = newCount - lastKnobCount;
  int reversingMultiplier = flipDisplay ? -1 : 1;
  int positionDelta = knobDelta / pulseCount * reversingMultiplier;
  if (positionDelta == 0) return;
  lastKnobCount = newCount;

  unsigned long lastInputDelta = millis() - lastInputMillis;
  lastInputMillis = millis();
  if (button.isLongPressed()) knobRotatedWhileLongPressed = true;

  if (randomizingMenuEndMillis > 0) return;
  menuSize = checkMenuSize(menuMode);
  if (menuSize == 0) return;

  int knobDirection = knobDelta > 0 ? 1 : -1;
  if (lastInputDelta > 333 || knobDirection != lastKnobDirection) {
    lastKnobDirection = knobDirection;
    knobVelocity.fill(0.0f);
  } else if (menuMode != VolumeControl && menuSize >= 50 && lastInputDelta > 0) {
    float velocity = abs((float)positionDelta / (float)min(lastInputDelta, (unsigned long)50));
    knobVelocity[knobVelocityPosition] = velocity;
    knobVelocityPosition = (knobVelocityPosition + 1) % knobVelocity.size();
    float averageKnobVelocity = 0.0;
    for (auto v : knobVelocity) averageKnobVelocity += (v);
    averageKnobVelocity = averageKnobVelocity / (float)knobVelocity.size();
    if(averageKnobVelocity > 0.04) {
      positionDelta *= 10;
    } else if (averageKnobVelocity > 0.08) {
      positionDelta *= 100;
    }
  }

  if (menuMode == SeekControl || menuMode == VolumeControl) {
    int newMenuIndex = ((int)menuIndex + positionDelta);
    newMenuIndex = newMenuIndex < 0 ? 0 : min(newMenuIndex, menuSize - 1);
    setMenuIndex(newMenuIndex);
    if (menuMode == VolumeControl) {
      spotifySetVolumeTo = menuIndex;
      spotifyQueueAction(SetVolume);
    }
  } else {
    int newMenuIndex = ((int)menuIndex + positionDelta) % (int)menuSize;
    if (newMenuIndex < 0) newMenuIndex += menuSize;
    setMenuIndex(newMenuIndex);
  }
}

void knobPressStarted() {
  pressedMenuIndex = menuIndex;
}

void knobClicked() {
  lastInputMillis = millis();
  if (knobHeldForRandom && randomizingMenuEndMillis > 0) {
    knobHeldForRandom = false;
    return;
  }
  if (pressedMenuIndex < 0) return;
  menuClickedMillis = lastInputMillis;
  clickEffectEndMillis = lastInputMillis + clickEffectMillis;
  if (randomizingMenuEndMillis > 0) randomizingMenuEndMillis = 0;

  switch (menuMode) {
    case RootMenu:
      selectRootMenuItem(pressedMenuIndex);
      break;
    case UserList:
      if (!spotifyUsers.empty() && activeSpotifyUser != &spotifyUsers[pressedMenuIndex]) {
        spotifyActionQueue.clear();
        spotifyTokenLifetime = 0;
        spotifyTokenSeconds = 0;
        spotifyAccessToken[0] = '\0';
        spotifyDevicesLoaded = false;
        spotifyDevices.clear();
        spotifyResetProgress();
        setActiveUser(&spotifyUsers[pressedMenuIndex]);
        writeDataJson();
        invalidateDisplay();
      } else if (activeSpotifyUser == &spotifyUsers[pressedMenuIndex]) {
        setMenuMode(NowPlaying, PlayPauseButton);
      }
      nextCurrentlyPlayingMillis = 1;
      break;
    case DeviceList:
      if (spotifyDevicesLoaded && !spotifyDevices.empty()) {
        bool changed = strcmp(activeSpotifyUser->selectedDeviceId, spotifyDevices[pressedMenuIndex].id) != 0;
        if (spotifyRetryAction != Idle) {
          spotifyQueueAction(spotifyRetryAction);
          spotifyRetryAction = Idle;
          nextCurrentlyPlayingMillis = lastInputMillis + SPOTIFY_WAIT_MILLIS;
        } else if (spotifyState.isPlaying && !spotifyGettingToken && spotifyState.trackId[0] != '\0' &&
            (!activeSpotifyDevice || strcmp(activeSpotifyDevice->id, spotifyDevices[pressedMenuIndex].id) != 0)) {
          spotifyQueueAction(TransferPlayback);
        }
        setActiveDevice(&spotifyDevices[pressedMenuIndex]);
        if (changed) {
          writeDataJson();
          invalidateDisplay();
        } else {
          setMenuMode(NowPlaying, PlayPauseButton);
        }
      }
      break;
    case GenreList:
    case SimilarList:
    case CountryList:
    case PlaylistList:
      if (activeSpotifyUser) playMenuPlaylist(menuMode, pressedMenuIndex);
      break;
    case SeekControl:
      if (activeSpotifyUser) {
        spotifySeekToMillis = pressedMenuIndex * 1000;
        spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
        spotifyState.lastUpdateMillis = millis();
        spotifyQueueAction(Seek);
        setMenuMode(NowPlaying, SeekButton);
      }
      break;
    case VolumeControl:
      setMenuMode(NowPlaying, VolumeButton);
      nextCurrentlyPlayingMillis = lastInputMillis + SPOTIFY_WAIT_MILLIS;
      break;
    case NowPlaying:
      switch (pressedMenuIndex) {
        case LikeButton:
          if (spotifyState.trackId[0] != '\0') {
            spotifyQueueAction(ToggleLike);
          }
          break;
        case ShuffleButton:
          if (!spotifyState.disallowsTogglingShuffle) {
            spotifyQueueAction(ToggleShuffle);
          }
          break;
        case BackButton:
          if (spotifyState.disallowsSkippingPrev || spotifyState.estimatedProgressMillis > 5000) {
            spotifySeekToMillis = 0;
            spotifyQueueAction(Seek);
          } else {
            spotifyQueueAction(Previous);
          }
          break;
        case PlayPauseButton:
          spotifyQueueAction(Toggle);
          break;
        case NextButton:
          if (!spotifyState.disallowsSkippingNext) {
            spotifyQueueAction(Next);
          }
          break;
        case RepeatButton:
          spotifyQueueAction(ToggleRepeat);
          break;
        case VolumeButton:
          if (activeSpotifyDevice != nullptr) {
            setMenuMode(VolumeControl, activeSpotifyDevice->volumePercent);
          } else {
            setMenuMode(VolumeControl, spotifySetVolumeTo >= 0 ? spotifySetVolumeTo : 50);
          }
          break;
        case SeekButton:
          if (spotifyState.durationMillis > 0) {
            setMenuMode(SeekControl, spotifyState.estimatedProgressMillis / 1000);
          }
          break;
        default:
          break;
      }
      updateDisplay();
      break;
    default:
      break;
  }
}

void knobDoubleClicked() {
  lastInputMillis = millis();
  if (knobHeldForRandom || randomizingMenuEndMillis > 0) {
    knobHeldForRandom = false;
    return;
  }
  if (menuMode == GenreList) {
    genreSort = (GenreSortModes)((genreSort + 1) % genreSortModesCount);
    setMenuIndex(getMenuIndexForGenreIndex(genreIndex));
    switch (genreSort) {
      case AlphabeticSort:
        setStatusMessage("sort by name");
        break;
      case AlphabeticSuffixSort:
        setStatusMessage("sort by suffix");
        break;
      case AmbienceSort:
        setStatusMessage("sort by ambience");
        break;
      case ModernitySort:
        setStatusMessage("sort by modernity");
        break;
      case PopularitySort:
        setStatusMessage("sort by popularity");
        break;
    }
  } else if (menuMode == SettingsMenu) {
    switch (menuIndex) {
      case SettingsUpdate:
        updateFirmware();
        break;
      case SettingsOrientation:
        flipDisplay = !flipDisplay;
        writeDataJson();
        tft.setRotation(flipDisplay ? 1 : 3);
        invalidateDisplay(true);
        break;
      case SettingsAddUser:
        if (strcmp(WiFi.getHostname(), "knobby") != 0) {
          MDNS.end();
          WiFi.setHostname("knobby"); // spotify oauth needs static redirect uri
          MDNS.begin("knobby");
          invalidateDisplay();
        }
        break;
      case SettingsRemoveUser:
        if (activeSpotifyUser) {
          setStatusMessage("logged out");
          auto activeItr = std::find_if(spotifyUsers.begin(), spotifyUsers.end(), [&](SpotifyUser_t const &user) { return &user == activeSpotifyUser; });
          setMenuMode(UserList, std::distance(spotifyUsers.begin(), activeItr));
          updateDisplay();
          spotifyUsers.erase(activeItr);
          spotifyAccessToken[0] = '\0';
          spotifyRefreshToken[0] = '\0';
          spotifyDevices.clear();
          spotifyDevicesLoaded = false;
          writeDataJson();
          delay(statusMessageMillis);
          tft.fillScreen(TFT_BLACK);
          ESP.restart();
        }
        break;
      case SettingsReset:
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(textStartX, lineTwo);
        img.setTextColor(TFT_WHITE, TFT_BLACK);
        drawCenteredText("resetting...", textWidth, 1);
        spotifyAccessToken[0] = '\0';
        spotifyRefreshToken[0] = '\0';
        spotifyUsers.clear();
        knobby.resetSettings();
        writeDataJson();
        WiFi.disconnect(true, true);
        delay(statusMessageMillis);
        tft.fillScreen(TFT_BLACK);
        ESP.restart();
        break;
      default:
        break;
    }
  } else if (!spotifyState.disallowsSkippingNext) {
    spotifyQueueAction(Next);
    setStatusMessage("next");
  }
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
      case GenreList:
        setMenuMode(RootMenu, (uint16_t)lastPlaylistMenuMode);
        break;
      default:
        setMenuMode(RootMenu, (uint16_t)menuMode);
        break;
    }
  } else if (menuMode == SimilarList) {
    setMenuMode(RootMenu, rootMenuSimilarIndex);
  } else if (spotifyState.isPlaying || menuMode == SeekControl || menuMode == VolumeControl) {
    setMenuMode(RootMenu, rootMenuNowPlayingIndex);
  } else if (menuMode == UserList) {
    setMenuMode(RootMenu, rootMenuUsersIndex);
  } else {
    setMenuMode(RootMenu, (uint16_t)menuMode);
  }
}

void knobLongPressStopped() {
  lastInputMillis = millis();
  if (knobHeldForRandom || longPressStartedMillis == 0) {
    knobHeldForRandom = false;
    return;
  }
  if (!knobHeldForRandom && shouldShowRandom()) startRandomizingMenu();
  longPressStartedMillis = 0;
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    if (menuMode == RootMenu && knobRotatedWhileLongPressed && randomizingMenuEndMillis == 0) selectRootMenuItem(menuIndex);
  #else
    if (menuMode == RootMenu && randomizingMenuEndMillis == 0) selectRootMenuItem(menuIndex);
  #endif
}

void drawBattery(unsigned int percent, unsigned int y, bool charging) {
  const unsigned int batterySize = 31;
  batterySprite.createSprite(batterySize, batterySize);
  batterySprite.fillSprite(TFT_BLACK);
  batterySprite.setTextDatum(MC_DATUM);
  batterySprite.setTextColor(TFT_DARKERGREY, TFT_BLACK);
  batterySprite.setCursor(1, 3);
  if (charging) {
    batterySprite.setTextColor(TFT_GREEN, TFT_BLACK);
    batterySprite.printToSprite(ICON_BATTERY_CHARGE);
  } else {
    if (percent >= 75) {
      batterySprite.printToSprite(ICON_BATTERY_FULL);
    } else if (percent >= 50) {
      batterySprite.printToSprite(ICON_BATTERY_HIGH);
    } else if (percent >= 25) {
      batterySprite.printToSprite(ICON_BATTERY_MID);
    } else {
      if (percent < 10) batterySprite.setTextColor(TFT_RED, TFT_BLACK);
      batterySprite.printToSprite(ICON_BATTERY_LOW);
    }
  }
  tft.setCursor(centerX - batterySize / 2, y);
  batterySprite.pushSprite(tft.getCursorX(), tft.getCursorY());
  batterySprite.deleteSprite();
}

void shutdownIfLowBattery() {
  if (knobby.shouldUpdateBattery() || knobby.powerStatus() != PowerStatusOnBattery) return;
  float batteryVoltage = knobby.batteryVoltage();
  if (batteryVoltage >= 3.1 || batteryVoltage < 0.01) return;
  digitalWrite(ADC_EN, LOW);
  log_i("Battery voltage is %.3f V, shutting down!", knobby.batteryVoltage());
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
  if (selected) {
    tft.drawFastHLine(6, lineDivider, screenWidth - 12, TFT_LIGHTGREY);
  } else {
    const int width = 160;
    tft.drawFastHLine(6, lineDivider, screenWidth - 12, TFT_BLACK);
    tft.drawFastHLine(centerX - width / 2, lineDivider, width, TFT_LIGHTBLACK);
  }
}

void drawIcon(const String& icon, bool selected, bool clicked, bool disabled, bool filled) {
  ico.setTextDatum(MC_DATUM);
  const int width = ICON_SIZE + 2;
  const int height = ICON_SIZE + 2;
  ico.createSprite(width, height + 1);

  const uint16_t bg = TFT_BLACK;
  const uint16_t fg = TFT_DARKGREY;
  const uint16_t fgActive = TFT_LIGHTGREY;
  const uint16_t fgDisabled = TFT_DARKERGREY;

  ico.fillSprite(bg);
  if (clicked) {
    ico.fillRoundRect(0, 0, width, height, 3, fgActive);
    ico.setTextColor(bg, fgActive);
  } else if (filled) {
    ico.fillRoundRect(2, 2, width - 4, height - 4, 3, selected ? fgActive : fg);
    ico.setTextColor(bg, selected ? fgActive : fg);
  } else if (disabled) {
    ico.setTextColor(fgDisabled, bg);
  } else if (selected) {
    ico.setTextColor(fgActive, bg);
  } else {
    ico.setTextColor(fg, bg);
  }
  ico.setCursor(1, 4);
  ico.printToSprite(icon);
  if (selected) ico.drawRoundRect(0, 0, width, height, 3, fgActive);
  ico.pushSprite(tft.getCursorX(), tft.getCursorY());
  ico.deleteSprite();
}

void drawStatusMessage() {
  if(!showingStatusMessage) {
    showingStatusMessage = true;
    tft.fillRect(0, 1, screenWidth, 31, TFT_BLACK);
  }
  tft.setCursor(textStartX + textPadding, lineOne);
  img.setTextColor(TFT_WHITE, TFT_BLACK);
  drawCenteredText(statusMessage, textWidth - textPadding * 2);
}

void drawMenuHeader(bool selected, const char *text) {
  if (statusMessage[0] != '\0') {
    drawStatusMessage();
  } else {
    tft.setCursor(textStartX + textPadding, lineOne);
    img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
    if (text[0] != '\0') {
      drawCenteredText(text, textWidth - textPadding * 2);
    } else if (menuSize > 0) {
      char label[14];
      sprintf(label, "%d / %d", menuIndex + 1, menuSize);
      drawCenteredText(label, textWidth - textPadding * 2);
    }
  }
  if (menuSize > 0) {
    tft.setTextColor(menuSize == 1 ? TFT_LIGHTBLACK : TFT_DARKERGREY, TFT_BLACK);
    tft.setCursor(8, lineOne - 2);
    tft.print("\xE2\x80\xB9");
    tft.setCursor(screenWidth - 15, lineOne - 2);
    tft.print("\xE2\x80\xBA");
  }
  if (!showingProgressBar && (text[0] != '\0' || menuMode == SeekControl || menuSize > 0)) drawDivider(selected);
}

void drawSetup() {
  drawMenuHeader(true, "setup knobby");
  img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (wifiConnectWarning) {
    if (menuIndex == 0) {
      tft.setCursor(textStartX, lineTwo);
      drawCenteredText("connect usb and visit", textWidth, 1);
      tft.setCursor(textStartX, lineThree);
      drawCenteredText("setup.knobby.net", textWidth, 1);
      tft.setCursor(textStartX, lineFour);
      drawCenteredText("", textWidth, 1);
    } else {
      tft.setCursor(textStartX, lineTwo);
      drawCenteredText("or join wifi network", textWidth, 1);
      tft.setCursor(textStartX, lineThree);
      drawCenteredText(("name: " + knobby.name()).c_str(), textWidth, 1);
      tft.setCursor(textStartX, lineFour);
      drawCenteredText(("pass: " + knobby.password()).c_str(), textWidth, 1);
    }
  } else {
    drawMenuHeader(true, "setup knobby");
    tft.setCursor(textStartX, lineTwo);
    drawCenteredText("log in with spotify at http://knobby.local", textWidth, maxTextLines);
  }
}

void drawRandomizing() {
  if (millis() < randomizingMenuNextMillis) return;
  randomizingMenuTicks++;
  randomizingMenuNextMillis = millis() + max((int)(pow(randomizingMenuTicks, 3) + pow(randomizingMenuTicks, 2)), 10);
  setMenuIndex(random(checkMenuSize(lastPlaylistMenuMode)));
  tft.setCursor(textStartX, lineTwo);
  if (isGenreMenu(lastPlaylistMenuMode)) {
    img.setTextColor(genreColors[genreIndex], TFT_BLACK);
    getPlaylistName(spotifyState.contextName, lastPlaylistMenuMode, menuIndex);
  } else if (lastPlaylistMenuMode == CountryList) {
    getPlaylistName(spotifyState.contextName, lastPlaylistMenuMode, menuIndex);
  } else if (lastPlaylistMenuMode == PlaylistList) {
    getPlaylistName(spotifyState.contextName, lastPlaylistMenuMode, menuIndex + 1);
  }
  drawCenteredText(spotifyState.contextName, textWidth, maxTextLines);
}

void drawRootMenu() {
  tft.setCursor(textStartX, lineTwo);
  img.setTextColor(TFT_WHITE, TFT_BLACK);
  if (shouldShowRandom()) {
    double pressedProgress = min(1.0, (double)getExtraLongPressedMillis() / (double)extraLongPressMillis);
    tft.drawFastHLine(0, 0, (int)(pressedProgress * screenWidth), TFT_WHITE);
    const char *randomText = "random";
    if (lastPlaylistMenuMode == GenreList) randomText = "random genre";
    else if (lastPlaylistMenuMode == CountryList) randomText = "random country";
    else if (lastPlaylistMenuMode == PlaylistList || lastPlaylistMenuMode == SimilarList) randomText = "random playlist";
    drawCenteredText(randomText, textWidth);
  } else {
    tft.drawFastHLine(0, 0, screenWidth, TFT_BLACK);
    if (menuIndex == rootMenuSimilarIndex) {
      drawCenteredText(rootMenuItems[SimilarList], textWidth);
    } else if (menuIndex == rootMenuNowPlayingIndex) {
      drawCenteredText(rootMenuItems[NowPlaying], textWidth);
    } else if (menuIndex == rootMenuUsersIndex) {
      drawCenteredText(rootMenuItems[UserList], textWidth);
    } else if (menuIndex == rootMenuDevicesIndex) {
      drawCenteredText(rootMenuItems[DeviceList], textWidth);
    } else {
      drawCenteredText(rootMenuItems[menuIndex], textWidth);
    }
  }
  tft.drawRoundRect(7, lineTwo - 15, screenWidth - 14, 49, 5, TFT_WHITE);
}

void drawSettingsMenu() {
  bool selected = false;
  img.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(textStartX, lineTwo);
  switch (menuIndex) {
    case SettingsAbout:
      drawCenteredText("knobby.net", textWidth, 1);
      tft.setCursor(textStartX, lineThree);
      char about[100];
      sprintf(about, "by milo winningham %s", KNOBBY_VERSION);
      drawCenteredText(about, textWidth, 2);
      break;
    case SettingsUpdate:
      drawCenteredText("double click to begin updating", textWidth, 3);
      break;
    case SettingsOrientation:
      drawCenteredText("double click to rotate the display", textWidth, 3);
      break;
    case SettingsAddUser:
      if (strcmp(WiFi.getHostname(), "knobby") != 0) {
        drawCenteredText("double click to add a spotify account", textWidth, 3);
      } else {
        selected = true;
        img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        drawCenteredText("log in with spotify at http://knobby.local", textWidth, 3);
      }
      break;
    case SettingsRemoveUser:
      drawCenteredText("double click to log out of spotify", textWidth, 3);
      break;
    case SettingsReset:
      drawCenteredText("double click to erase all data and enter setup mode", textWidth, 3);;
      break;
    default:
      break;
  }
  drawMenuHeader(selected, settingsMenuItems[menuIndex]);
}

void drawUserMenu() {
  SpotifyUser_t *user = &spotifyUsers[menuIndex];
  bool selected = user == activeSpotifyUser;
  drawMenuHeader(selected);

  if (user != nullptr) {
    tft.setCursor(textStartX, lineTwo);
    if (user->name[0] == '\0') {
      drawCenteredText("loading...", textWidth);
    } else {
      if (strcmp(user->refreshToken, spotifyRefreshToken) == 0) img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      drawCenteredText(user->name, textWidth, 3);
    }
  }
}

void drawDeviceMenu() {
  if (spotifyDevicesLoaded && !spotifyDevices.empty()) {
    SpotifyDevice_t *device = &spotifyDevices[menuIndex];
    bool selected = device == activeSpotifyDevice;
    drawMenuHeader(selected);

    if (device != nullptr) {
      tft.setCursor(textStartX, lineTwo);
      if (!spotifyDevicesLoaded || device->name[0] == '\0') {
        drawCenteredText("loading...", textWidth, 3);
      } else {
        if (strcmp(device->id, activeSpotifyDeviceId) == 0) img.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        drawCenteredText(device->name, textWidth, 3);
      }
    }
  } else if (!spotifyDevicesLoaded || spotifyActionIsQueued(GetDevices)) {
    tft.setCursor(textStartX, lineTwo);
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    drawCenteredText("loading...", textWidth, 3);
  } else if (spotifyDevicesLoaded && spotifyDevices.empty()) {
    tft.setCursor(textStartX, lineTwo);
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    drawCenteredText("no devices found", textWidth, 3);
  }
}

void drawVolumeControl() {
  const uint8_t x = 10;
  const uint8_t y = 30;
  const uint8_t width = 220;
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
}

void drawNowPlayingOrSeek() {
  const auto now = millis();
  tft.setCursor(0, lineOne);
  if (statusMessage[0] != '\0') {
    drawStatusMessage();
  } else if (menuMode == SeekControl) {
    char elapsed[11];
    formatMillis(elapsed, menuIndex * 1000);
    drawMenuHeader(false, elapsed);
  } else if (menuMode == NowPlaying) {
    const int iconTop = 4;
    const int width = ICON_SIZE + 2;
    const int extraSpace = 10;

    bool likeClicked = menuIndex == LikeButton && spotifyActionIsQueued(ToggleLike) && now <= clickEffectEndMillis;
    tft.setCursor(5, iconTop);
    drawIcon(spotifyState.isLiked ? ICON_FAVORITE : ICON_FAVORITE_OUTLINE, menuIndex == LikeButton, likeClicked,
            spotifyState.trackId[0] == '\0' && (spotifyAction != Previous && spotifyAction != Next));

    tft.setCursor(screenWidth / 2 - ICON_SIZE / 2 - ICON_SIZE * 2 - 3 - extraSpace, iconTop);
    drawIcon(ICON_SHUFFLE, menuIndex == ShuffleButton, false, spotifyState.disallowsTogglingShuffle, spotifyState.isShuffled);

    bool backClicked = menuIndex == BackButton && (spotifyActionIsQueued(Previous) || (spotifyActionIsQueued(Seek) && spotifySeekToMillis == 0)) && now <= clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() + width + extraSpace + 1, iconTop);
    drawIcon(ICON_SKIP_PREVIOUS, menuIndex == BackButton, backClicked, spotifyState.disallowsSkippingPrev);

    const String& playPauseIcon = spotifyState.isPlaying ? ICON_PAUSE : ICON_PLAY_ARROW;
    bool playPauseClicked = menuIndex == PlayPauseButton && spotifyActionIsQueued(Toggle) && now <= clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() + width, iconTop);
    drawIcon(playPauseIcon, menuIndex == PlayPauseButton, playPauseClicked);

    bool nextClicked = menuIndex == NextButton && spotifyActionIsQueued(Next) && now <= clickEffectEndMillis;
    tft.setCursor(tft.getCursorX() + width, iconTop);
    drawIcon(ICON_SKIP_NEXT, menuIndex == NextButton, nextClicked, spotifyState.disallowsSkippingNext);

    tft.setCursor(tft.getCursorX() + width + extraSpace, iconTop);
    if (spotifyState.repeatMode == RepeatOff) {
      drawIcon(ICON_REPEAT, menuIndex == RepeatButton, false, spotifyState.disallowsTogglingRepeatContext && spotifyState.disallowsTogglingRepeatTrack);
    } else if (spotifyState.repeatMode == RepeatContext) {
      drawIcon(ICON_REPEAT, menuIndex == RepeatButton, false, false, true);
    } else if (spotifyState.repeatMode == RepeatTrack) {
      drawIcon(ICON_REPEAT_ONE, menuIndex == RepeatButton, false, false, true);
    }

    tft.setCursor(screenWidth - 5 - width, iconTop);
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
    drawIcon(volumeIcon, menuIndex == VolumeButton, false, activeSpotifyDevice == nullptr);
  }

  if (!shouldShowProgressBar()) {
    const int seekRadius = 4;
    const bool seekSelected = menuMode == SeekControl || menuIndex == SeekButton;
    img.createSprite(dividerWidth + seekRadius * 2, seekRadius * 2 + 1);
    img.fillSprite(TFT_BLACK);
    img.drawFastHLine(seekRadius, seekRadius, dividerWidth, seekSelected ? TFT_DARKERGREY : TFT_LIGHTBLACK);
    if (seekSelected) img.drawFastHLine(seekRadius, seekRadius - 1, dividerWidth, TFT_DARKERGREY);
    if (spotifyState.estimatedProgressMillis > 0 && spotifyState.durationMillis > 0) {
      float progress = (float)spotifyState.estimatedProgressMillis / (float)spotifyState.durationMillis;
      img.drawFastHLine(seekRadius, seekRadius, round(dividerWidth * progress), seekSelected ? TFT_WHITE : TFT_LIGHTGREY);
      if (menuMode == SeekControl) {
        img.fillCircle(seekRadius + round(dividerWidth * ((float)menuIndex / (float)menuSize)), seekRadius, seekRadius, TFT_WHITE);
      } else if (menuIndex == SeekButton) {
        img.fillCircle(seekRadius + round(dividerWidth * progress), seekRadius, seekRadius, TFT_WHITE);
        img.fillCircle(seekRadius + round(dividerWidth * progress), seekRadius, 2, TFT_BLACK);
      }
    }
    img.pushSprite(6 - seekRadius, lineDivider - seekRadius);
    img.deleteSprite();
  }

  if (millis() - nowPlayingDisplayMillis >= 50) {
    tft.setCursor(textStartX, lineTwo);

    const bool isGenrePlaylist = playingGenreIndex >= 0;
    if (isGenrePlaylist) {
      img.setTextColor(genreColors[playingGenreIndex], TFT_BLACK);
    } else {
      img.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    const bool showContextName = menuMode == NowPlaying && (spotifyState.durationMillis == 0 ||
                                                            spotifyState.estimatedProgressMillis % 6000 > 3000);
    if (showContextName && spotifyState.contextName[0] != '\0' &&
        (spotifyState.isPlaying || spotifyPlayUri[0] != '\0' || spotifyActionIsQueued(Previous) ||
         spotifyActionIsQueued(Next))) {
      drawCenteredText(spotifyState.contextName, textWidth, maxTextLines);
    } else if (spotifyState.artistName[0] != '\0' && spotifyState.name[0] != '\0') {
      char playing[205];
      snprintf(playing, sizeof(playing) - 1, "%s – %s", spotifyState.artistName, spotifyState.name);
      drawCenteredText(playing, textWidth, maxTextLines);
    } else {
      img.setTextColor(TFT_LIGHTBLACK, TFT_BLACK);
      const char *text = nullptr;
      if (spotifyState.isPrivateSession) {
        text = "- private session -";
      } else if (!spotifyStateLoaded && (spotifyGettingToken || spotifyActionIsQueued(CurrentlyPlaying)) && !spotifyUsers.empty()) {
        text = "loading...";
      } else if (spotifyApiRequestStartedMillis < 0 && !spotifyState.isPlaying) {
        text = "- nothing playing -";
      }
      if (text) drawCenteredText(text, textWidth, maxTextLines);
    }

    nowPlayingDisplayMillis = millis();
  }
}

void drawPlaylistsMenu() {
  getPlaylistName(menuText, menuMode, menuIndex);
  const char *text = menuText;
  bool selected = playingGenreIndex == genreIndex;
  char selectedUri[sizeof(SpotifyState_t::contextUri)];

  if (menuMode == CountryList) {
    selected = playingCountryIndex == menuIndex;
  } else if (menuMode == PlaylistList) {
    if (!spotifyPlaylistsLoaded) {
      text = "loading...";
    } else {
      getPlaylistUri(selectedUri, menuMode, menuIndex);
      selected = strcmp(spotifyState.contextUri, selectedUri) == 0;
    }
  } else if (menuMode == SimilarList) {
    if (similarMenuItems.empty()) {
      text = "loading...";
    } else {
      getPlaylistUri(selectedUri, menuMode, menuIndex);
      selected = strcmp(spotifyState.contextUri, selectedUri) == 0;
    }
  }

  drawMenuHeader(selected);

  if (menuMode == PlaylistList || menuMode == CountryList) {
    img.setTextColor(selected ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
  } else if (menuMode == SimilarList && similarMenuItems[menuIndex].name[0] != '\0') {
    img.setTextColor(genreColors[similarMenuGenreIndex], TFT_BLACK);
  } else if (menuSize == 0) {
    img.setTextColor(TFT_DARKGREY, TFT_BLACK);
  } else {
    img.setTextColor(genreColors[genreIndex], TFT_BLACK);
  }

  tft.setCursor(textStartX, lineTwo);
  drawCenteredText(text, textWidth, 3);
}

void drawStatusOverlay() {
  auto batteryY = screenHeight - 44;
  bool charging = knobby.powerStatus() == PowerStatusPowered;
  #ifdef LILYGO_WATCH_2019_WITH_TOUCH
    drawBattery(knobby.batteryPercentage(), batteryY, charging);
  #else
    if (menuMode == RootMenu) drawBattery(knobby.batteryPercentage(), batteryY, charging);
  #endif
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
  img.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  unsigned long now = millis();
  menuSize = checkMenuSize(menuMode);
  if (menuMode == InitialSetup) {
    drawSetup();
  } else if (now < randomizingMenuEndMillis + 250) {
    drawRandomizing();
  } else if (menuMode == RootMenu) {
    drawRootMenu();
  } else if (menuMode == SettingsMenu) {
    drawSettingsMenu();
  } else if (menuMode == UserList) {
    drawUserMenu();
  } else if (menuMode == DeviceList) {
    drawDeviceMenu();
  } else if (menuMode == VolumeControl) {
    drawVolumeControl();
  } else if (menuMode == NowPlaying || menuMode == SeekControl) {
    drawNowPlayingOrSeek();
  } else {
    drawPlaylistsMenu();
  }
  drawStatusOverlay();
  lastDisplayMillis = millis();
}

void drawCenteredText(const char *text, uint16_t maxWidth, uint16_t maxLines) {
  const uint16_t lineHeight = img.gFont.yAdvance + lineSpacing;
  const uint16_t centerX = floor(maxWidth / 2.0);
  const uint16_t len = strlen(text);

  uint16_t lineNumber = 0;
  uint16_t pos = 0;
  int16_t totalWidth = 0;
  uint16_t index = 0;
  uint16_t preferredBreakpoint = 0;
  uint16_t widthAtBreakpoint = 0;
  bool breakpointOnSpace = false;
  uint16_t lastDrawnPos = 0;

  img.createSprite(maxWidth, lineHeight);
  img.fillSprite(TFT_BLACK);

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

      img.setCursor(centerX - floor(lineWidth / 2.0), 0);
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
    img.setCursor(centerX - floor(totalWidth / 2.0), 0);
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

int indexOfId(const char *ids[], int count, const char *string) {
  for (auto i = 0; i < count; i++) {
    if (strcmp(string, ids[i]) == 0) return i;
  }
  return -1;
}

// is genreIndex tracked in this menu?
bool isGenreMenu(MenuModes mode) {
  return mode == GenreList || mode == SimilarList;
}

bool isPlaylistMenu(MenuModes mode) {
  return isGenreMenu(mode) || mode == PlaylistList || mode == CountryList;
}

bool isTransientMenu(MenuModes mode) {
  return mode == SimilarList || mode == PlaylistList || (!isPlaylistMenu(mode) && mode != NowPlaying);
}

unsigned long getLongPressedMillis() {
  return longPressStartedMillis == 0 ? 0 : millis() - longPressStartedMillis;
}

unsigned long getExtraLongPressedMillis() {
  long extraMillis = (long)getLongPressedMillis() - extraLongPressMillis;
  return extraMillis < 0 ? 0 : extraMillis;
}

bool shouldShowProgressBar() {
  if (menuMode == InitialSetup || WiFi.SSID().isEmpty()) return false;
  auto now = millis();
  auto waitingMillis = spotifyApiRequestStartedMillis < 0 ? 0 : now - spotifyApiRequestStartedMillis;
  return (waitingMillis > waitToShowProgressMillis) ||
         (menuMode == NowPlaying && spotifyState.durationMillis == 0 && (now < 6000 || waitingMillis > 500));
}

bool shouldShowRandom() {
  if (randomizingMenuEndMillis > 0 || knobRotatedWhileLongPressed || checkMenuSize(lastMenuMode) < 2) return false;
  return (knobHeldForRandom || getLongPressedMillis() > extraLongPressMillis) &&
         (isPlaylistMenu(lastMenuMode) || lastMenuMode == NowPlaying || lastMenuMode == SeekControl || lastMenuMode == VolumeControl);
}

bool shouldShowSimilarMenu() {
  return (isGenreMenu(lastMenuMode) || playingGenreIndex >= 0) && spotifyRefreshToken[0] != '\0';
}

bool shouldShowUsersMenu() {
  return spotifyUsers.size() > 1;
}

bool shouldShowDevicesMenu() {
  return spotifyRefreshToken[0] != '\0';
}

uint16_t checkMenuSize(MenuModes mode) {
  int rootMenuSize = SimilarList;

  switch (mode) {
    case InitialSetup:
      return wifiConnectWarning ? 2 : 0;
    case RootMenu:
      rootMenuSimilarIndex = shouldShowSimilarMenu() ? rootMenuSize++ : -1;
      rootMenuNowPlayingIndex = rootMenuSize++;
      rootMenuUsersIndex = shouldShowUsersMenu() ? rootMenuSize++ : -1;
      rootMenuDevicesIndex = shouldShowDevicesMenu() ? rootMenuSize++ : -1;
      return rootMenuSize;
    case SettingsMenu:
      return (sizeof(settingsMenuItems) / sizeof(settingsMenuItems[0]));
    case UserList:
      return spotifyUsers.size();
    case DeviceList:
      return spotifyDevices.size();
    case PlaylistList:
      return spotifyPlaylistsCount + 1; // for virtual liked songs playlist
    case CountryList:
      return COUNTRY_COUNT;
    case GenreList:
      return GENRE_COUNT;
    case SimilarList:
      return similarMenuItems.size();
    case SeekControl:
      return spotifyState.durationMillis / 1000;
    case VolumeControl:
      return 101; // 0-100%
    case NowPlaying:
      // like, shuffle, previous, play/pause, next, repeat, volume, seek if playing
      return spotifyState.durationMillis > 0 ? 8 : 7;
    default:
      return 0;
  }
}

int getGenreIndexForMenuIndex(uint16_t index, MenuModes mode) {
  switch (mode) {
    case GenreList:
      switch (genreSort) {
        case AlphabeticSort:
          return index;
        case AlphabeticSuffixSort:
          return genreIndexes_suffix[index];
        case AmbienceSort:
          return genreIndexes_background[index];
        case ModernitySort:
          return genreIndexes_modernity[index];
        case PopularitySort:
          return genreIndexes_popularity[index];
        default:
          return index;
      }
    case PlaylistList:
      return indexOfId(genrePlaylists, GENRE_COUNT, spotifyPlaylists[index].id);
    case SimilarList:
      return indexOfId(genrePlaylists, GENRE_COUNT, similarMenuItems[index].playlistId);
    default:
      return -1;
  }
}

uint16_t getIndexOfGenreIndex(const uint16_t indexes[], uint16_t indexToFind) {
  for (uint16_t i = 0; i < GENRE_COUNT; i++) {
    if (indexes[i] == indexToFind) return i;
  }
  return 0;
}

uint16_t getMenuIndexForGenreIndex(uint16_t index) {
  switch (genreSort) {
    case AlphabeticSort:
      return index;
    case AlphabeticSuffixSort:
      return getIndexOfGenreIndex(genreIndexes_suffix, index);
    case AmbienceSort:
      return getIndexOfGenreIndex(genreIndexes_background, index);
    case ModernitySort:
      return getIndexOfGenreIndex(genreIndexes_modernity, index);
    case PopularitySort:
      return getIndexOfGenreIndex(genreIndexes_popularity, index);
    default:
      return index;
  }
}

int getMenuIndexForPlaylist(const char *contextUri) {
  if (!contextUri) return -1;
  auto size = checkMenuSize(PlaylistList);
  char uri[sizeof(SpotifyState_t::contextUri)];
  for (auto i = 0; i < size; i++) {
    getPlaylistUri(uri, PlaylistList, i);
    if (strcmp(uri, spotifyState.contextUri) == 0) {
      return i;
    }
  }
  return -1;
}

void getPlaylistName(char *name, MenuModes mode, uint16_t index) {
  const auto nameSize = sizeof(SpotifyState_t::contextName);
  switch (mode) {
    case GenreList:
      strncpy(name, genres[getGenreIndexForMenuIndex(index, mode)], nameSize);
      break;
    case CountryList:
      snprintf(name, nameSize, countries[index]);
      break;
    case PlaylistList:
      if (index == 0) {
        strncpy(name, "Liked Songs", nameSize);
      } else {
        strncpy(name, spotifyPlaylists[index - 1].name.c_str(), nameSize);
      }
      break;
    case SimilarList:
      if (similarMenuItems[menuIndex].name[0] != '\0') {
        strncpy(name, similarMenuItems[index].name, nameSize);
      } else {
        auto genre = getGenreIndexForMenuIndex(index, mode);
        if (genre >= 0 && genre < GENRE_COUNT) strncpy(name, genres[genre], nameSize);
      }
      break;
    default:
      break;
  }
}

void getContextName(char *name, const char *contextUri) {
  const String uri = String(contextUri);
  if (uri.startsWith("spotify:user:") && uri.endsWith(":collection")) {
    strcpy(name, "Liked Songs");
  } else if (uri.startsWith(spotifyPlaylistContextPrefix)) {
    auto id = uri.substring(uri.lastIndexOf(":") + 1).c_str();
    auto countryIndex = indexOfId(countryPlaylists, COUNTRY_COUNT, id);
    auto genreIndex = indexOfId(genrePlaylists, GENRE_COUNT, id);
    if (countryIndex >= 0) {
      getPlaylistName(name, CountryList, countryIndex);
    } else if (genreIndex >= 0) {
      auto oldGenreSort = genreSort;
      genreSort = AlphabeticSort;
      getPlaylistName(name, GenreList, genreIndex);
      genreSort = oldGenreSort;
    } else if (spotifyPlaylistsLoaded) {
      auto playlistIndex = getMenuIndexForPlaylist(contextUri);
      if (playlistIndex >= 0) getPlaylistName(name, PlaylistList, playlistIndex);
    }
  }
}

void getPlaylistUri(char *uri, MenuModes mode, uint16_t index) {
  const char *playlistUri = "spotify:playlist:%s";
  const auto uriSize = sizeof(SpotifyState_t::contextUri);
  switch (mode) {
    case GenreList:
      snprintf(uri, uriSize, playlistUri, genrePlaylists[getGenreIndexForMenuIndex(index, mode)]);
      break;
    case CountryList:
      snprintf(uri, uriSize, playlistUri, countryPlaylists[index]);
      break;
    case PlaylistList:
      if (index == 0) {
        snprintf(uri, uriSize, "spotify:user:%s:collection", activeSpotifyUser->name);
      } else {
        snprintf(uri, uriSize, playlistUri, spotifyPlaylists[index - 1].id);
      }
      break;
    case SimilarList:
      snprintf(uri, uriSize, playlistUri, similarMenuItems[index].playlistId);
      break;
    default:
      break;
  }
}

void setMenuIndex(uint16_t newMenuIndex) {
  if (menuSize == 0) {
    menuIndex = 0;
    return;
  }
  menuIndex = newMenuIndex % menuSize;

  int newGenreIndex = -1;
  switch (menuMode) {
    case PlaylistList:
      playlistIndex = menuIndex;
      break;
    case CountryList:
      countryIndex = menuIndex;
      break;
    case GenreList:
      genreIndex = getGenreIndexForMenuIndex(menuIndex, menuMode);
      break;
    case SimilarList:
      if (similarMenuItems[menuIndex].name[0] == '\0') {
        newGenreIndex = indexOfId(genrePlaylists, GENRE_COUNT, similarMenuItems[menuIndex].playlistId);
        if (newGenreIndex < 0) {
          genreIndex = similarMenuGenreIndex;
        } else {
          genreIndex = newGenreIndex;
        }
      } else {
        genreIndex = max(0, similarMenuGenreIndex);
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
  WiFi.begin(wifiManager->WiFi_SSID().c_str(), wifiManager->WiFi_Pass().c_str());
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(100);
  tft.fillScreen(TFT_BLACK);
  esp_deep_sleep_start();
}

void startDeepSleep() {
  log_i("Entering deep sleep.");
  if (isTransientMenu(menuMode)) setMenuMode(GenreList, getMenuIndexForGenreIndex(genreIndex));
  if (isTransientMenu(lastMenuMode)) lastMenuMode = GenreList;
  if (isTransientMenu(lastPlaylistMenuMode)) lastPlaylistMenuMode = GenreList;
  tft.fillScreen(TFT_BLACK);
  WiFi.disconnect(true);
  digitalWrite(ADC_EN, LOW);
  digitalWrite(TFT_BL, LOW);
  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(TFT_SLPIN);
  struct timeval tod;
  gettimeofday(&tod, NULL);
  lastSleepSeconds = tod.tv_sec;
  rtc_gpio_isolate(GPIO_NUM_0); // button 1
  rtc_gpio_isolate(GPIO_NUM_35); // button 2
  rtc_gpio_isolate(GPIO_NUM_39);
  rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_A_PIN);
  rtc_gpio_isolate((gpio_num_t)ROTARY_ENCODER_B_PIN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_ENCODER_BUTTON_PIN, LOW);
  #if CORE_DEBUG_LEVEL < ARDUHAL_LOG_LEVEL_DEBUG
  esp_deep_sleep_disable_rom_logging();
  #endif
  esp_deep_sleep_start();
}

void startRandomizingMenu(bool autoplay) {
  unsigned long now = millis();
  if (now > randomizingMenuEndMillis) {
    randomizingMenuEndMillis = now + randomizingLengthMillis;
    randomizingMenuTicks = 1;
    randomizingMenuAutoplay = autoplay;
    if (randomizingMenuAutoplay) {
      spotifyResetProgress();
      nextCurrentlyPlayingMillis = 0;
      spotifyActionQueue.clear();
    }
    setMenuMode(lastPlaylistMenuMode, 0);
  }
}

void playUri(const char *uri, const char *name) {
  spotifyResetProgress();
  spotifyState.isPlaying = true;
  strncpy(spotifyPlayUri, uri, sizeof(spotifyPlayUri));
  strncpy(spotifyState.contextUri, uri, sizeof(spotifyState.contextUri));
  strncpy(spotifyState.contextName, name, sizeof(spotifyState.contextName) - 1);
  spotifyQueueAction(PlayPlaylist);

  lastMenuMode = menuMode;
  lastMenuIndex = menuIndex;
  if (isPlaylistMenu(menuMode)) lastPlaylistMenuMode = menuMode;
  setMenuMode(NowPlaying, PlayPauseButton);
}

void playMenuPlaylist(MenuModes mode, uint16_t index) {
  char name[sizeof(SpotifyState_t::contextName)];
  char uri[sizeof(SpotifyState_t::contextUri)];
  getPlaylistName(name, mode, index);
  getPlaylistUri(uri, mode, index);
  if (!uri) return;
  playUri(uri, name);
  if (mode == GenreList || mode == SimilarList) {
    playingGenreIndex = getGenreIndexForMenuIndex(index, mode);
  } else if (mode == CountryList) {
    playingCountryIndex = index;
  }
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

  if (doc.containsKey("configPassword")) knobby.setPassword(doc["configPassword"]);
  firmwareURL = doc["firmwareURL"] | KNOBBY_FIRMWARE_URL;
  if (firmwareURL.isEmpty()) firmwareURL = KNOBBY_FIRMWARE_URL;
  flipDisplay = doc["flipDisplay"];
  pulseCount = doc["pulseCount"];
  if (pulseCount <= 0 || pulseCount > 8) pulseCount = ROTARY_ENCODER_PULSE_COUNT;
  wifiSSID = doc["wifiSSID"] | wifiManager->WiFi_SSID();
  wifiPassword = doc["wifiPassword"] | wifiManager->WiFi_Pass();

  JsonArray usersArray = doc["users"];
  spotifyUsers.clear();
  for (JsonObject jsonUser : usersArray) {
    const char *name = jsonUser["name"];
    const char *token = jsonUser["token"];
    const char *selectedDeviceId = jsonUser["selectedDeviceId"];

    SpotifyUser_t user;
    strncpy(user.name, name, sizeof(user.name) - 1);
    strncpy(user.refreshToken, token, sizeof(user.refreshToken) - 1);
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

  if (!firmwareURL.equals(KNOBBY_FIRMWARE_URL)) doc["firmwareURL"] = firmwareURL;
  if (flipDisplay) doc["flipDisplay"] = true;
  if (pulseCount != ROTARY_ENCODER_PULSE_COUNT) doc["pulseCount"] = pulseCount;

  JsonArray usersArray = doc.createNestedArray("users");

  for (SpotifyUser_t user : spotifyUsers) {
    JsonObject obj = usersArray.createNestedObject();
    obj["name"] = user.name;
    obj["token"] = user.refreshToken;
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
    tft.drawFastHLine(6, lineDivider, dividerWidth, TFT_LIGHTBLACK);
    tft.setCursor(textStartX, lineOne);
    img.setTextColor(TFT_WHITE, TFT_BLACK);
    drawCenteredText("updating...", textWidth, 1);
  }
  tft.drawFastHLine(6, lineDivider, round(((float)progress / total) * dividerWidth), TFT_LIGHTGREY);
  char status[5];
  sprintf(status, "%u%%", (progress / (total / 100)));
  tft.setCursor(textStartX, lineTwo);
  drawCenteredText(status, textWidth, 1);
  ESP_ERROR_CHECK(esp_task_wdt_reset());
}

bool spotifyActionIsQueued(SpotifyActions action) {
  return contains(spotifyActionQueue, action);
}

bool spotifyQueueAction(SpotifyActions action) {
  if (spotifyActionIsQueued(action)) return false;
  spotifyActionQueue.push_back(action);
  invalidateDisplay();
  return true;
}

int spotifyApiRequest(const char *method, const char *endpoint, const char *content = "") {
  uint32_t ts = millis();
  spotifyApiRequestStartedMillis = ts;
  String path = String("/v1/") + endpoint;
  log_i("%s %s %s", method, path.c_str(), content);

  spotifyHttp.begin(spotifyWifiClient, "api.spotify.com", 443, path);
  spotifyHttp.addHeader("Authorization", "Bearer " + String(spotifyAccessToken));
  if (strlen(content) == 0) spotifyHttp.addHeader("Content-Length", "0");

  int code;
  if (String(method) == "GET") {
    code = spotifyHttp.GET();
  } else {
    code = spotifyHttp.sendRequest(method, content);
  }

  if (code == 401) {
    log_e("401 Unauthorized, clearing spotifyAccessToken");
    spotifyAccessToken[0] = '\0';
    spotifyGettingToken = true;
    spotifyActionQueue.push_front(GetToken);
  }

  return code;
}

void spotifyApiRequestEnded() {
  spotifyHttp.end();
  spotifyApiRequestStartedMillis = -1;
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
  const char *method = "POST";
  const char *path = "/api/token";
  char requestContent[768];
  bool success = false;

  if (grant_type == gt_authorization_code) {
    snprintf(requestContent, sizeof(requestContent),
             "client_id=%s&grant_type=authorization_code&redirect_uri=http%%3A%%2F%%2Fknobby.local%%2Fcallback&code=%s&code_verifier=%s",
             spotifyClientId, code, spotifyCodeVerifier);
  } else {
    snprintf(requestContent, sizeof(requestContent),
             "client_id=%s&grant_type=refresh_token&refresh_token=%s",
             spotifyClientId, code);
  }
  log_i("%s %s", method, path);

  HTTPClient http;
  StreamString payload;
  http.setUserAgent("Knobby/1.0");
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  http.setReuse(false);
  http.begin(spotifyWifiClient, "accounts.spotify.com", 443, path);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpCode = http.sendRequest("POST", requestContent);
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
            } else if (grant_type == gt_refresh_token && activeSpotifyUser != nullptr) {
              strncpy(activeSpotifyUser->refreshToken, spotifyRefreshToken, sizeof(activeSpotifyUser->refreshToken));
              writeDataJson();
            }
          }
        }
      }
    } else {
      log_e("Unable to parse response payload: %s", payload.c_str());
      delay(4000);
    }
  } else if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
    setStatusMessage("can't connect!", spotifyPollInterval);
    delay(spotifyPollInterval);
  } else if (httpCode < 0) {
    // retry immediately
  } else {
    log_e("%d - %s", httpCode, payload.c_str());
    Serial.flush();
    if (httpCode == 400 && grant_type == gt_refresh_token) {
      spotifyUsers.clear();
      writeDataJson();
      tft.fillScreen(TFT_BLACK);
      ESP.restart();
    }
    inactivityMillis = 15000;
    delay(8000);
  }

  if (success) {
    if (grant_type == gt_authorization_code) {
      spotifyQueueAction(CurrentProfile);
    } else if (menuMode == DeviceList) {
      spotifyQueueAction(GetDevices);
    } else if (menuMode == PlaylistList) {
      spotifyQueueAction(GetPlaylists);
    } else if (spotifyPlayUri[0] != '\0') {
      spotifyQueueAction(PlayPlaylist);
    } else {
      spotifyQueueAction(CurrentlyPlaying);
    }
  }
  spotifyGettingToken = false;
}

void spotifyCurrentlyPlaying() {
  nextCurrentlyPlayingMillis = 0;
  if (spotifyAccessToken[0] == '\0' || !activeSpotifyUser) return;
  int statusCode = spotifyApiRequest("GET", "me/player?market=from_token");

  if (statusCode == 200) {
    DynamicJsonDocument json(24576);
    DeserializationError error = deserializeJson(json, spotifyHttp.getStream());

    if (!error) {
      spotifyStateLoaded = true;
      spotifyState.lastUpdateMillis = millis();
      spotifyState.isShuffled = json["shuffle_state"];
      spotifyState.progressMillis = spotifyState.estimatedProgressMillis = json["progress_ms"];

      const String repeatState = json["repeat_state"];
      if (repeatState == "track") {
        spotifyState.repeatMode = RepeatTrack;
      } else if (repeatState == "context") {
        spotifyState.repeatMode = RepeatContext;
      } else {
        spotifyState.repeatMode = RepeatOff;
      }

      JsonObject context = json["context"];
      if (!context.isNull() && !context["uri"].isNull()) {
        strncpy(spotifyState.contextUri, context["uri"], sizeof(spotifyState.contextUri));
        getContextName(spotifyState.contextName, spotifyState.contextUri);
        if (strcmp(context["type"], "playlist") == 0) {
          const char *id = strrchr(context["uri"], ':') + 1;
          playingGenreIndex = indexOfId(genrePlaylists, GENRE_COUNT, id);
          playingCountryIndex = indexOfId(countryPlaylists, COUNTRY_COUNT, id);
        } else {
          playingCountryIndex = -1;
          playingGenreIndex = -1;
        }
      } else {
        playingCountryIndex = -1;
        playingGenreIndex = -1;
        spotifyState.contextName[0] = '\0';
      }

      JsonObject item = json["item"];

      if (!item.isNull()) {
        emptyCurrentlyPlayingResponses = 0;
        spotifyState.durationMillis = item["duration_ms"];
        const char *id = item["linked_from"]["id"] | item["id"];
        strncpy(spotifyState.trackId, id, SPOTIFY_ID_SIZE);
        if (menuMode == SeekControl) {
          menuSize = checkMenuSize(SeekControl);
          setMenuIndex(spotifyState.progressMillis / 1000);
        }
        strncpy(spotifyState.name, item["name"], sizeof(spotifyState.name) - 1);

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
        spotifyState.isPrivateSession = jsonDevice["is_private_session"];
      }

      if (!spotifyState.isPrivateSession) spotifyState.isPlaying = json["is_playing"];
      spotifyState.disallowsSkippingNext = false;
      spotifyState.disallowsSkippingPrev = false;
      spotifyState.disallowsTogglingShuffle = false;
      spotifyState.disallowsTogglingRepeatContext = false;
      spotifyState.disallowsTogglingRepeatTrack = false;
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
        } else if (strcmp(key, "toggling_repeat_context") == 0) {
          spotifyState.disallowsTogglingRepeatContext = disallow.value();
        } else if (strcmp(key, "toggling_repeat_track") == 0) {
          spotifyState.disallowsTogglingRepeatTrack = disallow.value();
        }
      }

      auto now = millis();
      if (spotifyState.isPlaying && spotifyState.durationMillis > 0) {
        // Check if current song is about to end
        uint32_t remainingMillis = spotifyState.durationMillis - spotifyState.progressMillis;
        if (remainingMillis < spotifyPollInterval) {
          // Refresh at the end of current song,
          // without considering remaining polling delay
          nextCurrentlyPlayingMillis = now + remainingMillis + 100;
        }
      }
      if (spotifyState.isPlaying && nextCurrentlyPlayingMillis == 0 && !spotifyState.isPrivateSession) {
        nextCurrentlyPlayingMillis = now + (spotifyState.durationMillis == 0 ? 2000 : spotifyPollInterval);
      }
      if (spotifyState.name[0] != '\0') {
        if ((lastInputMillis == 0 && menuMode != NowPlaying) ||
            (lastInputMillis == menuClickedMillis && (menuMode == DeviceList || menuMode == UserList))) {
          setMenuMode(NowPlaying, PlayPauseButton);
        }
      }
      invalidateDisplay();
    } else {
      log_e("Heap free: %d", ESP.getFreeHeap());
      log_e("Error %s parsing response", error.c_str());
    }
  } else if (statusCode == 204) {
    bool trackWasLoaded = spotifyState.name[0] != '\0';
    spotifyStateLoaded = true;
    spotifyState.isShuffled = false;
    spotifyState.repeatMode = RepeatOff;
    spotifyResetProgress();
    if (!trackWasLoaded && lastInputMillis == 0 && millis() < 10000 && menuMode == NowPlaying) {
      setMenuMode(GenreList, getMenuIndexForGenreIndex(genreIndex));
    } else if (lastInputMillis == menuClickedMillis && menuMode == UserList) {
      spotifyQueueAction(GetDevices);
      setMenuMode(DeviceList, 0);
    }
    if (spotifyPlayAtMillis > 0 && millis() - spotifyPlayAtMillis < SPOTIFY_WAIT_MILLIS * 3) {
      nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
    } else {
      nextCurrentlyPlayingMillis = millis() + spotifyPollInterval + emptyCurrentlyPlayingResponses++ * 1000;
    }
  } else if (statusCode < 0) {
    nextCurrentlyPlayingMillis = millis() + emptyCurrentlyPlayingResponses++ * 100; // retry faster
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getString().c_str());
    setStatusMessage("spotify error");
    nextCurrentlyPlayingMillis = millis() + spotifyPollInterval;
  }
  spotifyApiRequestEnded();
}

/**
 * Get information about the current Spotify user
 */
void spotifyCurrentProfile() {
  if (spotifyAccessToken[0] == '\0') return;

  int statusCode = spotifyApiRequest("GET", "me");
  if (statusCode == 200) {
    DynamicJsonDocument json(2000);
    DeserializationError error = deserializeJson(json, spotifyHttp.getStream());

    if (!error) {
      const char *displayName = json["display_name"];
      strncpy(activeSpotifyUser->name, displayName, sizeof(activeSpotifyUser->name) - 1);
      writeDataJson();
      if (menuMode == UserList || (menuMode == SettingsMenu && menuIndex == SettingsAddUser)) {
        setMenuMode(UserList, spotifyUsers.size() - 1);
      } else {
        invalidateDisplay();
      }
    } else {
      log_e("Unable to parse response payload:\n  %s", spotifyHttp.getString().c_str());
    }
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getString().c_str());
  }

  if (!spotifyDevicesLoaded) spotifyQueueAction(GetDevices);
  spotifyApiRequestEnded();
}

bool spotifyRetryError(int statusCode) {
  if (statusCode == 404) {
    log_w("Spotify device not found");
    if (activeSpotifyDeviceId[0] != '\0') {
      setActiveDevice(nullptr);
      activeSpotifyDeviceId[0] = '\0';
      spotifyDevicesLoaded = false;
    }
    spotifyRetryAction = spotifyAction;
    spotifyQueueAction(GetDevices);
    setMenuMode(DeviceList, 0);
    setStatusMessage("select device");
    return true;
  } else if (statusCode == 401) {
    return true;
  } else if (statusCode >= 400) {
    log_e("HTTP %d - %s", statusCode, spotifyHttp.getString().c_str());
  }
  return false;
}

void spotifyNext() {
  int statusCode = spotifyApiRequest("POST", "me/player/next");
  spotifyResetProgress(true);
  if (statusCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingPrev = false;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getString().c_str());
  }
  spotifyApiRequestEnded();
};

void spotifyPrevious() {
  int statusCode = spotifyApiRequest("POST", "me/player/previous");
  spotifyResetProgress(true);
  if (statusCode == 204) {
    spotifyState.isPlaying = true;
    spotifyState.disallowsSkippingNext = false;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getString().c_str());
  }
  spotifyApiRequestEnded();
};

void spotifySeek() {
  if (spotifyAccessToken[0] == '\0' || spotifySeekToMillis < 0) return;
  spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
  char path[40];
  snprintf(path, sizeof(path), "me/player/seek?position_ms=%d", spotifySeekToMillis);
  int statusCode = spotifyApiRequest("PUT", path);
  if (statusCode == 204) {
    spotifyState.lastUpdateMillis = millis();
    spotifyState.progressMillis = spotifyState.estimatedProgressMillis = spotifySeekToMillis;
    nextCurrentlyPlayingMillis = spotifyState.lastUpdateMillis + SPOTIFY_WAIT_MILLIS;
    invalidateDisplay();
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getString().c_str());
    spotifyResetProgress(true);
  }
  spotifyApiRequestEnded();
};

void spotifyToggle() {
  if (spotifyAccessToken[0] == '\0') return;

  bool wasPlaying = spotifyState.isPlaying;
  if (!wasPlaying) spotifyPlayAtMillis = millis();
  spotifyState.isPlaying = !wasPlaying;
  spotifyState.lastUpdateMillis = millis();
  spotifyState.progressMillis = spotifyState.estimatedProgressMillis;

  int statusCode;
  if (activeSpotifyDeviceId[0] != '\0' && !wasPlaying) {
    char path[90];
    snprintf(path, sizeof(path), "me/player/play?device_id=%s", activeSpotifyDeviceId);
    statusCode = spotifyApiRequest("PUT", path);
  } else {
    statusCode = spotifyApiRequest("PUT", wasPlaying ? "me/player/pause" : "me/player/play");
  }
  spotifyRetryError(statusCode);

  if (statusCode == 204) {
    spotifyState.lastUpdateMillis = millis();
    nextCurrentlyPlayingMillis = spotifyState.lastUpdateMillis + SPOTIFY_WAIT_MILLIS;
    invalidateDisplay();
  } else {
    spotifyResetProgress(true);
    nextCurrentlyPlayingMillis = 1;
  }
  spotifyApiRequestEnded();
};

void spotifyPlayPlaylist() {
  if (spotifyAccessToken[0] == '\0' || spotifyPlayUri[0] == '\0') return;
  spotifyPlayAtMillis = millis();

  char requestContent[128];
  snprintf(requestContent, sizeof(requestContent), "{\"context_uri\":\"%s\"}", spotifyPlayUri);
  int statusCode;
  if (activeSpotifyDeviceId[0] != '\0') {
    char path[90];
    snprintf(path, sizeof(path), "me/player/play?device_id=%s", activeSpotifyDeviceId);
    statusCode = spotifyApiRequest("PUT", path, requestContent);
  } else {
    statusCode = spotifyApiRequest("PUT", "me/player/play", requestContent);
  }
  bool success = statusCode == 204;
  bool retry = spotifyRetryError(statusCode);

  spotifyResetProgress(true);
  if (success) {
    spotifyState.isPlaying = true;
    strncpy(spotifyState.contextUri, spotifyPlayUri, SPOTIFY_ID_SIZE);
  } else if(!retry) {
    spotifyState.contextUri[0] = '\0';
  }
  if (!retry) spotifyPlayUri[0] = '\0';
  spotifyApiRequestEnded();
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
  spotifyState.isLiked = false;
  spotifyState.checkedLike = false;
  nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  if (!spotifyState.isPrivateSession) spotifyState.isPlaying = false;
  if (!keepContext) {
    nowPlayingDisplayMillis = 0;
    playingCountryIndex = -1;
    playingGenreIndex = -1;
    spotifyState.contextName[0] = '\0';
    spotifyState.contextUri[0] = '\0';
  }
  if (menuMode == SeekControl) setMenuIndex(0);
  invalidateDisplay();
};

void spotifyGetDevices() {
  if (spotifyAccessToken[0] == '\0') return;
  int statusCode = spotifyApiRequest("GET", "me/player/devices");
  if (statusCode == 200) {
    DynamicJsonDocument doc(5000);
    DeserializationError error = deserializeJson(doc, spotifyHttp.getStream());

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
      }
      if (menuMode == DeviceList) {
        setMenuMode(DeviceList, activeDeviceIndex < 0 ? 0 : activeDeviceIndex);
        invalidateDisplay(true);
      }
    }
  }
  spotifyApiRequestEnded();
  invalidateDisplay();
}

void spotifySetVolume() {
  if (activeSpotifyDevice == nullptr || spotifySetVolumeTo < 0) {
    spotifySetVolumeTo = -1;
    return;
  }
  int setpoint = spotifySetVolumeTo;
  char path[74];
  snprintf(path, sizeof(path), "me/player/volume?volume_percent=%d", setpoint);
  spotifyApiRequest("PUT", path);
  if (activeSpotifyDevice != nullptr) activeSpotifyDevice->volumePercent = setpoint;
  if (spotifySetVolumeTo == setpoint) spotifySetVolumeTo = -1;
  spotifyApiRequestEnded();
}

void spotifyCheckLike() {
  if (spotifyAccessToken[0] == '\0' || spotifyState.trackId[0] == '\0') return;

  int statusCode;
  char path[46];
  snprintf(path, sizeof(path), "me/tracks/contains?ids=%s", spotifyState.trackId);
  statusCode = spotifyApiRequest("GET", path);

  if (statusCode == 200) {
    StaticJsonDocument<16> json;
    DeserializationError error = deserializeJson(json, spotifyHttp.getStream());
    if (!error) {
      bool liked = json[0];
      if (liked != spotifyState.isLiked) {
        spotifyState.isLiked = liked;
        invalidateDisplay();
      }
    } else {
      log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
    }
    spotifyState.checkedLike = true;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
  }
  spotifyApiRequestEnded();
};

void spotifyToggleLike() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyState.isLiked = !spotifyState.isLiked;
  invalidateDisplay();

  int statusCode;
  char path[37];
  snprintf(path, sizeof(path), "me/tracks?ids=%s", spotifyState.trackId);
  statusCode = spotifyApiRequest(spotifyState.isLiked ? "PUT" : "DELETE", path);

  if (statusCode > 204) {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
    spotifyState.isLiked = !spotifyState.isLiked;
    invalidateDisplay();
  }
  spotifyApiRequestEnded();
};

void spotifyToggleShuffle() {
  if (spotifyAccessToken[0] == '\0') return;

  spotifyState.isShuffled = !spotifyState.isShuffled;
  invalidateDisplay();

  int statusCode;
  char path[33];
  snprintf(path, sizeof(path), "me/player/shuffle?state=%s", spotifyState.isShuffled ? "true" : "false");
  statusCode = spotifyApiRequest("PUT", path);

  if (statusCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
    spotifyState.isShuffled = !spotifyState.isShuffled;
    invalidateDisplay();
  }
  spotifyApiRequestEnded();
};

void spotifyToggleRepeat() {
  if (spotifyAccessToken[0] == '\0') return;
  int statusCode;
  const char *pathTemplate = "me/player/repeat?state=%s";
  char path[35];

  if (spotifyState.repeatMode == RepeatOff && !spotifyState.disallowsTogglingRepeatContext) {
    spotifyState.repeatMode = RepeatContext;
    snprintf(path, sizeof(path), pathTemplate, "context");
  } else if ((spotifyState.repeatMode == RepeatOff || spotifyState.repeatMode == RepeatContext) &&
             !spotifyState.disallowsTogglingRepeatTrack) {
    spotifyState.repeatMode = RepeatTrack;
    snprintf(path, sizeof(path), pathTemplate, "track");
  } else {
    spotifyState.repeatMode = RepeatOff;
    snprintf(path, sizeof(path), pathTemplate, "off");
  }
  invalidateDisplay();
  statusCode = spotifyApiRequest("PUT", path);

  if (statusCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
    spotifyState.repeatMode = RepeatOff;
    invalidateDisplay();
  }
  spotifyApiRequestEnded();
};

void spotifyTransferPlayback() {
  if (activeSpotifyDevice == nullptr) return;
  char requestContent[84];
  snprintf(requestContent, sizeof(requestContent), "{\"device_ids\":[\"%s\"]}", activeSpotifyDeviceId);
  int statusCode;
  statusCode = spotifyApiRequest("PUT", "me/player", requestContent);
  if (statusCode == 204) {
    nextCurrentlyPlayingMillis = millis() + SPOTIFY_WAIT_MILLIS;
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
  }
  spotifyApiRequestEnded();
};

void spotifyGetPlaylistDescription() {
  if (spotifyAccessToken[0] == '\0') return;
  const char *activePlaylistId = genrePlaylists[genreIndex];
  int statusCode;
  char url[53];
  snprintf(url, sizeof(url), "playlists/%s?fields=description", activePlaylistId);
  statusCode = spotifyApiRequest("GET", url);

  if (statusCode == 200) {
    DynamicJsonDocument json(3000);
    DeserializationError error = deserializeJson(json, spotifyHttp.getStream());
    if (!error) {
      String description = json["description"];

      similarMenuItems.clear();
      const auto prefixLength = strlen(spotifyPlaylistContextPrefix);
      int urlPosition = description.indexOf(spotifyPlaylistContextPrefix);
      while (urlPosition > 0) {
        SimilarItem_t item;
        int idStart = urlPosition + prefixLength;
        int idEnd = idStart + SPOTIFY_ID_SIZE;
        strlcpy(item.playlistId, description.c_str() + idStart, sizeof(item.playlistId));

        int nameStart = description.indexOf(">", idEnd) + 1;
        int nameEnd = description.indexOf("<", nameStart);
        if (nameStart > 0 && nameEnd > 0) {
          String name = description.substring(nameStart, nameEnd);
          int matchingGenreIndex = indexOfId(genrePlaylists, GENRE_COUNT, item.playlistId);
          if (name == "Intro" || name == "Pulse" || name == "Edge" || matchingGenreIndex >= 0) {
            if(matchingGenreIndex < 0) {
              strncpy(item.name, name.c_str(), sizeof(item.name) - 1);
            } else {
              item.name[0] = '\0';
            }
            similarMenuItems.push_back(item);
          }

          urlPosition = description.indexOf(spotifyPlaylistContextPrefix, nameEnd);
        } else {
          urlPosition = description.indexOf(spotifyPlaylistContextPrefix, urlPosition + prefixLength);
        }
      }
      setMenuMode(SimilarList, 0);
    }
  } else {
    log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
  }
  spotifyApiRequestEnded();
};

void spotifyGetPlaylists() {
  if (spotifyAccessToken[0] == '\0') return;
  int statusCode;
  int16_t nextOffset = 0;
  uint16_t limit = 50;
  uint16_t offset = 0;
  char url[64];

  spotifyPlaylists.clear();
  spotifyPlaylistsCount = 0;

  while (nextOffset >= 0) {
    snprintf(url, sizeof(url), "me/playlists/?fields=items(id,name)&limit=%d&offset=%d", limit, nextOffset);
    statusCode = spotifyApiRequest("GET", url);
    if (statusCode == 200) {
      DynamicJsonDocument json(6000);
      DeserializationError error = deserializeJson(json, spotifyHttp.getStream());
      if (!error) {
        spotifyPlaylistsCount = json["total"];
        limit = json["limit"];
        offset = json["offset"];

        JsonArray items = json["items"];
        for (auto item : items) {
          const char *id = item["id"];
          const char *name = item["name"];
          SpotifyPlaylist_t playlist;
          strncpy(playlist.id, id, SPOTIFY_ID_SIZE);
          playlist.name = String(name);
          spotifyPlaylists.push_back(playlist);
        }

        uint32_t heap = ESP.getFreeHeap();
        bool heapTooSmall = heap < 80000;

        spotifyPlaylistsLoaded = true;
        if (menuMode == PlaylistList) {
          menuSize = checkMenuSize(PlaylistList);
          invalidateDisplay();
        }

        if (json["next"].isNull() || heapTooSmall) {
          nextOffset = -1;
          if (heapTooSmall) log_e("Skipping remaining playlists, only %d bytes heap left", heap);
        } else {
          nextOffset = offset + limit;
        }
      }
    } else {
      nextOffset = -1;
      log_e("%d - %s", statusCode, spotifyHttp.getSize() > 0 ? spotifyHttp.getString() : "");
    }
  }
  spotifyApiRequestEnded();
}

void updateFirmware() {
  if (checkedForUpdateMillis > 0 && millis() - checkedForUpdateMillis < 60000) {
    setStatusMessage("up to date");
    return;
  }
  if (knobby.powerStatus() == PowerStatusOnBattery && knobby.batteryPercentage() < 10) {
    setStatusMessage("battery too low");
    return;
  }

  setStatusMessage("checking...");
  spotifyActionQueue.clear();
  updateDisplay();

  spotifyWifiClient.stop();
  WiFiClientSecure client;
  client.setCACert(s3CACertificates);
  HTTPClient http;
  http.setUserAgent("Knobby/1.0");
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  http.setReuse(false);
  log_i("GET %s", firmwareURL);
  http.begin(client, firmwareURL);
  const char * headerKeys[] = { "x-amz-meta-git-version" };
  http.collectHeaders(headerKeys, (sizeof(headerKeys) / sizeof(headerKeys[0])));
  int code = http.GET();

  if (code != 200) {
    http.end();
    setStatusMessage("update failed");
    log_e("HTTP code %d", code);
    return;
  }

  String version = http.header("x-amz-meta-git-version");
  if (!version.isEmpty()) log_i("got version header %s", version);
  if (version.equals(GIT_VERSION)) {
    checkedForUpdateMillis = millis();
    setStatusMessage("no update yet");
    log_i("version matches, no update needed!");
    return;
  }

  updateContentLength = http.getSize();
  if (!Update.begin(updateContentLength > 0 ? updateContentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
  }
  disableCore1WDT();
  Update.writeStream(http.getStream());
  http.end();

  if (!Update.end(true)){
    updateContentLength = 0;
    Update.printError(Serial);
    enableCore1WDT();
    setStatusMessage("update failed");
    invalidateDisplay(true);
  } else {
    log_i("OTA: update complete");
    Serial.flush();
    delay(100);
    tft.fillScreen(TFT_BLACK);
    ESP.restart();
  }
}
