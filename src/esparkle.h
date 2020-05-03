#ifndef ESPARKLE_H
#define ESPARKLE_H

void ICACHE_RAM_ATTR ISRoutine();

bool wifiConnect();

void playAudio();
bool stopPlaying();
void tts(String text, String voice);
void beep(uint8_t repeat = 1);

bool mqttConnect(bool about = false);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttCmdAbout();
void mqttCmdList();

void ledDefault(uint32_t delay = 500);
void ledRainbow(uint32_t delay);
void ledBlink(uint32_t delay, int color);
void ledSine(uint32_t delay, int color);
void ledPulse(uint32_t delay, int color);
void ledDisco(uint32_t delay);
void ledSolid(int color);
void ledOff();

void prettyBytes(uint32_t bytes, String &output);
uint32_t getUptimeSecs();
void getUptimeDhms(char *output, size_t max_len);
#endif //ESPARKLE_H
