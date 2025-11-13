/*
 * ESP32-C3 A - RECEIVER (Simplified with Head/Tail Edit Mode)
 * Nhận tín hiệu từ ESP B để tăng/giảm số LED
 *
 * CHẾ ĐỘ EDIT ĐUÔI (mặc định):
 * - BTN_0: Giảm tổng số LED
 * - BTN_1: Tăng tổng số LED (tối đa 300)
 *
 * CHẾ ĐỘ EDIT ĐẦU:
 * - BTN_0: Tăng số LED tắt từ đầu (dịch điểm bắt đầu sang phải)
 * - BTN_1: Giảm số LED tắt từ đầu (dịch điểm bắt đầu sang trái)
 *
 * - BTN_2: Chuyển đổi giữa 2 chế độ edit đầu/đuôi
 * - BTN_3: Giảm độ sáng LED (ấn thường: -5, giữ 2s: -10)
 * - BTN_4: Tăng độ sáng LED (ấn thường: +5, giữ 2s: +10)
 * - BTN_5: Chế độ LED tiếp theo (vòng tròn)
 * - BTN_6: Chế độ LED trước đó (vòng tròn)
 * - BTN_7: Giảm tốc độ hiệu ứng (0-100, mặc định 50)
 * - BTN_8: Tăng tốc độ hiệu ứng (0-100, mặc định 50)
 * - BTN_9: Giảm FPS (10-60, mặc định 30) - Giảm FPS = Mượt hơn nhưng chậm
 * - BTN_10: Tăng FPS (10-60, mặc định 30) - Tăng FPS = Nhanh hơn
 * - Lưu tất cả vào NVS (giữ khi mất điện)
 * - LED GPIO 8: Sáng 100ms mỗi khi nhận tín hiệu
 */

#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

// ===== CẤU HÌNH LED =====
#define LED_PIN 2              // Chân LED WS2812B
#define MAX_LEDS 300           // Số LED tối đa
#define STATUS_LED_PIN 8       // LED báo hiệu (LED thường)

CRGB leds[MAX_LEDS];
Preferences preferences;       // Lưu trữ vào NVS

// ===== ĐỊNH NGHĨA CÁC CHẾ ĐỘ LED =====
enum LedMode {
  MODE_SOLID = 0,      // Đơn sắc Blue
  MODE_RAINBOW,        // Cầu vồng
  MODE_BREATHING,      // Breathing (hít thở)
  MODE_CHASE,          // Chạy đuổi
  MODE_SPARKLE,        // Lấp lánh
  MODE_FIRE,           // Lửa
  MODE_THEATER_CHASE,  // Theater chase
  MODE_FADE,           // Fade in/out
  MODE_COUNT           // Tổng số mode (LUÔN ĐỂ CUỐI!)
};

// Tên các chế độ (để hiển thị Serial)
const char* modeNames[] = {
  "Solid Blue",
  "Rainbow",
  "Breathing",
  "Chase",
  "Sparkle",
  "Fire",
  "Theater Chase",
  "Fade"
};

// ===== BIẾN ĐIỀU KHIỂN =====
uint16_t numLedsTotal = 50;    // Tổng số LED được điều khiển (0-300)
uint16_t numLedsStart = 0;     // Số LED tắt từ đầu (vị trí bắt đầu)
bool editingMode = false;      // false = edit đuôi, true = edit đầu
uint8_t brightness = 50;       // Độ sáng LED (0-255), mặc định 50
uint8_t currentMode = MODE_SOLID;  // Chế độ LED hiện tại

// Biến cho hiệu ứng
unsigned long lastEffectUpdate = 0;
uint16_t effectStep = 0;
uint8_t effectSpeed = 50;      // Tốc độ hiệu ứng (0-100), mặc định 50
uint8_t fps = 30;              // FPS (10-60), mặc định 30

// Biến theo dõi ấn giữ nút (cho BTN_3 và BTN_4)
unsigned long lastBrightnessButtonTime = 0;  // Thời gian nhận nút cuối
uint8_t brightnessButtonCount = 0;           // Số lần nhấn liên tục
uint8_t lastBrightnessButton = 255;          // Nút cuối (3 hoặc 4)

// Biến debounce cho nút chuyển mode (BTN_5 và BTN_6)
unsigned long lastModeChangeTime = 0;        // Thời gian chuyển mode cuối
const unsigned long modeChangeDebounce = 500; // Debounce 500ms

// ===== CẤU TRÚC DỮ LIỆU NHẬN =====
// Nhận từ ESP B (button number: 0-12)
typedef struct button_message {
  uint8_t button;              // Số nút được nhấn
} button_message;

button_message receivedBtn;

// ===== HÀM LƯU/ĐỌC DỮ LIỆU TỪ NVS =====
void saveSettings() {
  preferences.begin("led-store", false);
  preferences.putUShort("numTotal", numLedsTotal);
  preferences.putUShort("numStart", numLedsStart);
  preferences.putBool("editMode", editingMode);
  preferences.putUChar("brightness", brightness);
  preferences.putUChar("ledMode", currentMode);
  preferences.putUChar("speed", effectSpeed);
  preferences.putUChar("fps", fps);
  preferences.end();

  Serial.println("✓ Đã lưu cài đặt vào NVS:");
  Serial.print("  - Tổng LED: ");
  Serial.println(numLedsTotal);
  Serial.print("  - LED tắt từ đầu: ");
  Serial.println(numLedsStart);
  Serial.print("  - Chế độ edit: ");
  Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
  Serial.print("  - Độ sáng: ");
  Serial.println(brightness);
  Serial.print("  - Chế độ LED: ");
  Serial.println(modeNames[currentMode]);
  Serial.print("  - Tốc độ: ");
  Serial.println(effectSpeed);
  Serial.print("  - FPS: ");
  Serial.println(fps);
}

void loadSettings() {
  preferences.begin("led-store", true);
  numLedsTotal = preferences.getUShort("numTotal", 50);  // Mặc định 50
  numLedsStart = preferences.getUShort("numStart", 0);   // Mặc định 0
  editingMode = preferences.getBool("editMode", false);  // Mặc định edit đuôi
  brightness = preferences.getUChar("brightness", 50);   // Mặc định 50
  currentMode = preferences.getUChar("ledMode", MODE_SOLID);  // Mặc định Solid Blue
  effectSpeed = preferences.getUChar("speed", 50);       // Mặc định 50
  fps = preferences.getUChar("fps", 30);                 // Mặc định 30
  preferences.end();

  Serial.println("✓ Đã đọc cài đặt từ NVS:");
  Serial.print("  - Tổng LED: ");
  Serial.println(numLedsTotal);
  Serial.print("  - LED tắt từ đầu: ");
  Serial.println(numLedsStart);
  Serial.print("  - Chế độ edit: ");
  Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
  Serial.print("  - Độ sáng: ");
  Serial.println(brightness);
  Serial.print("  - Chế độ LED: ");
  Serial.println(modeNames[currentMode]);
  Serial.print("  - Tốc độ: ");
  Serial.println(effectSpeed);
  Serial.print("  - FPS: ");
  Serial.println(fps);
}

// ===== CÁC HÀM RENDER CHO TỪNG CHẾ ĐỘ =====

void renderModeSolid() {
  // Đơn sắc Blue
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Blue;
  }
}

void renderModeRainbow() {
  // Rainbow effect
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CHSV((effectStep + (i - numLedsStart) * 5) % 256, 255, 255);
  }
}

void renderModeBreathing() {
  // Breathing effect - Tốc độ điều chỉnh bởi effectSpeed
  uint8_t speed = map(effectSpeed, 0, 100, 5, 50);
  uint8_t breath = beatsin8(speed, 50, 255);
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB(0, 0, breath);  // Blue breathing
  }
}

void renderModeChase() {
  // Chạy đuổi
  uint16_t activeLeds = numLedsTotal - numLedsStart;
  if (activeLeds > 0) {
    uint16_t pos = numLedsStart + (effectStep % activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Blue;
      // Fade các LED khác
      fadeToBlackBy(leds, MAX_LEDS, 20);
      leds[pos] = CRGB::Blue;
    }
  }
}

void renderModeSparkle() {
  // Lấp lánh
  fadeToBlackBy(leds, MAX_LEDS, 50);
  if (random8() < 80 && numLedsTotal > numLedsStart) {
    uint16_t activeLeds = numLedsTotal - numLedsStart;
    uint16_t pos = numLedsStart + random16(activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Blue;
    }
  }
}

void renderModeFire() {
  // Hiệu ứng lửa (màu đỏ-vàng)
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CHSV(random8(0, 30), 255, random8(100, 255));
  }
}

void renderModeTheaterChase() {
  // Theater chase
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    if ((i + effectStep) % 3 == 0) {
      leds[i] = CRGB::Blue;
    } else {
      leds[i] = CRGB::Black;
    }
  }
}

void renderModeFade() {
  // Fade in/out - Tốc độ điều chỉnh bởi effectSpeed
  uint8_t speed = map(effectSpeed, 0, 100, 5, 40);
  uint8_t fadeBrightness = beatsin8(speed, 0, 255);
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB(0, 0, fadeBrightness);
  }
}

// ===== HÀM CẬP NHẬT HIỂN THỊ LED THEO CHẾ ĐỘ HIỆN TẠI =====
void updateLEDs() {
  // Chỉ tắt LED ngoài vùng active
  // Tắt LED từ 0 đến numLedsStart-1
  for (uint16_t i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  // Tắt LED từ numLedsTotal đến MAX_LEDS-1
  for (uint16_t i = numLedsTotal; i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }

  // Render theo chế độ hiện tại
  switch (currentMode) {
    case MODE_SOLID:
      renderModeSolid();
      break;
    case MODE_RAINBOW:
      renderModeRainbow();
      break;
    case MODE_BREATHING:
      renderModeBreathing();
      break;
    case MODE_CHASE:
      renderModeChase();
      break;
    case MODE_SPARKLE:
      renderModeSparkle();
      break;
    case MODE_FIRE:
      renderModeFire();
      break;
    case MODE_THEATER_CHASE:
      renderModeTheaterChase();
      break;
    case MODE_FADE:
      renderModeFade();
      break;
    default:
      renderModeSolid();
      break;
  }

  FastLED.show();
}

// ===== CALLBACK KHI NHẬN DỮ LIỆU =====
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receivedBtn, incomingData, sizeof(receivedBtn));

  // Bật LED báo hiệu GPIO 8 trong 100ms
  digitalWrite(STATUS_LED_PIN, HIGH);

  Serial.print("\n[Nhận tín hiệu] Nút: ");
  Serial.println(receivedBtn.button);

  // ===== BTN_2: CHUYỂN ĐỔI CHẾ ĐỘ =====
  if (receivedBtn.button == 2) {
    editingMode = !editingMode;
    Serial.print(">>> Chuyển sang chế độ: ");
    Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
    saveSettings();
  }

  // ===== BTN_3: GIẢM ĐỘ SÁNG =====
  else if (receivedBtn.button == 3) {
    unsigned long currentTime = millis();
    uint8_t step = 5; // Mặc định: ấn thường = 5

    // Kiểm tra nếu đang giữ nút (nhận liên tục trong < 300ms)
    if (lastBrightnessButton == 3 && (currentTime - lastBrightnessButtonTime) < 300) {
      brightnessButtonCount++;
      // Nếu giữ >= 2s (khoảng 10 lần với debounce 200ms) → bước nhảy 10
      if (brightnessButtonCount >= 10) {
        step = 10;
      }
    } else {
      // Reset nếu nhấn mới hoặc đổi nút
      brightnessButtonCount = 0;
    }

    lastBrightnessButton = 3;
    lastBrightnessButtonTime = currentTime;

    // Giảm độ sáng
    if (brightness > step) {
      brightness -= step;
    } else {
      brightness = 0;
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    Serial.print("Độ sáng: ");
    Serial.print(brightness);
    Serial.print(" (");
    Serial.print(step);
    Serial.println(")");
    saveSettings();
  }

  // ===== BTN_4: TĂNG ĐỘ SÁNG =====
  else if (receivedBtn.button == 4) {
    unsigned long currentTime = millis();
    uint8_t step = 5; // Mặc định: ấn thường = 5

    // Kiểm tra nếu đang giữ nút (nhận liên tục trong < 300ms)
    if (lastBrightnessButton == 4 && (currentTime - lastBrightnessButtonTime) < 300) {
      brightnessButtonCount++;
      // Nếu giữ >= 2s (khoảng 10 lần với debounce 200ms) → bước nhảy 10
      if (brightnessButtonCount >= 10) {
        step = 10;
      }
    } else {
      // Reset nếu nhấn mới hoặc đổi nút
      brightnessButtonCount = 0;
    }

    lastBrightnessButton = 4;
    lastBrightnessButtonTime = currentTime;

    // Tăng độ sáng
    if (brightness <= 255 - step) {
      brightness += step;
    } else {
      brightness = 255;
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    Serial.print("Độ sáng: ");
    Serial.print(brightness);
    Serial.print(" (");
    Serial.print(step);
    Serial.println(")");
    saveSettings();
  }

  // ===== BTN_5: CHẾ ĐỘ LED TIẾP THEO =====
  else if (receivedBtn.button == 5) {
    unsigned long currentTime = millis();
    // Chỉ chuyển mode nếu đã qua thời gian debounce
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;

      currentMode++;
      if (currentMode >= MODE_COUNT) {
        currentMode = 0;  // Quay về chế độ đầu tiên
      }
      Serial.print(">>> Chế độ LED: ");
      Serial.println(modeNames[currentMode]);
      saveSettings();
    }
  }

  // ===== BTN_6: CHẾ ĐỘ LED TRƯỚC ĐÓ =====
  else if (receivedBtn.button == 6) {
    unsigned long currentTime = millis();
    // Chỉ chuyển mode nếu đã qua thời gian debounce
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;

      if (currentMode == 0) {
        currentMode = MODE_COUNT - 1;  // Quay về chế độ cuối cùng
      } else {
        currentMode--;
      }
      Serial.print(">>> Chế độ LED: ");
      Serial.println(modeNames[currentMode]);
      saveSettings();
    }
  }

  // ===== BTN_7: GIẢM TỐC ĐỘ HIỆU ỨNG =====
  else if (receivedBtn.button == 7) {
    if (effectSpeed > 5) {
      effectSpeed -= 5;
    } else {
      effectSpeed = 0;
    }
    Serial.print(">>> Tốc độ: ");
    Serial.println(effectSpeed);
    saveSettings();
  }

  // ===== BTN_8: TĂNG TỐC ĐỘ HIỆU ỨNG =====
  else if (receivedBtn.button == 8) {
    if (effectSpeed <= 95) {
      effectSpeed += 5;
    } else {
      effectSpeed = 100;
    }
    Serial.print(">>> Tốc độ: ");
    Serial.println(effectSpeed);
    saveSettings();
  }

  // ===== BTN_9: GIẢM FPS =====
  else if (receivedBtn.button == 9) {
    if (fps > 10) {
      fps -= 5;
      if (fps < 10) fps = 10;
    }
    Serial.print(">>> FPS: ");
    Serial.println(fps);
    saveSettings();
  }

  // ===== BTN_10: TĂNG FPS =====
  else if (receivedBtn.button == 10) {
    if (fps < 60) {
      fps += 5;
      if (fps > 60) fps = 60;
    }
    Serial.print(">>> FPS: ");
    Serial.println(fps);
    saveSettings();
  }

  // ===== CHẾ ĐỘ EDIT ĐUÔI (mặc định) =====
  else if (!editingMode) {
    if (receivedBtn.button == 0) {
      // BTN_0: Giảm tổng số LED
      if (numLedsTotal > 0) {
        numLedsTotal--;

        // Nếu numLedsStart >= numLedsTotal, điều chỉnh lại
        if (numLedsStart >= numLedsTotal) {
          numLedsStart = numLedsTotal > 0 ? numLedsTotal - 1 : 0;
        }

        updateLEDs();
        saveSettings();
      } else {
        Serial.println("Đã ở mức tối thiểu (0 LED)");
      }
    }
    else if (receivedBtn.button == 1) {
      // BTN_1: Tăng tổng số LED
      if (numLedsTotal < MAX_LEDS) {
        numLedsTotal++;
        updateLEDs();
        saveSettings();
      } else {
        Serial.println("Đã đạt tối đa (300 LED)");
      }
    }
  }

  // ===== CHẾ ĐỘ EDIT ĐẦU =====
  else {
    if (receivedBtn.button == 0) {
      // BTN_0: Tăng số LED tắt từ đầu (dịch sang phải)
      if (numLedsStart < numLedsTotal - 1) {
        numLedsStart++;
        updateLEDs();
        saveSettings();
      } else {
        Serial.println("Không thể tắt thêm LED từ đầu");
      }
    }
    else if (receivedBtn.button == 1) {
      // BTN_1: Giảm số LED tắt từ đầu (dịch sang trái)
      if (numLedsStart > 0) {
        numLedsStart--;
        updateLEDs();
        saveSettings();
      } else {
        Serial.println("Đã ở vị trí đầu tiên");
      }
    }
  }

  // Tắt LED báo hiệu sau 100ms
  delay(100);
  digitalWrite(STATUS_LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n=== ESP-A RECEIVER (Head/Tail Edit Mode) ===");

  // Cấu hình LED báo hiệu GPIO 8
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  Serial.println("✓ GPIO 8 (LED báo hiệu) đã cấu hình");

  // Khởi tạo WiFi
  WiFi.mode(WIFI_STA);
  Serial.print("✓ MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Lỗi khởi tạo ESP-NOW");
    while (1) delay(1000);
  }
  Serial.println("✓ ESP-NOW đã khởi tạo");

  // Đăng ký callback nhận dữ liệu
  esp_now_register_recv_cb(OnDataRecv);

  // Khởi tạo FastLED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);

  // Đọc cài đặt từ bộ nhớ
  loadSettings();

  // Áp dụng độ sáng đã lưu
  FastLED.setBrightness(brightness);
  Serial.print("✓ FastLED đã khởi tạo (Độ sáng: ");
  Serial.print(brightness);
  Serial.println(")");

  // Hiển thị LED ban đầu
  updateLEDs();

  Serial.println("\n=== SẴN SÀNG NHẬN TÍN HIỆU ===");
  Serial.println("BTN_2: Chuyển đổi chế độ edit (đầu/đuôi)");
  Serial.println("BTN_3: Giảm độ sáng (ấn: -5, giữ 2s: -10)");
  Serial.println("BTN_4: Tăng độ sáng (ấn: +5, giữ 2s: +10)");
  Serial.println("BTN_5: Chế độ LED tiếp theo");
  Serial.println("BTN_6: Chế độ LED trước đó");
  Serial.println("BTN_7: Giảm tốc độ hiệu ứng");
  Serial.println("BTN_8: Tăng tốc độ hiệu ứng");
  Serial.println("BTN_9: Giảm FPS (mượt hơn)");
  Serial.println("BTN_10: Tăng FPS (nhanh hơn)");
  Serial.println("\nChế độ EDIT ĐUÔI:");
  Serial.println("  BTN_0: Giảm tổng số LED");
  Serial.println("  BTN_1: Tăng tổng số LED");
  Serial.println("\nChế độ EDIT ĐẦU:");
  Serial.println("  BTN_0: Tắt thêm LED từ đầu");
  Serial.println("  BTN_1: Bật thêm LED từ đầu\n");
}

void loop() {
  // Cập nhật hiệu ứng LED theo chế độ hiện tại
  unsigned long currentMillis = millis();

  // Tính interval từ FPS: interval (ms) = 1000 / fps
  uint16_t interval = 1000 / fps;

  // Cập nhật hiệu ứng theo FPS
  if (currentMillis - lastEffectUpdate > interval) {
    lastEffectUpdate = currentMillis;
    effectStep++;
    updateLEDs();
  }

  delay(5);  // Delay nhỏ để không chiếm CPU
}
