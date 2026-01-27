#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12

#define MAX_LINKS 100

#define MAX_NAME_LENGTH 30
#define MAX_URL_LENGTH 100

class AudioLinks {
public:
    String name;
    String url;
};

extern AudioLinks audioLinks[MAX_LINKS];

extern uint8_t linksCount;
extern uint8_t currentLinkIndex;

extern SemaphoreHandle_t sdMutex;

bool initSDCard();
void getLinksList();
String getFileName(uint8_t index);

#endif