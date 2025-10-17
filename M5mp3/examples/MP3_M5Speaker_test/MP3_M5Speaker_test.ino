#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include <AudioOutputM5Speaker.h>

// Adjust these pins to match your board if needed
#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12

AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputM5Speaker *out = nullptr;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.println("MP3 -> M5Speaker test starting");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD.begin failed");
    return;
  }

  const char *path = "/mp3s/q7.mp3"; // change to a file on your SD
  if (!SD.exists(path)) {
    Serial.printf("File not found: %s\n", path);
    return;
  }

  // Print size and header bytes for debugging
  File ftest = SD.open(path);
  if (ftest) {
    uint32_t sz = ftest.size();
    Serial.printf("SD open OK, size=%u bytes\n", (unsigned)sz);
    uint8_t hdr[16];
    int r = ftest.read(hdr, sizeof(hdr));
    Serial.printf("First %d bytes: ", r);
    for (int i = 0; i < r; ++i) Serial.printf("%02X ", hdr[i]);
    Serial.println();
    ftest.close();
  } else {
    Serial.println("Failed to open file for header read");
  }

  file = new AudioFileSourceSD(path);
  out = new AudioOutputM5Speaker(&M5.Speaker, 0);
  mp3 = new AudioGeneratorMP3();
  mp3->begin(file, out);
}

void loop() {
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("MP3 finished or error");
    }
  }
  delay(10);
}
