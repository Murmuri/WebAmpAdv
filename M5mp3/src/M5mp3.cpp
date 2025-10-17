#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <Wire.h>
#include <SD.h>
#include "M5Cardputer.h"
#include "Audio.h"  // https://github.com/schreibfaul1/ESP32-audioI2S   version 2.0.0
#include "font.h"
#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time  verison 2.0.6
#include "driver/i2s.h"
#include <math.h>
M5Canvas sprite(&M5Cardputer.Display);
M5Canvas spr(&M5Cardputer.Display);
// microSD card
#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12
// Cardputer-Adv audio pin mappings (Stamp-S3A):
// G8  = SDA (I2C)
// G9  = SCL (I2C)
// G41 = SCLK / BCLK
// G42 = AMP_EN
// G43 = LRCK
// G46 = ASDOUT (DOUT)
// Use these pins when running on Cardputer-Adv
#define I2C_SDA   8   // G8
#define I2C_SCL   9   // G9
#define I2S_BCLK  41  // G41
// Simple I2C scan utility (implemented below)
static void i2c_scan();

#define I2S_LRCK  43  // G43
#define I2S_DOUT  42  // Cardputer ADV I2S data out
#define HP_DET_PIN 17   // Headphone detect pin (LOW when inserted)
#define AMP_EN_PIN 46   // Amplifier enable (HIGH = enabled)
#define BAT_ADC_PIN 10  // ADC pin used for battery reading - verify against schematic!
#define ES8311_I2C_FREQ 400000UL
// ES8311 I2C address on Cardputer-Adv
constexpr uint8_t ES8311_ADDR = 0x18;
#define AUDIO_FILENAME_01 "/song.mp3"

// Forward declarations (helpers implemented below)
static bool es8311_write(uint8_t reg, uint8_t val);
static bool initCardputerAdvCodec();
Audio audio;
unsigned short grays[18];
unsigned short gray;
int sliderPos = 0;
// UI and playback state
int volume = 10;          // 0..21 for ESP32-audioI2S
int bri = 2;              // brightness index 0..4
int brightness[5] = {60, 120, 180, 220, 255};
bool stoped = false;      // spelling kept from original code
bool isPlaying = true;
bool volUp = false;
int n = 0;                // current file index
int nextS = 0;            // request to switch tracks
int graphSpeed = 0;
int g[14] = {0};
unsigned short light;
int textPos = 90;
static bool lastHPState = false;
// Task handle for audio task
TaskHandle_t handleAudioTask = NULL;
ESP32Time rtc(0);
//
// Global flag to indicate codec init state so tasks can check it
bool codec_initialized = false;

#define MAX_FILES 100
// Array to store file names
String audioFiles[MAX_FILES];
int fileCount = 0;
// Forward declarations
void Task_TFT(void *pvParameters);
void Task_Audio(void *pvParameters);
void listFiles(fs::FS &fs, const char *dirname, uint8_t levels);
void resetClock() {
  rtc.setTime(0, 0, 0, 17, 1, 2021);
}

// Battery helper (assumes 12-bit ADC and ~2:1 divider to 3.3V ADC ref)
static int getBatteryPercent() {
  int raw = analogRead(BAT_ADC_PIN);
  float voltage = (raw / 4095.0f) * 3.3f * 2.0f; // approximate divider x2
  int mv = (int)(voltage * 1000.0f);
  int percent = constrain(map(mv, 3300, 4200, 0, 100), 0, 100);
  return percent;
}

// Simple I2C scan for debugging
static void i2c_scan() {
  Serial.println("I2C scan start");
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" I2C device found at 0x%02X\n", addr);
    }
  }
  Serial.println("I2C scan done");
}

// Minimal ES8311 blind write helper
static bool es8311_write(uint8_t reg, uint8_t val) {
  uint8_t data = val;
  if (!M5.In_I2C.writeRegister(ES8311_ADDR, reg, &data, 1, ES8311_I2C_FREQ)) {
    Serial.printf("ES8311 I2C write failed reg 0x%02X\n", reg);
    return false;
  }
  delay(2);
  return true;
}




// Simple boot-time sine test: ESP32 drives I2S clocks (master), ES8311 is slave.
static void playTestTone(uint32_t freq_hz, uint32_t duration_ms, uint32_t sample_rate = 44100, uint16_t amplitude = 12000) {
  Serial.printf("Playing test tone: %lu Hz for %lu ms\n", (unsigned long)freq_hz, (unsigned long)duration_ms);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = (int)sample_rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  // Use non-deprecated standard I2S format
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) {
    Serial.println("i2s_driver_install failed (I2S0) - skipping test tone");
    return;
  }
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    Serial.println("i2s_set_pin failed - skipping test tone");
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  i2s_set_clk(I2S_NUM_0, sample_rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  const size_t frames_per_buf = 256; // stereo frames
  int16_t buf[frames_per_buf * 2];   // 2 channels
  double phase = 0.0;
  double phase_inc = 2.0 * M_PI * (double)freq_hz / (double)sample_rate;
  uint32_t elapsed = 0;
  uint32_t chunk_ms = (uint32_t)((1000.0 * frames_per_buf) / (double)sample_rate);
  if (chunk_ms == 0) chunk_ms = 6; // ~5.8ms at 44.1kHz
  while (elapsed < duration_ms) {
    for (size_t i = 0; i < frames_per_buf; ++i) {
      int16_t s = (int16_t)(sin(phase) * amplitude);
      phase += phase_inc;
      if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
      buf[2 * i + 0] = s; // Left
      buf[2 * i + 1] = s; // Right
    }
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, (const char *)buf, sizeof(buf), &bytes_written, portMAX_DELAY);
    elapsed += chunk_ms;
  }
  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("Test tone done");
}

static bool initCardputerAdvCodec() {
  Serial.println("initCardputerAdvCodec(): preparing ES8311 on Cardputer-Adv");

  Wire.end();
  delay(50);
  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  Wire.setTimeOut(50);
  delay(10);
  i2c_scan();

  pinMode(HP_DET_PIN, INPUT_PULLUP);
  pinMode(AMP_EN_PIN, OUTPUT);
  digitalWrite(AMP_EN_PIN, LOW);
  Serial.printf("AMP_EN: held LOW on pin %d until codec init\n", AMP_EN_PIN);

  struct RegisterValue {
    uint8_t reg;
    uint8_t value;
  };

  static constexpr RegisterValue kInitSequence[] = {
      {0x00, 0x80},
      {0x01, 0xB5},
      {0x02, 0x18},
      {0x0D, 0x01},
      {0x12, 0x00},
      {0x13, 0x10},
      {0x32, 0xBF},
      {0x37, 0x08},
  };

  bool ok = true;
  for (const auto &entry : kInitSequence) {
    if (!es8311_write(entry.reg, entry.value)) {
      ok = false;
    }
  }

  codec_initialized = ok;
  if (!codec_initialized) {
    Serial.println("ES8311 init sequence failed");
    return false;
  }

  Serial.println("ES8311 init sequence done");
  Serial.printf("Using Cardputer-Adv audio pins: I2C SDA=%d SCL=%d, BCLK=%d LRCK=%d DOUT=%d\n", I2C_SDA, I2C_SCL, I2S_BCLK, I2S_LRCK, I2S_DOUT);

  bool hpInsertedBoot = (digitalRead(HP_DET_PIN) == LOW);
  lastHPState = hpInsertedBoot;
  if (hpInsertedBoot) {
    Serial.println("Headphones detected at boot -> keeping speaker AMP OFF");
    digitalWrite(AMP_EN_PIN, LOW);
  } else {
    Serial.println("No headphones -> speaker AMP enabled");
    digitalWrite(AMP_EN_PIN, HIGH);
  }

  playTestTone(440, 1500, 44100, 12000);

  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(volume);
  audio.setBalance(0);

  return true;
}
void setup() {
  Serial.begin(115200);
  Serial.println("hi");
  resetClock();
  // Initialize M5Cardputer and SD card
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_mic = false;
  cfg.internal_spk = false; // leave external I2S free for ES8311 codec
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5Cardputer.begin(cfg, true);
  auto spk_cfg = M5Cardputer.Speaker.config();
  spk_cfg.sample_rate = 128000;
  spk_cfg.task_pinned_core = APP_CPU_NUM;
  M5Cardputer.Speaker.config(spk_cfg);
  // Do NOT initialize M5Cardputer.Speaker when using external ES8311 via I2S.
  // It can take over the I2S peripheral and conflict with ESP32-audioI2S.
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(brightness[bri]);
  sprite.createSprite(240, 135);
  spr.createSprite(86, 16);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println(F("ERROR: SD Mount Failed!"));
  }
  // Prefer MP3s in /mp3s folder. If none found, fall back to root.
  listFiles(SD, "/mp3s", MAX_FILES);
  if (fileCount == 0) {
    Serial.println("No files found in /mp3s, scanning root");
    listFiles(SD, "/", MAX_FILES);
  }
  if (!initCardputerAdvCodec()) {
    Serial.println("initCardputerAdvCodec() failed - leaving amplifier disabled");
  }
  if (fileCount > 0) {
    Serial.printf("Trying to play: %s\n", audioFiles[n].c_str());
    // Double-check the file exists
    if (SD.exists(audioFiles[n])) {
      // Open the file directly and print size + header bytes for verification
      File f = SD.open(audioFiles[n]);
      if (f) {
        uint32_t sz = f.size();
        Serial.printf("SD open OK, size=%u bytes\n", (unsigned)sz);
        // read first 12 bytes
        uint8_t hdr[12];
        int r = f.read(hdr, sizeof(hdr));
        Serial.printf("First %d bytes: ", r);
        for (int i = 0; i < r; i++) Serial.printf("%02X ", hdr[i]);
        Serial.println();
        // Optional: detect ID3 and log a warning if a large tag may delay playback
        if (r >= 3 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
          Serial.println("Note: ID3 tag detected; initial parsing may delay audio start");
        }
        f.close();
      } else {
        Serial.println("SD.open failed (could not read file)");
      }
      if (codec_initialized) {
        audio.connecttoFS(SD, audioFiles[n].c_str());
      } else {
        Serial.println("Skipping audio.connecttoFS(): codec not initialized");
      }
    } else {
      Serial.printf("File not found on SD: %s\n", audioFiles[n].c_str());
    }
  } else {
    Serial.println("No audio files found on SD - skipping connect");
  }
  int co = 214;
  for (int i = 0; i < 18; i++) {
    grays[i] = M5Cardputer.Display.color565(co, co, co + 40);
    co = co - 13;
  }
  // Create tasks and pin them to different cores
  xTaskCreatePinnedToCore(Task_TFT, "Task_TFT", 20480, NULL, 2, NULL, 0);                  // Core 0
  xTaskCreatePinnedToCore(Task_Audio, "Task_Audio", 10240, NULL, 3, &handleAudioTask, 1);  // Core 1
}
void loop() {
  // Poll headphone detect and gate AMP_EN accordingly
  bool hpInserted = (digitalRead(HP_DET_PIN) == LOW);
  if (hpInserted != lastHPState) {
    lastHPState = hpInserted;
    if (hpInserted) {
      Serial.println("HP inserted -> speaker AMP OFF");
      digitalWrite(AMP_EN_PIN, LOW);
    } else {
      Serial.println("HP removed -> speaker AMP ON");
      digitalWrite(AMP_EN_PIN, HIGH);
    }
  }
  delay(100);
}
void draw() {
  if (graphSpeed == 0) {
    gray = grays[15];
    light = grays[11];
    sprite.fillRect(0, 0, 240, 135, gray);
    sprite.fillRect(4, 8, 130, 122, BLACK);
    sprite.fillRect(129, 8, 5, 122, 0x0841);
    sliderPos = map(n, 0, fileCount, 8, 110);
    sprite.fillRect(129, sliderPos, 5, 20, grays[2]);
    sprite.fillRect(131, sliderPos + 4, 1, 12, grays[16]);
    sprite.fillRect(4, 2, 50, 2, ORANGE);
    sprite.fillRect(84, 2, 50, 2, ORANGE);
    sprite.fillRect(190, 2, 45, 2, ORANGE);
    sprite.fillRect(190, 6, 45, 3, grays[4]);
    sprite.drawFastVLine(3, 9, 120, light);
    sprite.drawFastVLine(134, 9, 120, light);
    sprite.drawFastHLine(3, 129, 130, light);
    sprite.drawFastHLine(0, 0, 240, light);
    sprite.drawFastHLine(0, 134, 240, light);
    sprite.fillRect(139, 0, 3, 135, BLACK);
    sprite.fillRect(148, 14, 86, 42, BLACK);
    sprite.fillRect(148, 59, 86, 16, BLACK);
    sprite.fillTriangle(162, 18, 162, 26, 168, 22, GREEN);
    sprite.fillRect(162, 30, 6, 6, RED);
    sprite.drawFastVLine(143, 0, 135, light);
    sprite.drawFastVLine(238, 0, 135, light);
    sprite.drawFastVLine(138, 0, 135, light);
    sprite.drawFastVLine(148, 14, 42, light);
    sprite.drawFastHLine(148, 14, 86, light);
    //buttons
    for (int i = 0; i < 4; i++)
      sprite.fillRoundRect(148 + (i * 22), 94, 18, 18, 3, grays[4]);
    //button icons
    sprite.fillRect(220, 104, 8, 2, grays[13]);
    sprite.fillRect(220, 108, 8, 2, grays[13]);
    sprite.fillTriangle(228, 102, 228, 106, 231, 105, grays[13]);
    sprite.fillTriangle(220, 106, 220, 110, 217, 109, grays[13]);
    if (!stoped) {
      sprite.fillRect(152, 104, 3, 6, grays[13]);
      sprite.fillRect(157, 104, 3, 6, grays[13]);
    } else {
      sprite.fillTriangle(156, 102, 156, 110, 160, 106, grays[13]);
    }
    //volume bar
    sprite.fillRoundRect(172, 82, 60, 3, 2, YELLOW);
    sprite.fillRoundRect(155 + ((volume / 5) * 17), 80, 10, 8, 2, grays[2]);
    sprite.fillRoundRect(157 + ((volume / 5) * 17), 82, 6, 4, 2, grays[10]);
    // brightness
    sprite.fillRoundRect(172, 124, 30, 3, 2, MAGENTA);
    sprite.fillRoundRect(172 + (bri * 5), 122, 10, 8, 2, grays[2]);
    sprite.fillRoundRect(174 + (bri * 5), 124, 6, 4, 2, grays[10]);
    //BATTERY
    sprite.drawRect(206, 119, 28, 12, GREEN);
    sprite.fillRect(234, 122, 3, 6, GREEN);
    //graph
    for (int i = 0; i < 14; i++) {
      if (!stoped)
        g[i] = random(1, 5);
      for (int j = 0; j < g[i]; j++)
        sprite.fillRect(172 + (i * 4), 50 - j * 3, 3, 2, grays[4]);
    }
    sprite.setTextFont(0);
    sprite.setTextDatum(0);
    if (n < 5)
      for (int i = 0; i < 10; i++) {
        if (i == n) sprite.setTextColor(WHITE, BLACK);
        else sprite.setTextColor(GREEN, BLACK);
        if (i < fileCount)
          sprite.drawString(audioFiles[i].substring(1, 20), 8, 10 + (i * 12));
      }
    int yos = 0;
    if (n >= 5)
      for (int i = n - 5; i < n - 5 + 10; i++) {
        if (i == n) sprite.setTextColor(WHITE, BLACK);
        else sprite.setTextColor(GREEN, BLACK);
        if (i < fileCount)
          sprite.drawString(audioFiles[i].substring(1, 20), 8, 10 + (yos * 12));
        yos++;
      }
    sprite.setTextColor(grays[1], gray);
    sprite.drawString("WINAMP", 150, 4);
    sprite.setTextColor(grays[2], gray);
    sprite.drawString("LIST", 58, 0);
    sprite.setTextColor(grays[4], gray);
    sprite.drawString("VOL", 150, 80);
    sprite.drawString("LIG", 150, 122);
    if (isPlaying) {
      sprite.setTextColor(grays[8], BLACK);
      sprite.drawString("P", 152, 18);
      sprite.drawString("L", 152, 27);
      sprite.drawString("A", 152, 36);
      sprite.drawString("Y", 152, 45);
    } else {
      sprite.setTextColor(grays[8], BLACK);
      sprite.drawString("S", 152, 18);
      sprite.drawString("T", 152, 27);
      sprite.drawString("O", 152, 36);
      sprite.drawString("P", 152, 45);
    }
    sprite.setTextColor(GREEN, BLACK);
  sprite.setFont(&DSEG7_Classic_Mini_Regular_16);
    if (!stoped)
      sprite.drawString(rtc.getTime().substring(3, 8), 172, 18);
    sprite.setTextFont(0);
    int percent = getBatteryPercent();
    sprite.setTextDatum(3);
    sprite.drawString(String(percent) + "%", 220, 121);
    sprite.setTextColor(BLACK, grays[4]);
    sprite.drawString("B", 220, 96);
    sprite.drawString("N", 198, 96);
    sprite.drawString("P", 176, 96);
    sprite.drawString("A", 154, 96);
    sprite.setTextColor(BLACK, grays[5]);
    sprite.drawString(">>", 202, 103);
    sprite.drawString("<<", 180, 103);
    spr.fillSprite(BLACK);
    spr.setTextColor(GREEN, BLACK);
    if (!stoped)
      spr.drawString(audioFiles[n].substring(1, audioFiles[n].length()), textPos, 4);
    textPos = textPos - 2;
    if (textPos < -300) textPos = 90;
    spr.pushSprite(&sprite, 148, 59);
    sprite.pushSprite(0, 0);
  }
  graphSpeed++;
  if (graphSpeed == 4) graphSpeed = 0;
}
void Task_TFT(void *pvParameters) {
  while (1) {
    M5Cardputer.update();
    // Check for key press events
    if (M5Cardputer.Keyboard.isChange()) {
      if (M5Cardputer.Keyboard.isKeyPressed('a')) {
        isPlaying = !isPlaying;
        stoped = !stoped;
      }  // Toggle the playback state
      if (M5Cardputer.Keyboard.isKeyPressed('v')) {
        isPlaying = false;
        volUp = true;
        volume = volume + 5;
        if (volume > 20) volume = 5;
      }
      if (M5Cardputer.Keyboard.isKeyPressed('l')) {
        bri++;
        if (bri == 5) bri = 0;
        M5Cardputer.Display.setBrightness(brightness[bri]);
      }
      if (M5Cardputer.Keyboard.isKeyPressed('n')) {
        resetClock();
        isPlaying = false;
        textPos = 90;
        n++;
        if (n >= fileCount) n = 0;
        nextS = 1;
      }
      if (M5Cardputer.Keyboard.isKeyPressed('p')) {
        resetClock();
        isPlaying = false;
        textPos = 90;
        n--;
        if (n < 0) n = fileCount - 1;
        nextS = 1;
      }
      if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        n--;
        if (n >= fileCount)
          n = 0;
      }
      if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        n++;
        if (n < 0)
          n = fileCount - 1;
      }
      if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        resetClock();
        stoped = false;
        isPlaying = false;
        textPos = 90;
        nextS = 1;
      }
      if (M5Cardputer.Keyboard.isKeyPressed('b')) {
        resetClock();
        isPlaying = false;
        textPos = 90;
        n = random(0, fileCount);
        nextS = 1;
      }
    }
    draw();
    vTaskDelay(40 / portTICK_PERIOD_MS);  // Adjust the delay for responsiveness
  }
}
void Task_Audio(void *pvParameters) {
  static unsigned long lastLog = 0;
  const TickType_t playDelay = pdMS_TO_TICKS(1);
  const TickType_t idleDelay = pdMS_TO_TICKS(20);

  while (1) {
    if (volUp) {
      audio.setVolume(volume);
      isPlaying = true;
      volUp = false;
    }

    if (nextS) {
      audio.stopSong();
      Serial.printf("Task_Audio: next track requested: %s\n", audioFiles[n].c_str());
      if (SD.exists(audioFiles[n])) {
        audio.connecttoFS(SD, audioFiles[n].c_str());
      } else {
        Serial.printf("Task_Audio: file not found: %s\n", audioFiles[n].c_str());
      }
      isPlaying = true;
      nextS = 0;
    }

    if (isPlaying && codec_initialized && !stoped) {
      audio.loop();

      if (millis() - lastLog >= 500) {
        int ampState = digitalRead(AMP_EN_PIN);
        Serial.printf("Task_Audio: audio.loop() heartbeat  codec_initialized=%d AMP_EN=%d ES_ADDR=0x%02X\n", codec_initialized ? 1 : 0, ampState, ES8311_ADDR);
        lastLog = millis();
      }

      vTaskDelay(playDelay);
    } else {
      vTaskDelay(idleDelay);
    }
  }
}
// Function to play a song from a given URL or file path
void playSong(const char *source) {
  //audio.stopSong(); // Stop any current playback
  audio.connecttohost(source);  // Open and play the new song from a URL
}
// Function to stop playback
void stopSong() {
  audio.stopSong();
}
// Function to open and prepare a song (without autoplay)
void openSong(const char *source) {
  audio.connecttohost(source);  // Opens the source, similar to play
}
void listFiles(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);
  // Ensure dirname starts with '/'
  String dir = String(dirname);
  if (!dir.startsWith("/")) dir = String("/") + dir;
  File root = fs.open(dir.c_str());
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file && fileCount < MAX_FILES) {
    if (file.isDirectory()) {
      // Build subdirectory path ensuring single leading '/'
      String sub = String(file.name());
      if (!sub.startsWith("/")) sub = dir + String("/") + sub;
      Serial.print("DIR : ");
      Serial.println(sub.c_str());
      if (levels) {
        listFiles(fs, sub.c_str(), levels - 1);
      }
    } else {
      // Build full file path
      String fname = String(file.name());
      if (!fname.startsWith("/")) fname = dir + String("/") + fname;
      // Filter by supported extensions: .mp3 and .wav
      String lower = fname; lower.toLowerCase();
      int dot = lower.lastIndexOf('.') ;
      bool supported = false;
      if (dot >= 0) {
        String ext = lower.substring(dot + 1);
        if (ext == "mp3" || ext == "wav") supported = true;
      }
      if (supported) {
        Serial.print("FILE: ");
        Serial.println(fname.c_str());
        audioFiles[fileCount] = fname;
        Serial.printf("Stored audioFiles[%d] = %s\n", fileCount, audioFiles[fileCount].c_str());
        fileCount++;
      } else {
        Serial.printf("SKIP (unsupported): %s\n", fname.c_str());
      }
    }
    file = root.openNextFile();
  }
}
void audio_eof_mp3(const char *info) {
  resetClock();
  Serial.print("eof_mp3     ");
  Serial.println(info);
  n++;
  if (n == fileCount) n = 0;
  Serial.printf("eof: opening next file: %s\n", audioFiles[n].c_str());
  if (SD.exists(audioFiles[n])) {
    audio.connecttoFS(SD, audioFiles[n].c_str());
  } else {
    Serial.printf("eof: next file not found: %s\n", audioFiles[n].c_str());
  }
}

