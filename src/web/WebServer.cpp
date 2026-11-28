#include "WebServer.h"
#include "../clock/ClockTools.h" // (after v111b)
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../config/WiFiConfig.h"
#include "../storage/PreferencesManager.h"
#include "../video/Scanlines.h"
#include "../video/Integer1080.h" // (after v123j) edgesFlushNow
// 3X4X: one language per firmware; the English web comes with -DWEBUI_LANG_EN (env gbsc_en)
#ifdef WEBUI_LANG_EN
#include "../../webui/build/webui_html_en.h"
#else
#include "../../webui/build/webui_html.h"
#endif
#include "../core/slot.h"
#include <LittleFS.h>
#include <ArduinoOTA.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
#else
#include <Update.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#endif

// Empty string for WiFi.begin() when no password
static const String emptyWiFiPassword = "";
// [patch web-update] set when the current firmware upload was refused or failed
static bool webUpdateRejected = false;
// [patch web-update] set only after the new firmware was fully written and verified
static bool webUpdateDone = false;
static bool webUpdateActive = false;
static unsigned long webUpdateLastChunk = 0;

bool webUpdateBusy()
{
    if (webUpdateActive && (millis() - webUpdateLastChunk) > 10000UL) {
        webUpdateActive = false; // upload stalled or aborted: resume normal operation
    }
    return webUpdateActive;
}

void startWebserver()
{
    persWM.setApCredentials(ap_ssid, ap_password);
    persWM.onConnect([]() {
        SerialM.print(F("(WiFi): STA mode connected; IP: "));
        SerialM.println(WiFi.localIP().toString());
#ifdef ESP8266
        if (MDNS.begin(device_hostname_partial, WiFi.localIP())) { // MDNS request for gbscontrol.local
            //Serial.println(F("MDNS started"));
            MDNS.addService("http", "tcp", 80); // Add service to MDNS-SD
            MDNS.announce();
        }
#else
        if (MDNS.begin(device_hostname_partial)) { // MDNS request for gbscontrol.local
            //Serial.println(F("MDNS started"));
            MDNS.addService("http", "tcp", 80); // Add service to MDNS-SD
            // MDNS.announce(); // Not needed on ESP32
        }
#endif
        SerialM.println(FPSTR(st_info_string));
    });
    persWM.onAp([]() {
        SerialM.println(FPSTR(ap_info_string));
        // add mdns announce here as well?
    });

#ifdef ESP8266
    disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
        Serial.print(F("Station disconnected, reason: "));
        Serial.println(event.reason);
    });
#else
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print(F("Station disconnected, reason: "));
        Serial.println(info.wifi_sta_disconnected.reason);
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        //Serial.println(F("sending web page"));
        if (ESP.getFreeHeap() > 10000) {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/html", webui_html, webui_html_len);
            response->addHeader("Content-Encoding", "gzip");
            request->send(response);
        }
    });

    server.on("/sc", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();
            //Serial.print(F("got serial request params: "));
            //Serial.println(params);
            if (params > 0) {
                const AsyncWebParameter *p = request->getParam(0);
                //Serial.println(p->name());
                serialCommandBuffer += p->name().charAt(0);

                // hack, problem with '+' command received via url param
                if (serialCommandBuffer.charAt(serialCommandBuffer.length() - 1) == ' ') {
                    serialCommandBuffer.setCharAt(serialCommandBuffer.length() - 1, '+');
                }
            }
            request->send(200); // reply
        }
    });

    server.on("/uc", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();
            //Serial.print(F("got user request params: "));
            //Serial.println(params);
            if (params > 0) {
                const AsyncWebParameter *p = request->getParam(0);
                //Serial.println(p->name());
                userCommandBuffer += p->name().charAt(0);
            }
            request->send(200); // reply
        }
    });

    // v54: image commands, /img?<n>, n = 0..9 white balance R/G/B and colour cast U/V (+ and -); 10 = 720p x3 16:9 (v55)
    server.on("/img", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000 && request->params() > 0) {
            const String n = request->getParam(0)->name();
            const int code = n.toInt();
            // v72: codes 0..25 are the original 'a'..'z'; 26 and up continue at '0' so the new buttons have somewhere to go
            // without disturbing the letters already in use (the character is only a dispatch key inside the main loop).
            // v107j: 45 is now taken (console profile PC Engine). v107m: 46 / 47 are the wide / fill size steps. v107n: 48..50 the Líneas row, 51 / 52 the vertical memory window. v107o: 53 = 720p x3 in its last shape, 54 = ver huella. (after v107r) 55..57 the three undo buttons (move, edge, scale). (after v107v) 58 / 59 colour export / import; 60..73 the console profiles without measurements; (after v107z) 74 the Dreamcast's 4:3 / 16:9 in 1080p, and 74 is the last: 75 would land on 'a' (code 0). (A code 75 or above would need its own character: 'a'..'z' are codes 0..25.) A new code needs this limit raised AND a mapping in
            // the main loop that does not land on a character another command already uses.
            if (n.length() >= 1 && n.length() <= 2 && code >= 0 && code <= 97 && // (after v127i) 97 force NTSC presets; (after v127c) 93 register test previous, 94..96 its groups; (after v126) 91 ignore false pulses; (after v126b) 92 test recipe; (after v124d) 88 / 89 the PC Engine's 320 / 352 px; (after v125) 90 the ODV 60 Hz recipe
                // (after v120c) 85 / 86 the console's gain, 87 factory memory
                // (after v111c) 82..84 memory reset, FTL phase -/+
                // (after v111) 77..80 the register tests of Developer; (after v111b) 81 exact output rate
                 imgCommandBuffer.length() < 16) {
                // (after v107z) 75 and up go straight to their command character ('\xa3' and on): '0' + 49 would be 'a'
                imgCommandBuffer += (code < 26) ? (char)('a' + code) : (code >= 75) ? (char)(0xa3 + (code - 75)) : (char)('0' + (code - 26));
            }
        }
        request->send(200);
    });

    // (after v111b) numbers typed in the web: /set?hz=<1/10000 Hz> (exact output rate), /set?adc=<PLLAD_MD>, /set?si=<Hz>.
    // Only stored here; the main loop applies them (clockToolsTick), never this callback.
    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->params() > 0) {
            const String k = request->getParam(0)->name();
            const String v = request->getParam(0)->value();
            const uint32_t n = (uint32_t)strtoul(v.c_str(), nullptr, 10);
            if (k == "hz")
                clockToolsPendingHz = n;
            else if (k == "adc")
                clockToolsPendingAdc = (uint16_t)(n > 65535 ? 0 : n);
            else if (k == "si")
                clockToolsPendingSi = n;
        }
        request->send(200);
    });

    server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", "true");
        request->send(response);

        // Temporarily enable persistence to save credentials (ESP8266 only)
        // Temporarily enable persistence to save credentials
#ifdef ESP8266
        WiFi.persistent(true);
#endif
        
        if (request->arg("n").length()) {     // n holds ssid
            String ssid = request->arg("n");
            String pass = request->arg("p");
#ifdef ESP32
            // On ESP32, WiFi.begin(..., false) does not reliably save to NVS.
            // Use direct ESP-IDF calls to force saving to flash.
            wifi_config_t conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
                memset(&conf, 0, sizeof(conf));
            }

            memset(conf.sta.ssid, 0, sizeof(conf.sta.ssid));
            strncpy((char*)conf.sta.ssid, ssid.c_str(), sizeof(conf.sta.ssid) - 1);
            
            if (pass.length()) {
                memset(conf.sta.password, 0, sizeof(conf.sta.password));
                strncpy((char*)conf.sta.password, pass.c_str(), sizeof(conf.sta.password) - 1);
                conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            } else {
                 memset(conf.sta.password, 0, sizeof(conf.sta.password));
                 conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
            }
            
            esp_wifi_set_storage(WIFI_STORAGE_FLASH);
            esp_wifi_set_config(WIFI_IF_STA, &conf);
            
            SerialM.println(F("Saved WiFi creds to NVS (ESP32) - Safe Method"));
#else
            // ESP8266 logic
            if (pass.length()) { // p holds password
                // false = only save credentials, don't connect
                WiFi.begin(ssid.c_str(), pass.c_str(), 0, 0, false);
            } else {
                WiFi.begin(ssid.c_str(), emptyWiFiPassword.c_str(), 0, 0, false);
            }
#endif
        } else {
            WiFi.begin();
        }
        
        // Disable persistence again to avoid Core 3.1.x bug (ESP8266 only)
#ifdef ESP8266
        WiFi.persistent(false);
#endif

        userCommand = 'u'; // next loop, set wifi station mode and restart device
    });

    server.on("/bin/slots.bin", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            SlotMetaArray slotsObject;
            File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");

            if (!slotsBinaryFileRead) {
                File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
                for (int i = 0; i < SLOTS_TOTAL; i++) {
                    slotsObject.slot[i].slot = i;
                    slotsObject.slot[i].presetID = 0;
                    slotsObject.slot[i].scanlines = 0;
                    slotsObject.slot[i].scanlinesStrength = 0;
                    slotsObject.slot[i].wantVdsLineFilter = false;
                    slotsObject.slot[i].wantStepResponse = true;
                    slotsObject.slot[i].wantPeaking = true;
                    char emptySlotName[25] = "Empty                   ";
                    strncpy(slotsObject.slot[i].name, emptySlotName, 25);
                }
                slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileWrite.close();
            } else {
                slotsBinaryFileRead.close();
            }

            request->send(LittleFS, "/slots.bin", "application/octet-stream");
        }
    });

    server.on("/slot/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;

        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();

            if (params > 0) {
                const AsyncWebParameter *slotParam = request->getParam(0);
                String slotParamValue = slotParam->value();
                char slotValue[2];
                slotParamValue.toCharArray(slotValue, sizeof(slotValue));
                uopt->presetSlot = (uint8_t)slotValue[0];
                uopt->presetPreference = OutputCustomized;
                uopt->outputChosen = 1; // (after v111f)
                saveUserPrefs();
                result = true;
            }
        }

        request->send(200, "application/json", result ? "true" : "false");
    });

    server.on("/slot/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;

        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();

            if (params > 0) {
                SlotMetaArray slotsObject;
                File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");

                if (slotsBinaryFileRead) {
                    slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
                    slotsBinaryFileRead.close();
                } else {
                    File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");

                    for (int i = 0; i < SLOTS_TOTAL; i++) {
                        slotsObject.slot[i].slot = i;
                        slotsObject.slot[i].presetID = 0;
                        slotsObject.slot[i].scanlines = 0;
                        slotsObject.slot[i].scanlinesStrength = 0;
                        slotsObject.slot[i].wantVdsLineFilter = false;
                        slotsObject.slot[i].wantStepResponse = true;
                        slotsObject.slot[i].wantPeaking = true;
                        char emptySlotName[25] = "Empty                   ";
                        strncpy(slotsObject.slot[i].name, emptySlotName, 25);
                    }

                    slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                    slotsBinaryFileWrite.close();
                }

                // index param
                const AsyncWebParameter *slotIndexParam = request->getParam(0);
                String slotIndexString = slotIndexParam->value();
                uint8_t slotIndex = lowByte(slotIndexString.toInt());
                if (slotIndex >= SLOTS_TOTAL) {
                    goto fail;
                }

                // name param
                const AsyncWebParameter *slotNameParam = request->getParam(1);
                String slotName = slotNameParam->value();

                char emptySlotName[25] = "                        ";
                strncpy(slotsObject.slot[slotIndex].name, emptySlotName, 25);

                slotsObject.slot[slotIndex].slot = slotIndex;
                slotName.toCharArray(slotsObject.slot[slotIndex].name, sizeof(slotsObject.slot[slotIndex].name));
                slotsObject.slot[slotIndex].presetID = rto->presetID;
                slotsObject.slot[slotIndex].scanlines = uopt->wantScanlines;
                slotsObject.slot[slotIndex].scanlinesStrength = uopt->scanlineStrength;
                slotsObject.slot[slotIndex].wantVdsLineFilter = uopt->wantVdsLineFilter;
                slotsObject.slot[slotIndex].wantStepResponse = uopt->wantStepResponse;
                slotsObject.slot[slotIndex].wantPeaking = uopt->wantPeaking;

                File slotsBinaryOutputFile = LittleFS.open(SLOTS_FILE, "w");
                slotsBinaryOutputFile.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryOutputFile.close();

                result = true;
            }
        }

        fail:
        request->send(200, "application/json", result ? "true" : "false");
    });

    server.on("/slot/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;
        int params = request->params();
        const AsyncWebParameter *p = request->getParam(0);
        char param = p->name().charAt(0);
        if (params > 0)
        {
            if (param == '0')
            {
                SerialM.println(F("Wait..."));
                result = true;
            }
            else
            {
                Ascii8 slot = uopt->presetSlot;
                Ascii8 nextSlot;
                auto currentSlot = slotIndexMap.indexOf(slot);

                SlotMetaArray slotsObject;
                File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
                slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileRead.close();
                String slotName = slotsObject.slot[currentSlot].name;

                // remove preset files
                LittleFS.remove("/preset_ntsc." + String((char)slot));
                LittleFS.remove("/preset_pal." + String((char)slot));
                LittleFS.remove("/preset_ntsc_480p." + String((char)slot));
                LittleFS.remove("/preset_pal_576p." + String((char)slot));
                LittleFS.remove("/preset_ntsc_720p." + String((char)slot));
                LittleFS.remove("/preset_ntsc_1080p." + String((char)slot));
                LittleFS.remove("/preset_medium_res." + String((char)slot));
                LittleFS.remove("/preset_vga_upscale." + String((char)slot));
                LittleFS.remove("/preset_unknown." + String((char)slot));

                uint8_t loopCount = 0;
                uint8_t flag = 1;
                while (flag != 0)
                {
                    slot = slotIndexMap[currentSlot + loopCount];
                    nextSlot = slotIndexMap[currentSlot + loopCount + 1];
                    flag = 0;
                    flag += LittleFS.rename("/preset_ntsc." + String((char)(nextSlot)), "/preset_ntsc." + String((char)slot));
                    flag += LittleFS.rename("/preset_pal." + String((char)(nextSlot)), "/preset_pal." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_480p." + String((char)(nextSlot)), "/preset_ntsc_480p." + String((char)slot));
                    flag += LittleFS.rename("/preset_pal_576p." + String((char)(nextSlot)), "/preset_pal_576p." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_720p." + String((char)(nextSlot)), "/preset_ntsc_720p." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_1080p." + String((char)(nextSlot)), "/preset_ntsc_1080p." + String((char)slot));
                    flag += LittleFS.rename("/preset_medium_res." + String((char)(nextSlot)), "/preset_medium_res." + String((char)slot));
                    flag += LittleFS.rename("/preset_vga_upscale." + String((char)(nextSlot)), "/preset_vga_upscale." + String((char)slot));
                    flag += LittleFS.rename("/preset_unknown." + String((char)(nextSlot)), "/preset_unknown." + String((char)slot));

                    slotsObject.slot[currentSlot + loopCount].slot = slotsObject.slot[currentSlot + loopCount + 1].slot;
                    slotsObject.slot[currentSlot + loopCount].presetID = slotsObject.slot[currentSlot + loopCount + 1].presetID;
                    slotsObject.slot[currentSlot + loopCount].scanlines = slotsObject.slot[currentSlot + loopCount + 1].scanlines;
                    slotsObject.slot[currentSlot + loopCount].scanlinesStrength = slotsObject.slot[currentSlot + loopCount + 1].scanlinesStrength;
                    slotsObject.slot[currentSlot + loopCount].wantVdsLineFilter = slotsObject.slot[currentSlot + loopCount + 1].wantVdsLineFilter;
                    slotsObject.slot[currentSlot + loopCount].wantStepResponse = slotsObject.slot[currentSlot + loopCount + 1].wantStepResponse;
                    slotsObject.slot[currentSlot + loopCount].wantPeaking = slotsObject.slot[currentSlot + loopCount + 1].wantPeaking;
                    // slotsObject.slot[currentSlot + loopCount].name = slotsObject.slot[currentSlot + loopCount + 1].name;
                    strncpy(slotsObject.slot[currentSlot + loopCount].name, slotsObject.slot[currentSlot + loopCount + 1].name, 25);
                    loopCount++;
                }

                File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
                slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileWrite.close();
                SerialM.println("Preset \"" + slotName + "\" removed");
                result = true;
            }
        }

        request->send(200, "application/json", result ? "true" : "false");
    });

    // [patch web-update] Firmware update from any browser: open http://<ip>/update and pick firmware.bin
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html",
                      F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                        "<title>GBS firmware update</title></head><body style='font-family:sans-serif;max-width:30em;margin:2em auto;padding:0 1em'>"
                        "<h2>GBS firmware update</h2>"
                        "<p>Choose the <b>firmware.bin</b> built by PlatformIO. The GBS restarts by itself when the upload finishes. "
                        "Do not switch it off while uploading.</p>"
                        "<form method='POST' action='/update' enctype='multipart/form-data'>"
                        "<input type='file' name='firmware' accept='.bin' required><br><br>"
                        "<input type='submit' value='Upload and update'></form></body></html>"));
    });

    server.on(
        "/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = webUpdateDone && !webUpdateRejected && !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(
                200, "text/html",
                ok ? F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                       "<body style='font-family:sans-serif;margin:2em'><h2>Update OK</h2><p>The GBS is restarting. Reload the page in about 20 seconds.</p></body>")
                   : F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                       "<body style='font-family:sans-serif;margin:2em'><h2>Update FAILED</h2><p>The current firmware was not changed. "
                       "Make sure you picked firmware.bin and try again.</p><p><a href='/update'>Back</a></p></body>"));
            response->addHeader("Connection", "close");
            if (ok) {
                request->onDisconnect([]() {
                    delay(100);
                    prefsFlush(); // (after v123j)
                    edgesFlushNow();
                    ESP.restart();
                });
            }
            request->send(response);
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            webUpdateLastChunk = millis();
            if (!index) {
                webUpdateRejected = false;
                webUpdateDone = false;
                webUpdateActive = true;
#ifdef ESP8266
                WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
                SerialM.println(F("Web update: start"));
                prefsFlush();    // (after v123j) nothing waiting is lost to the restart
                edgesFlushNow();
                // a valid ESP image starts with 0xE9; refuse anything else before touching flash
                if (len == 0 || data[0] != 0xE9) {
                    SerialM.println(F("Web update: not a firmware image, refused"));
                    webUpdateRejected = true; // mark as rejected
                    webUpdateActive = false;
                    return;
                }
#ifdef ESP8266
                Update.runAsync(true);
                uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSketch)) {
#else
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#endif
                    Update.printError(Serial);
                    webUpdateRejected = true;
                    webUpdateActive = false;
                    return;
                }
            }
            if (webUpdateRejected) {
                return; // rejected earlier
            }
            if (len) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                    webUpdateRejected = true;
                    webUpdateActive = false;
                    return;
                }
            }
            if (final) {
                webUpdateActive = false;
                if (Update.end(true)) {
                    webUpdateDone = true;
                    SerialM.println(F("Web update: done, restarting"));
                } else {
                    Update.printError(Serial);
                }
            }
        });

    server.on("/filesystem/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "true");
    });

    server.on(
        "/filesystem/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) { request->send(200, "application/json", "true"); },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                if (filename.startsWith("/")) {
                    request->_tempFile = LittleFS.open(filename, "w");
                } else {
                    request->_tempFile = LittleFS.open("/" + filename, "w");
                }
            }
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
            }
        });

    server.on("/filesystem/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        prefsFlush(); // (after v123j) the backup carries what is waiting too
        edgesFlushNow();
        if (ESP.getFreeHeap() > 10000) {
            // On ESP32 query parameter ordering and
            // unnamed params (e.g. cache-busters like "&176...") can make getParam(0) unreliable.
            // Always resolve the file path by *name*.
            if (request->hasParam("file")) {
                String path = request->getParam("file")->value();
                if (!path.startsWith("/")) {
                    path = "/" + path;
                }
                request->send(LittleFS, path, String(), true);
            } else {
                request->send(200, "application/json", "false");
            }
        } else {
            request->send(200, "application/json", "false");
        }
    });

    server.on("/filesystem/dir", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
#ifdef ESP8266
            Dir dir = LittleFS.openDir("/");
            String output = "[";

            while (dir.next()) {
                String name = dir.fileName();
                if (!name.startsWith("/")) {
                    name = "/" + name;
                }
                output += "\"";
                output += name;
                output += "\",";
                delay(1); // wifi stack
            }
#else
            File root = LittleFS.open("/");
            String output = "[";
            File file = root.openNextFile();
            while(file){
                String name = file.name();
                if (!name.startsWith("/")) {
                    name = "/" + name;
                }
                output += "\"";
                output += name;
                output += "\",";
                file = root.openNextFile();
                delay(1);
            }
#endif

            output += "]";

            output.replace(",]", "]");

            request->send(200, "application/json", output);
            return;
        }
        request->send(200, "application/json", "false");
    });

    server.on("/filesystem/format", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", LittleFS.format() ? "true" : "false");
    });

    server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        WiFiMode_t wifiMode = WiFi.getMode();
        request->send(200, "application/json", wifiMode == WIFI_AP ? "{\"mode\":\"ap\"}" : "{\"mode\":\"sta\",\"ssid\":\"" + WiFi.SSID() + "\"}");
    });

    server.on("/gbs/restore-filters", HTTP_GET, [](AsyncWebServerRequest *request) {
        SlotMetaArray slotsObject;
        File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
        bool result = false;
        if (slotsBinaryFileRead) {
            slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
            slotsBinaryFileRead.close();
            auto currentSlot = slotIndexMap.indexOf(uopt->presetSlot);
            if (currentSlot == -1) {
                goto fail;
            }

            uopt->wantScanlines = slotsObject.slot[currentSlot].scanlines;

            SerialM.print(F("slot: "));
            SerialM.println(uopt->presetSlot);
            SerialM.print(F("scanlines: "));
            if (uopt->wantScanlines) {
                SerialM.println(F("on (Line Filter recommended)"));
            } else {
                disableScanlines();
                SerialM.println(F("off"));
            }
            saveUserPrefs();

            uopt->scanlineStrength = slotsObject.slot[currentSlot].scanlinesStrength;
            uopt->wantVdsLineFilter = slotsObject.slot[currentSlot].wantVdsLineFilter;
            uopt->wantStepResponse = slotsObject.slot[currentSlot].wantStepResponse;
            uopt->wantPeaking = slotsObject.slot[currentSlot].wantPeaking;
            result = true;
        }

        fail:
        request->send(200, "application/json", result ? "true" : "false");
    });

    //webSocket.onEvent(webSocketEvent);

    persWM.setConnectNonBlock(true);
#ifdef ESP32
    // ESP32: WiFi.SSID() is the *currently connected* SSID, not the saved one.
    // Use the stored STA config to decide whether to attempt STA or go AP immediately.
    bool hasStoredStaConfig = false;
    {
        wifi_config_t conf;
        memset(&conf, 0, sizeof(conf));
        // Ensure driver is initialized so esp_wifi_get_config works reliably
        WiFi.mode(WIFI_STA);
        delay(10);
        if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] != 0) {
            hasStoredStaConfig = true;
        }
        // PersWiFiManager will reconfigure modes as needed
        WiFi.mode(WIFI_OFF);
        delay(10);
    }
    if (!hasStoredStaConfig) {
        persWM.setupWiFiHandlers();
        persWM.startApMode();
    } else {
        persWM.begin(); // first try connecting to stored network, go AP mode after timeout
    }
#else
    if (WiFi.SSID().length() == 0) {
        // no stored network to connect to > start AP mode right away
        persWM.setupWiFiHandlers();
        persWM.startApMode();
    } else {
        persWM.begin(); // first try connecting to stored network, go AP mode after timeout
    }
#endif

    server.begin();    // Webserver for the site
    webSocket.begin(); // Websocket for interaction
    // Enable heartbeat: ping every 2000ms, pong timeout 3000ms, disconnect after 2 missed pongs
    // This keeps the connection alive during idle periods (client has 2.7s timeout)
    webSocket.enableHeartbeat(2000, 3000, 2);
    yield();

#ifdef HAVE_PINGER_LIBRARY
    // pinger library
    pinger.OnReceive([](const PingerResponse &response) {
        if (response.ReceivedResponse) {
            Serial.printf(
                "Reply from %s: time=%lums\n",
                response.DestIPAddress.toString().c_str(),
                response.ResponseTime);

            pingLastTime = millis() - 900; // produce a fast stream of pings if connection is good
        } else {
            Serial.printf("Request timed out.\n");
        }

        // Return true to continue the ping sequence.
        // If current event returns false, the ping sequence is interrupted.
        return true;
    });

    pinger.OnEnd([](const PingerResponse &response) {
        // detailed info not necessary
        return true;
    });
#endif
}

void initUpdateOTA()
{
    ArduinoOTA.setHostname("GBS OTA");

    // ArduinoOTA.setPassword("admin");
    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    //ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    // update: no password is as (in)secure as this publicly stated hash..
    // rely on the user having to enable the OTA feature on the web ui

    ArduinoOTA.onStart([]() {
        if (ArduinoOTA.getCommand() == U_FLASH) { // (after v123j) not for a filesystem upload, which replaces it
            prefsFlush();
            edgesFlushNow();
        }
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_LittleFS
            type = "filesystem";

        // NOTE: if updating LittleFS this would be the place to unmount LittleFS using LittleFS.end()
        LittleFS.end();
        SerialM.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        SerialM.println(F("\nEnd"));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        SM_PRINTF("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        SM_PRINTF("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            SerialM.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            SerialM.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            SerialM.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            SerialM.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR)
            SerialM.println(F("End Failed"));
    });
    ArduinoOTA.begin();
    yield();
}

