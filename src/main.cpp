#include <Arduino.h>
#include "M5Cardputer.h"
#include "audio_config.h"
#include "file_manager.h"
#include "ui_manager.h"
#include "battery.h"
#include <ESP32Time.h>
#include <utility/Keyboard/KeyboardReader/TCA8418.h>

TaskHandle_t handleAudioTask = NULL;
TaskHandle_t handleUITask = NULL;

static ESP32Time rtc(0);

void Task_TFT(void *pvParameters);
void Task_Audio(void *pvParameters);
void audio_eof_mp3(const char *info);

void setup() {
    Serial.begin(115200);
    Serial.println("=== Mini MP3 Player for Cardputer Adv ===");

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    M5Cardputer.begin(cfg, true);

    Serial.println("Configuring TCA8418 keyboard driver");
    std::unique_ptr<KeyboardReader> reader(new TCA8418KeyboardReader());
    M5Cardputer.Keyboard.begin(std::move(reader));

    if (!initSDCard()) {
        Serial.println("CRITICAL: SD Card initialization failed!");
        while(1) {
            M5Cardputer.update();
            delay(100);
        }
    }

    initUI();

    if (!initES8311Codec()) {
        Serial.println("ERROR: Audio codec initialization failed!");
    }

    scanAvailableFolders("/");
    
    Serial.printf("Found %d folders\n", folderCount);
    Serial.printf("Memory info: Free heap=%d, Min free=%d\n", 
                  ESP.getFreeHeap(), ESP.getMinFreeHeap());

    xTaskCreatePinnedToCore(Task_TFT, "Task_TFT", 20480, NULL, 2, &handleUITask, 0);
    xTaskCreatePinnedToCore(Task_Audio, "Task_Audio", 10240, NULL, 3, &handleAudioTask, 1);
    
    Serial.println("Setup complete. Ready to play!");
}

void loop() {
    updateHeadphoneDetection();
    delay(100);
}

void Task_TFT(void *pvParameters) {
  while (true) {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

      for (auto ch : ks.word) {
        handleKeyPress(ch);
      }

      if (ks.enter) {
        handleKeyPress('\n');
      }

      if (ks.del) {
        handleKeyPress('\b');
      }
    }

    draw();
    vTaskDelay(40 / portTICK_PERIOD_MS);
  }
}

void Task_Audio(void *pvParameters) {
    static unsigned long lastLog = 0;
    const TickType_t playDelay = pdMS_TO_TICKS(1);
    const TickType_t idleDelay = pdMS_TO_TICKS(20);
    
    while (1) {
        if (volumeUpdateRequest) {
            audio.setVolume(volume);
            volumeUpdateRequest = false;
        }

        if (nextTrackRequest) {
            audio.stopSong();
            Serial.printf("Task_Audio: Loading track %d: %s\n", 
                         currentFileIndex, audioFiles[currentFileIndex].c_str());
            
            if (fileCount > 0 && SD.exists(audioFiles[currentFileIndex])) {
                File f = SD.open(audioFiles[currentFileIndex]);
                if (f) {
                    uint32_t sz = f.size();
                    Serial.printf("File size: %u bytes\n", sz);
                    f.close();
                    
                    if (codec_initialized) {
                        audio.connecttoFS(SD, audioFiles[currentFileIndex].c_str());
                    } else {
                        Serial.println("WARNING: Codec not initialized");
                    }
                } else {
                    Serial.println("ERROR: Could not open file");
                }
            } else {
                Serial.println("ERROR: File not found on SD");
            }
            
            nextTrackRequest = 0;
        }
        
        if (currentUIState == UI_PLAYER && isPlaying && codec_initialized && !stoped && fileCount > 0) {
            audio.loop();
            
            if (millis() - lastLog >= 5000) {
                Serial.printf("Task_Audio: Playing track %d/%d, volume=%d\n", 
                             currentFileIndex + 1, fileCount, volume);
                lastLog = millis();
            }
            
            vTaskDelay(playDelay);
        } else {
            vTaskDelay(idleDelay);
        }
    }
}

void audio_eof_mp3(const char *info) {
    rtc.setTime(0, 0, 0, 17, 1, 2021);
    Serial.print("eof_mp3: ");
    Serial.println(info);
    
    if (currentUIState == UI_PLAYER && fileCount > 0) {
        currentFileIndex++;
        if (currentFileIndex >= fileCount) {
            currentFileIndex = 0;
        }
        
        Serial.printf("Auto-advancing to next track: %s\n", audioFiles[currentFileIndex].c_str());
        nextTrackRequest = true;
    }
}