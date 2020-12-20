#include <Arduino.h>
#include <LITTLEFS.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <MPU6050.h>
#include <FastLED.h>
#include <Ticker.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esparkle.h"
#include "config.h"

// Misc global variables
bool otaInProgress = false;
bool ledActionInProgress = false;
bool newAudioSource = false;
volatile bool mpuInterrupt = false;

CRGB leds[NUM_LEDS];
int curColor;
uint32_t curDelay;

char audioSource[256] = "";
uint8_t msgPriority = 0;
float onceGain = 0;

ESP8266WiFiMulti wifiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Ticker ledActionTimer;
MPU6050 mpu;

AudioFileSourceHTTPStream *stream = nullptr;
AudioFileSourceLittleFS *file = nullptr;
AudioFileSourceBuffer *buff = nullptr;
AudioFileSourcePROGMEM *string = nullptr;
AudioGeneratorMP3 *mp3 = nullptr;
AudioGeneratorRTTTL *rtttl = nullptr;
AudioOutputI2S *out = nullptr;

//############################################################################
// SETUP
//############################################################################

void setup() {
    Serial.begin(115200);
    Serial.println();

    // INIT LittleFS
    LittleFS.begin();

    // INIT WIFI
    WiFi.hostname(ESP_NAME);
    WiFi.mode(WIFI_STA);
    for (auto i : AP_LIST) {
        wifiMulti.addAP(i.ssid, i.passphrase);
    }
    wifiConnect();

    // INIT MQTT
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(MQTT_BUFF_SIZE);
    mqttConnect(true);

    // INIT OTA
    ArduinoOTA.setHostname(ESP_NAME);
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        Serial.println(F("Start updating..."));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.println(progress / (total / 100));
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\nend"));
        otaInProgress = false;
    });
    ArduinoOTA.onError([](ota_error_t error) {
        String msg;
        if (error == OTA_AUTH_ERROR)
            msg = F("auth failed");
        else if (error == OTA_BEGIN_ERROR)
            msg = F("begin failed");
        else if (error == OTA_CONNECT_ERROR)
            msg = F("connect failed");
        else if (error == OTA_RECEIVE_ERROR)
            msg = F("receive failed");
        else if (error == OTA_END_ERROR)
            msg = F("end failed");
        Serial.printf_P(PSTR("Error: %s"), msg.c_str());
    });
    ArduinoOTA.begin();

    // INIT MPU
    Wire.begin();
    Serial.println(F("Initializing MPU6050..."));
    mpu.initialize();
    if (mpu.testConnection()) {
        Serial.println(F("MPU6050 connection successful"));
        mpu.setIntMotionEnabled(true);
        mpu.setMotionDetectionThreshold(MOTION_DETECTION_THRESHOLD);
        mpu.setMotionDetectionDuration(MOTION_DETECTION_DURATION);
        attachInterrupt(digitalPinToInterrupt(MPU_INTERRUPT_PIN), ISRoutine, RISING);
    } else {
        Serial.println(F("MPU6050 connection failed"));
    }

    // INIT LED
    LEDS.addLeds<LED_TYPE, LED_DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(max_bright);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
    ledDefault();

    // READY SOUND
    strlcpy(audioSource, "/mp3/bullfrog.mp3", sizeof(audioSource));
    newAudioSource = true;
}

//############################################################################
// LOOP
//############################################################################

void loop() {

    unsigned long curMillis = millis();

    // HANDLE Wifi

    static unsigned long lastWifiMillis = 0;
    bool wifiIsConnected = WiFi.isConnected();
    if (!wifiIsConnected) {
        stopPlaying();
        /*
        if (mp3 && mp3->isRunning()) {
            mp3->stop();
        } else if (rtttl && rtttl->isRunning()) {
            rtttl->stop();
        }
        */
        ledBlink(50, 0xFF0000);
        if (curMillis - lastWifiMillis > 60000) {
            if (wifiConnect()) {
                ledDefault();
            }
            lastWifiMillis = curMillis;
        }
    }

    // HANDLE OTA
    if (wifiIsConnected) {
        ArduinoOTA.handle();
        if (otaInProgress) {
            return;
        }
    }

    // HANDLE MQTT
    static unsigned long lastMqttConnMillis = 0;
    if (wifiIsConnected) {
        if (!mqttClient.connected()) {
            if (curMillis - lastMqttConnMillis > 5000) {
                Serial.println(F("Disconnected from MQTT"));
                mqttConnect();
                lastMqttConnMillis = curMillis;
            }
        } else {
            mqttClient.loop();
        }
    }

    // HANDLE MPU
    static unsigned long lastMpuTapMillis = 0;
    static uint8_t mpuTapCount = 0;
    if (mpuInterrupt) {
        mpuInterrupt = false;
        if (curMillis - lastMpuTapMillis > MPU_INTERRUPT_INTERVAL_MS) {

            // Handle multi-tap
            if (curMillis - lastMpuTapMillis > MPU_MULTITAP_INTERVAL_MS) {
                mpuTapCount = 0;
            }
            Serial.printf_P(PSTR("MPU interrupt %d\n"), ++mpuTapCount);

            if (mpuTapCount == MPU_MULTITAP_RESTART) {
                beep();
                Serial.println(F("Restarting ESP..."));
                ESP.restart();
                delay(500);
            }

            if (mpuTapCount < 3) {
                // If something is running, stop it...
                bool stopped = stopPlaying();
                /*
                if (mp3 && mp3->isRunning()) {
                    mp3->stop();
                    stopped = true;
                } else if (rtttl && rtttl->isRunning()) {
                    rtttl->stop();
                    stopped = true;
                }
                */

                if (ledActionInProgress) {
                    msgPriority = 0;
                    ledDefault();
                    stopped = true;
                }

                // ...otherwise, play random MP3 from stream
                if (!stopped) {
                    strlcpy(audioSource, RANDOM_STREAM_URL, sizeof(audioSource));
                    newAudioSource = true;
                }
            }
            lastMpuTapMillis = curMillis;
        }
    }

    // HANDLE MP3
    if (newAudioSource) {
        playAudio();
    } else if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            //mp3->stop();
            stopPlaying();
            //Serial.println(F("MP3 done"));
        }
    } else if (rtttl && rtttl->isRunning()) {
        if (!rtttl->loop()) {
            //rtttl->stop();
            stopPlaying();
            //Serial.println(F("RTTTL done"));
        }
    }

    // HANDLE LED
    FastLED.show();
}

//############################################################################
// ISRoutine
//############################################################################

void ISRoutine() {
    mpuInterrupt = true;
}

//############################################################################
// WIFI
//############################################################################

bool wifiConnect() {
    Serial.print(F("Connecting to WiFi"));
    uint8_t count = 10;
    while (count-- && (wifiMulti.run() != WL_CONNECTED)) {
        Serial.print(".");
        delay(1000);
    }

    if (WiFi.isConnected()) {
        Serial.println(F("\nConnected to WiFi"));
        return true;
    } else {
        Serial.println(F("\nUnable to connect to WiFi"));
        return false;
    }
}

//############################################################################
// MQTT
//############################################################################

bool mqttConnect(bool about) {

    String cltName = String(ESP_NAME) + '_' + String(ESP.getChipId(), HEX);
    if (mqttClient.connect(cltName.c_str(), MQTT_USER, MQTT_PASSWORD)) {
        Serial.println(F("Connected to MQTT"));
        if (about) {
            mqttCmdAbout();
        } else {
            mqttClient.publish(MQTT_OUT_TOPIC, PSTR("Reconnected to MQTT"));
        }
        mqttClient.subscribe(MQTT_IN_TOPIC);
    } else {
        Serial.println(F("Unable to connect to MQTT"));
    }
    return mqttClient.connected();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {

    StaticJsonDocument<512> jsonInDoc;
    DeserializationError err = deserializeJson(jsonInDoc, payload);

    if (err) {
        mqttClient.publish(MQTT_OUT_TOPIC, PSTR("{event:\"deserializeJson(jsonInDoc, payload) failed\"}"));
        mqttClient.publish(MQTT_OUT_TOPIC, err.c_str());
        return;
    }

    serializeJsonPretty(jsonInDoc, Serial);
    Serial.println();

    // Simple commands
    if (jsonInDoc.containsKey("cmd")) {
        if (strcmp("break", jsonInDoc["cmd"]) == 0) { // Break current action: {cmd:"break"}
            stopPlaying();
            /*
            if (mp3 && mp3->isRunning()) {
                mp3->stop();
            } else if (rtttl && rtttl->isRunning()) {
                rtttl->stop();
            }
             */
            if (ledActionInProgress) {
                msgPriority = 0;
                ledDefault();
            }
        } else if (strcmp("restart", jsonInDoc["cmd"]) == 0) { // Restart ESP: {cmd:"restart"}
            ESP.restart();
            delay(500);
        } else if (strcmp("about", jsonInDoc["cmd"]) == 0) { // About: {cmd:"about"}
            mqttCmdAbout();
        } else if (strcmp("list", jsonInDoc["cmd"]) == 0) { // List LittleFS files: {cmd:"list"}
            mqttCmdList();
        }
        return;
    }

    // Set max brightness: {bright:255}
    if (jsonInDoc.containsKey("bright")) {
        uint8_t b = jsonInDoc["bright"].as<uint8_t>();
        if (b >= 0 && b <= 255) {
            FastLED.setBrightness(b);
        }
    }

    // Set default gain: {"gain":1.2}
    if (jsonInDoc.containsKey("gain")) {
        float g = jsonInDoc["gain"].as<float>();
        if (g > 0.01 && g < 3.0) {
            defaultGain = g;
        }
    }

    // Set once gain: {oncegain:1.2}
    if (jsonInDoc.containsKey("oncegain")) {
        float g = jsonInDoc["oncegain"].as<float>();
        if (g > 0.01 && g < 3.0) {
            onceGain = g;
        }
    }

    // Set new MP3 source
    // - from stream: {"mp3":"http://www.universal-soundbank.com/sounds/7340.mp3"}
    // - from LittleFS: {"mp3":"/mp3/song.mp3"}
    if (jsonInDoc.containsKey("mp3")) {
        strlcpy(audioSource, jsonInDoc["mp3"], sizeof(audioSource));
        newAudioSource = true;
    }

    // Set new MP3 source from TTS proxy {"tts":"May the force be with you"}
    if (jsonInDoc.containsKey("tts")) {
        tts(jsonInDoc["tts"], jsonInDoc.containsKey("voice") ? jsonInDoc["voice"] : String());
    }

    // Set new Rtttl source {"rtttl":"Xfiles:d=4,o=5,b=160:e,b,a,b,d6,2b."}
    if (jsonInDoc.containsKey("rtttl")) {
        strlcpy(audioSource, jsonInDoc["rtttl"], sizeof(audioSource));
        newAudioSource = true;
    }

    // Set new message priority : {"led":"Blink",color:"0xff0000",delay:50,priority:9}
    // LED pattern is considered only if msg priority is >= to previous msg priority
    // This is to avoid masking an important LED alert with a minor one
    if (jsonInDoc.containsKey("priority")) {
        uint8_t thisPriority = jsonInDoc["priority"].as<uint8_t>();
        if (thisPriority < msgPriority) {
            return;
        }
        msgPriority = thisPriority;
    }

    // Set led pattern: {"led":"Blink",color:"0xff0000",delay:50}
    if (jsonInDoc.containsKey("led")) {
        uint32_t d = 100;
        if (jsonInDoc.containsKey("delay")) {
            d = jsonInDoc["delay"].as<uint32_t>();
        }

        int c = 0xFFFFFF;
        if (jsonInDoc.containsKey("color")) {
            c = (int)strtol(jsonInDoc["color"], nullptr, 0);
        }

        if (strcmp("Rainbow", jsonInDoc["led"]) == 0) {
            ledRainbow(d);
        } else if (strcmp("Blink", jsonInDoc["led"]) == 0) {
            ledBlink(d, c);
        } else if (strcmp("Sine", jsonInDoc["led"]) == 0) {
            ledSine(d, c);
        } else if (strcmp("Pulse", jsonInDoc["led"]) == 0) {
            ledPulse(d, c);
        } else if (strcmp("Disco", jsonInDoc["led"]) == 0) {
            ledDisco(d);
        } else if (strcmp("Solid", jsonInDoc["led"]) == 0) {
            ledSolid(c);
        } else if (strcmp("Off", jsonInDoc["led"]) == 0) {
            ledOff();
        } else {
            ledDefault();
        }
    }
}

/**
 * Publish useful ESP information to MQTT out topic
 */
void mqttCmdAbout() {

    char uptimeBuffer[15];
    getUptimeDhms(uptimeBuffer, sizeof(uptimeBuffer));

    String freeSpace;
    prettyBytes(ESP.getFreeSketchSpace(), freeSpace);

    String sketchSize;
    prettyBytes(ESP.getSketchSize(), sketchSize);

    String chipSize;
    prettyBytes(ESP.getFlashChipRealSize(), chipSize);

    String freeHeap;
    prettyBytes(ESP.getFreeHeap(), freeHeap);

    Serial.println(F("Preparing about..."));

    DynamicJsonDocument jsonDoc(1024);

    jsonDoc[F("version")] = ESPARKLE_VERSION;
    jsonDoc[F("sdkVersion")] = ESP.getSdkVersion();
    jsonDoc[F("coreVersion")] = ESP.getCoreVersion();
    jsonDoc[F("resetReason")] = ESP.getResetReason();
    jsonDoc[F("ssid")] = WiFi.SSID();
    jsonDoc[F("ip")] = WiFi.localIP().toString();
    jsonDoc[F("staMac")] = WiFi.macAddress();
    jsonDoc[F("apMac")] = WiFi.softAPmacAddress();
    jsonDoc[F("chipId")] = String(ESP.getChipId(), HEX);
    jsonDoc[F("chipSize")] = chipSize;
    jsonDoc[F("sketchSize")] = sketchSize;
    jsonDoc[F("freeSpace")] = freeSpace;
    jsonDoc[F("freeHeap")] = freeHeap;
    jsonDoc[F("uptime")] = uptimeBuffer;
    jsonDoc[F("defaultGain")] = defaultGain;

    String mqttMsg;
    serializeJsonPretty(jsonDoc, mqttMsg);
    Serial.println(mqttMsg.c_str());

    mqttClient.publish(MQTT_OUT_TOPIC, mqttMsg.c_str());
}

void mqttCmdList() {

    DynamicJsonDocument jsonDoc(1024);

    JsonArray array = jsonDoc.to<JsonArray>();

    Dir dir = LittleFS.openDir("/mp3");
    while (dir.next()) {
        array.add(dir.fileName());
    }

    String mqttMsg;
    serializeJsonPretty(jsonDoc, mqttMsg);

    mqttClient.publish(MQTT_OUT_TOPIC, mqttMsg.c_str());
}

//############################################################################
// AUDIO
//############################################################################

void playAudio() {
    newAudioSource = false;

    if (audioSource[0] == 0) {
        return;
    }

    stopPlaying();

    Serial.printf_P(PSTR("\nFree heap: %d\n"), ESP.getFreeHeap());

    if (!out) {
        out = new AudioOutputI2S();
        out->SetOutputModeMono(true);
    }
    out->SetGain(onceGain ?: defaultGain);
    onceGain = 0;

    if (strncmp("http", audioSource, 4) == 0) {
        // Get MP3 from stream
        Serial.printf_P(PSTR("**MP3 stream: %s\n"), audioSource);
        stream = new AudioFileSourceHTTPStream(audioSource);
        buff = new AudioFileSourceBuffer(stream, 1024 * 2);
        //buff = new AudioFileSourceBuffer(stream, preallocateBuffer, preallocateBufferSize);
        mp3 = new AudioGeneratorMP3();
        mp3->begin(buff, out);
        if (!mp3->isRunning()) {
            //Serial.println(F("Unable to play MP3"));
            stopPlaying();
        }
    } else if (strncmp("/mp3/", audioSource, 5) == 0) {
        // Get MP3 from LittleFS
        Serial.printf_P(PSTR("**MP3 file: %s\n"), audioSource);
        file = new AudioFileSourceLittleFS(audioSource);
        mp3 = new AudioGeneratorMP3();
        mp3->begin(file, out);
        if (!mp3->isRunning()) {
            //Serial.println(F("Unable to play MP3"));
            stopPlaying();
        }
    } else {
        // Get RTTTL
        Serial.printf_P(PSTR("**RTTL file: %s\n"), audioSource);
        string = new AudioFileSourcePROGMEM(audioSource, strlen(audioSource));
        rtttl = new AudioGeneratorRTTTL();
        rtttl->begin(string, out);
        if (!rtttl->isRunning()) {
            //Serial.println(F("Unable to play RTTTL"));
            stopPlaying();
        }
    }
}

bool stopPlaying() {
    bool stopped = false;
    if (rtttl) {
        rtttl->stop();
        delete rtttl;
        rtttl = nullptr;
        stopped = true;
    }
    if (mp3) {
        mp3->stop();
        delete mp3;
        mp3 = nullptr;
        stopped = true;
    }
    if (buff) {
        buff->close();
        delete buff;
        buff = nullptr;
    }
    if (file) {
        file->close();
        delete file;
        file = nullptr;
    }
    if (stream) {
        stream->close();
        delete stream;
        stream = nullptr;
    }

    return stopped;
}

void tts(String text, String voice) {
    if (text.length()) {
        String query = String("text=") + text;
        if (voice.length()) {
            query += "&voice=" + voice;
        }
        Serial.println(query.c_str());
        HTTPClient http;
        http.begin(espClient, TTS_PROXY_URL);
        http.setAuthorization(TTS_PROXY_USER, TTS_PROXY_PASSWORD);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpCode = http.POST(query);
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println(payload.c_str());
            if (payload.endsWith(".mp3")) {
                strlcpy(audioSource, payload.c_str(), sizeof(audioSource));
                newAudioSource = true;
            }
        } else {
            String payload = http.getString();
            Serial.println(payload.c_str());
        }
    }
}

void beep(uint8_t repeat) {
    strlcpy(audioSource, "/mp3/nasty-error-long.mp3", sizeof(audioSource));

    for (uint8_t i = 0; i < repeat; i++) {
        playAudio();
        while (mp3->isRunning()) {
            if (!mp3->loop()) {
                //mp3->stop();
                stopPlaying();
                //Serial.println(F("MP3 done"));
            }
            yield();
        }
    }
}

//############################################################################
// LED
//############################################################################
void ledDefault(uint32_t delay) {

    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = false;
        static uint8_t hue = 0;
        fill_solid(leds, NUM_LEDS, CHSV(hue++, 255, 255));
    });
}

void ledRainbow(uint32_t delay) {

    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = true;
        static uint8_t hue = 0;
        fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
        hue = (hue + 1) % 255;
    });
}

void ledBlink(uint32_t delay, int color) {

    curColor = color;

    Serial.println(curColor, HEX);

    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = true;
        static bool t = true;
        fill_solid(leds, NUM_LEDS, t ? curColor : CRGB::Black);
        t = !t;
    });
}

void ledSine(uint32_t delay, int color) {

    curColor = color;

    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = true;

        static uint8_t i = 0;
        static CHSV hsvColor;
        if (curColor) {
            hsvColor = rgb2hsv_approximate(curColor);
            curColor = 0;
        }
        hsvColor.value = sin8(i);
        fill_solid(leds, NUM_LEDS, hsvColor);
        i = (i + 1) % 255;
    });
}

void ledPulse(uint32_t delay, int color) {

    curColor = color;
    curDelay = delay;

    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = true;

        static uint16_t blackCountdown = 0;

        if (blackCountdown) {
            blackCountdown--;
            return;
        }

        static uint8_t i = 0;
        CRGB rgbColor = curColor;

        rgbColor.fadeToBlackBy(i);
        fill_solid(leds, NUM_LEDS, rgbColor);
        i = (i + 1) % 255;

        if (i == 0) {
            blackCountdown = 750 / curDelay; // Stay black during 750ms
        }
    });
}

void ledDisco(uint32_t delay) {
    ledActionTimer.detach();
    ledActionTimer.attach_ms(delay, []() {
        ledActionInProgress = true;
        fill_solid(leds, NUM_LEDS, CHSV(random8(), 255, 255));
    });
}

void ledSolid(int color) {
    ledActionTimer.detach();
    fill_solid(leds, NUM_LEDS, color);
}

void ledOff() {
    ledSolid(0x000000);
}

//############################################################################
// HELPERS
//############################################################################

void prettyBytes(uint32_t bytes, String &output) {

    const char *suffixes[7] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    uint8_t s = 0;
    double count = bytes;

    while (count >= 1024 && s < 7) {
        s++;
        count /= 1024;
    }
    if (count - floor(count) == 0.0) {
        output = String((int)count) + suffixes[s];
    } else {
        output = String(round(count * 10.0) / 10.0, 1) + suffixes[s];
    };
}

uint32_t getUptimeSecs() {
    static uint32_t uptime = 0;
    static uint32_t previousMillis = 0;
    uint32_t now = millis();

    uptime += (now - previousMillis) / 1000UL;
    previousMillis = now;
    return uptime;
}

void getUptimeDhms(char *output, size_t max_len) {
    uint32 d, h, m, s;
    uint32_t sec = getUptimeSecs();

    d = sec / 86400;
    sec = sec % 86400;
    h = sec / 3600;
    sec = sec % 3600;
    m = sec / 60;
    s = sec % 60;

    snprintf(output, max_len, "%dd %02d:%02d:%02d", d, h, m, s);
}
