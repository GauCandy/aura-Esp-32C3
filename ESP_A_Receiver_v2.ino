/*
 * ESP32-C3 A - RECEIVER (Command-Based)
 * Nhận LỆNH từ ESP B và tự xử lý logic
 * Chân 2: Chuỗi LED WS2812B (số lượng tùy chỉnh)
 * Chân 8: LED báo tín hiệu (LED thường)
 */

#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>

// Cấu hình LED chính
#define LED_PIN 2
#define MAX_LEDS 300  // Số LED tối đa
#define STATUS_LED_PIN 8  // LED thường (không phải WS2812B)

CRGB leds[MAX_LEDS];

// ===== CÁC MÃ LỆNH =====
#define CMD_MODE_NEXT 0
#define CMD_BRIGHTNESS_UP 1
#define CMD_BRIGHTNESS_DOWN 2
#define CMD_SPEED_UP 3
#define CMD_SPEED_DOWN 4
#define CMD_LEDS_INCREASE 5
#define CMD_LEDS_DECREASE 6
#define CMD_LEDS_TOGGLE 7

// Cấu trúc dữ liệu nhận từ ESP B (chỉ chứa lệnh)
typedef struct command_message {
  uint8_t command; // Mã lệnh
} command_message;

command_message receivedCmd;

// Biến điều khiển (ESP A tự quản lý)
uint8_t currentBrightness = 128;
uint8_t currentMode = 1;
uint8_t currentSpeed = 128;
uint16_t numLedsStart = 10;
uint16_t numLedsEnd = 10;
bool editingStart = true; // true = đang sửa LED đầu, false = đang sửa LED đuôi

unsigned long lastUpdate = 0;
uint8_t effectStep = 0;

// Callback khi nhận dữ liệu (ESP32 Arduino Core v3.x)
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receivedCmd, incomingData, sizeof(receivedCmd));

  // Xử lý lệnh
  processCommand(receivedCmd.command);

  // Báo hiệu nhận được tín hiệu - Nháy LED status
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(50);
  digitalWrite(STATUS_LED_PIN, LOW);
}

// Hàm xử lý lệnh
void processCommand(uint8_t cmd) {
  switch (cmd) {
    case CMD_MODE_NEXT:
      currentMode++;
      if (currentMode > 9) currentMode = 0;
      Serial.print("Mode: ");
      Serial.println(currentMode);
      break;

    case CMD_BRIGHTNESS_UP:
      if (currentBrightness < 255) {
        currentBrightness += 15;
        if (currentBrightness > 255) currentBrightness = 255;
      }
      FastLED.setBrightness(currentBrightness);
      Serial.print("Brightness: ");
      Serial.println(currentBrightness);
      break;

    case CMD_BRIGHTNESS_DOWN:
      if (currentBrightness > 0) {
        currentBrightness -= 15;
        if (currentBrightness < 15) currentBrightness = 15;
      }
      FastLED.setBrightness(currentBrightness);
      Serial.print("Brightness: ");
      Serial.println(currentBrightness);
      break;

    case CMD_SPEED_UP:
      if (currentSpeed < 255) {
        currentSpeed += 15;
        if (currentSpeed > 255) currentSpeed = 255;
      }
      Serial.print("Speed: ");
      Serial.println(currentSpeed);
      break;

    case CMD_SPEED_DOWN:
      if (currentSpeed > 0) {
        currentSpeed -= 15;
        if (currentSpeed < 15) currentSpeed = 15;
      }
      Serial.print("Speed: ");
      Serial.println(currentSpeed);
      break;

    case CMD_LEDS_INCREASE:
      if (editingStart) {
        if (numLedsStart < MAX_LEDS) numLedsStart += 5;
        Serial.print("LEDs Start: ");
        Serial.println(numLedsStart);
      } else {
        if (numLedsEnd < MAX_LEDS) numLedsEnd += 5;
        Serial.print("LEDs End: ");
        Serial.println(numLedsEnd);
      }
      break;

    case CMD_LEDS_DECREASE:
      if (editingStart) {
        if (numLedsStart > 0) numLedsStart -= 5;
        Serial.print("LEDs Start: ");
        Serial.println(numLedsStart);
      } else {
        if (numLedsEnd > 0) numLedsEnd -= 5;
        Serial.print("LEDs End: ");
        Serial.println(numLedsEnd);
      }
      break;

    case CMD_LEDS_TOGGLE:
      editingStart = !editingStart;
      Serial.print("Editing: ");
      Serial.println(editingStart ? "START" : "END");
      break;

    default:
      Serial.println("Unknown command");
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // Cấu hình LED status (LED thường)
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Sáng khi khởi động

  // Khởi tạo WiFi ở chế độ Station
  WiFi.mode(WIFI_STA);

  // In ra địa chỉ MAC
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Đăng ký callback nhận dữ liệu
  esp_now_register_recv_cb(OnDataRecv);

  // Khởi tạo FastLED - CHỈ cho dải LED chính
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);
  FastLED.setBrightness(currentBrightness);
  FastLED.show();

  // LED status sẵn sàng (sáng liên tục)
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(200);
  digitalWrite(STATUS_LED_PIN, HIGH);

  Serial.println("ESP-A Receiver ready! (Command-Based)");
}

void loop() {
  unsigned long currentMillis = millis();
  uint16_t delayTime = map(currentSpeed, 0, 255, 100, 10); // Tốc độ càng cao, delay càng nhỏ

  if (currentMillis - lastUpdate > delayTime) {
    lastUpdate = currentMillis;

    // Chạy hiệu ứng theo chế độ
    switch (currentMode) {
      case 0: // Tắt
        effectOff();
        break;
      case 1: // Đơn sắc
        effectSolidColor();
        break;
      case 2: // Rainbow
        effectRainbow();
        break;
      case 3: // Chạy đuổi
        effectChase();
        break;
      case 4: // Nhấp nháy
        effectBlink();
        break;
      case 5: // Fade
        effectFade();
        break;
      case 6: // Sparkle
        effectSparkle();
        break;
      case 7: // Fire
        effectFire();
        break;
      case 8: // Breathing
        effectBreathing();
        break;
      case 9: // Theater Chase
        effectTheaterChase();
        break;
      default:
        effectSolidColor();
        break;
    }

    FastLED.show();
    effectStep++;
  }
}

// ===== CÁC HIỆU ỨNG LED =====

void effectOff() {
  fill_solid(leds, MAX_LEDS, CRGB::Black);
}

void effectSolidColor() {
  // LED đầu: Đỏ
  for (int i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Red;
  }
  // Khoảng giữa: Tắt
  for (int i = numLedsStart; i < MAX_LEDS - numLedsEnd; i++) {
    leds[i] = CRGB::Black;
  }
  // LED đuôi: Xanh dương
  for (int i = MAX_LEDS - numLedsEnd; i < MAX_LEDS; i++) {
    if (i >= 0) {
      leds[i] = CRGB::Blue;
    }
  }
}

void effectRainbow() {
  uint16_t totalActiveLeds = numLedsStart + numLedsEnd;
  fill_rainbow(leds, totalActiveLeds, effectStep * 2, 7);
  // Tắt phần giữa
  for (int i = numLedsStart; i < MAX_LEDS - numLedsEnd; i++) {
    leds[i] = CRGB::Black;
  }
}

void effectChase() {
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  // Chạy từ đầu
  if (numLedsStart > 0) {
    int pos = effectStep % numLedsStart;
    leds[pos] = CRGB::White;
  }

  // Chạy từ đuôi
  if (numLedsEnd > 0) {
    int posEnd = (MAX_LEDS - numLedsEnd) + (effectStep % numLedsEnd);
    if (posEnd < MAX_LEDS) {
      leds[posEnd] = CRGB::White;
    }
  }
}

void effectBlink() {
  if (effectStep % 2 == 0) {
    effectSolidColor();
  } else {
    effectOff();
  }
}

void effectFade() {
  uint8_t brightness = beatsin8(currentSpeed / 10, 0, 255);
  for (int i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CHSV(0, 255, brightness); // Đỏ
  }
  for (int i = MAX_LEDS - numLedsEnd; i < MAX_LEDS; i++) {
    if (i >= 0) {
      leds[i] = CHSV(160, 255, brightness); // Xanh dương
    }
  }
  for (int i = numLedsStart; i < MAX_LEDS - numLedsEnd; i++) {
    leds[i] = CRGB::Black;
  }
}

void effectSparkle() {
  fadeToBlackBy(leds, MAX_LEDS, 50);

  // Sparkle ngẫu nhiên ở đầu
  if (random8() < 50 && numLedsStart > 0) {
    int pos = random16(numLedsStart);
    leds[pos] = CRGB::White;
  }

  // Sparkle ngẫu nhiên ở đuôi
  if (random8() < 50 && numLedsEnd > 0) {
    int pos = random16(MAX_LEDS - numLedsEnd, MAX_LEDS);
    leds[pos] = CRGB::White;
  }
}

void effectFire() {
  // Hiệu ứng lửa đơn giản
  for (int i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CHSV(random8(0, 20), 255, random8(100, 255));
  }
  for (int i = MAX_LEDS - numLedsEnd; i < MAX_LEDS; i++) {
    if (i >= 0) {
      leds[i] = CHSV(random8(0, 20), 255, random8(100, 255));
    }
  }
  for (int i = numLedsStart; i < MAX_LEDS - numLedsEnd; i++) {
    leds[i] = CRGB::Black;
  }
}

void effectBreathing() {
  uint8_t breath = beatsin8(currentSpeed / 20, 50, 255);
  for (int i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CHSV(0, 255, breath);
  }
  for (int i = MAX_LEDS - numLedsEnd; i < MAX_LEDS; i++) {
    if (i >= 0) {
      leds[i] = CHSV(160, 255, breath);
    }
  }
  for (int i = numLedsStart; i < MAX_LEDS - numLedsEnd; i++) {
    leds[i] = CRGB::Black;
  }
}

void effectTheaterChase() {
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  for (int i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    if ((i + effectStep) % 3 == 0) {
      leds[i] = CRGB::Red;
    }
  }

  for (int i = MAX_LEDS - numLedsEnd; i < MAX_LEDS; i++) {
    if (i >= 0 && (i + effectStep) % 3 == 0) {
      leds[i] = CRGB::Blue;
    }
  }
}
