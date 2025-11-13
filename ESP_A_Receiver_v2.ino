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
 * - BTN_2: Chuyển đổi giữa 2 chế độ edit đầu/đuôi (có indicator)
 * - BTN_3: Giảm độ sáng LED (ấn thường: -5, giữ 2s: -10)
 * - BTN_4: Tăng độ sáng LED (ấn thường: +5, giữ 2s: +10)
 * - BTN_5: Chế độ LED tiếp theo (vòng tròn)
 * - BTN_6: Chế độ LED trước đó (vòng tròn)
 * - BTN_7: Giảm (Speed/FPS tùy mode adjustment)
 * - BTN_8: Tăng (Speed/FPS tùy mode adjustment)
 * - BTN_9: Chuyển adjustment mode (Speed/FPS) + hiển thị visual
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

// Biến adjustment mode (BTN_7/8 điều chỉnh Speed hoặc FPS)
bool adjustmentMode = false;   // false = Speed, true = FPS
unsigned long lastAdjustmentTime = 0;  // Thời gian adjustment cuối
bool showAdjustmentVisual = false;     // Hiển thị visual

// Biến indicator cho edit mode
unsigned long lastEditActivity = 0;    // Thời gian hoạt động edit cuối
bool showEditVisual = false;           // Hiển thị visual edit (10s timeout)

// Biến theo dõi ấn giữ nút (cho BTN_3 và BTN_4)
unsigned long lastBrightnessButtonTime = 0;  // Thời gian nhận nút cuối
uint8_t brightnessButtonCount = 0;           // Số lần nhấn liên tục
uint8_t lastBrightnessButton = 255;          // Nút cuối (3 hoặc 4)

// Biến debounce cho nút chuyển mode
unsigned long lastModeChangeTime = 0;        // Thời gian chuyển LED mode cuối
unsigned long lastAdjustModeChangeTime = 0;  // Thời gian chuyển adjustment mode
unsigned long lastEditModeChangeTime = 0;    // Thời gian chuyển edit mode
const unsigned long modeChangeDebounce = 500; // Debounce 500ms

// ===== CẤU TRÚC DỮ LIỆU NHẬN =====
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
}

void loadSettings() {
  preferences.begin("led-store", true);
  numLedsTotal = preferences.getUShort("numTotal", 50);
  numLedsStart = preferences.getUShort("numStart", 0);
  editingMode = preferences.getBool("editMode", false);
  brightness = preferences.getUChar("brightness", 50);
  currentMode = preferences.getUChar("ledMode", MODE_SOLID);
  effectSpeed = preferences.getUChar("speed", 50);
  fps = preferences.getUChar("fps", 30);
  preferences.end();

  Serial.println("✓ Đã đọc cài đặt từ NVS");
}

// ===== CÁC HÀM RENDER CHO TỪNG CHẾ ĐỘ =====

void renderModeSolid() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Blue;
  }
}

void renderModeRainbow() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CHSV((effectStep + (i - numLedsStart) * 5) % 256, 255, 255);
  }
}

void renderModeBreathing() {
  uint8_t speed = map(effectSpeed, 0, 100, 5, 50);
  uint8_t breath = beatsin8(speed, 50, 255);
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB(0, 0, breath);
  }
}

void renderModeChase() {
  uint16_t activeLeds = numLedsTotal - numLedsStart;
  if (activeLeds > 0) {
    // Tắt vùng active trước
    for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    // Fade
    fadeToBlackBy(&leds[numLedsStart], activeLeds, 20);
    // Vẽ LED hiện tại
    uint16_t pos = numLedsStart + (effectStep % activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Blue;
    }
  }
}

void renderModeSparkle() {
  // Fade vùng active
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i].fadeToBlackBy(50);
  }
  if (random8() < 80 && numLedsTotal > numLedsStart) {
    uint16_t activeLeds = numLedsTotal - numLedsStart;
    uint16_t pos = numLedsStart + random16(activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Blue;
    }
  }
}

void renderModeFire() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CHSV(random8(0, 30), 255, random8(100, 255));
  }
}

void renderModeTheaterChase() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    if ((i + effectStep) % 3 == 0) {
      leds[i] = CRGB::Blue;
    } else {
      leds[i] = CRGB::Black;
    }
  }
}

void renderModeFade() {
  uint8_t speed = map(effectSpeed, 0, 100, 5, 40);
  uint8_t fadeBrightness = beatsin8(speed, 0, 255);
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB(0, 0, fadeBrightness);
  }
}

// ===== HÀM VẼ EDIT VISUAL =====
void drawEditVisual() {
  // Tắt tất cả LED
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  // Hiển thị giá trị đang edit dưới dạng 8-bit
  uint8_t valueToShow = editingMode ? numLedsStart : numLedsTotal;

  // Hiển thị 8 bit (LEDs 0-7)
  for (uint8_t bit = 0; bit < 8 && bit < MAX_LEDS; bit++) {
    if (valueToShow & (1 << bit)) {
      leds[bit] = CRGB::Cyan;  // Bit = 1: Cyan
    } else {
      leds[bit] = CRGB::Black;  // Bit = 0: Tắt
    }
  }

  // LED thứ 8: Chỉ báo chế độ edit
  if (MAX_LEDS > 8) {
    leds[8] = editingMode ? CRGB::Red : CRGB::Green;  // Đỏ = Edit đầu, Xanh = Edit đuôi
  }

  // LED thứ 9: Chỉ báo đang ở chế độ edit (luôn trắng)
  if (MAX_LEDS > 9) {
    leds[9] = CRGB::White;
  }

  FastLED.show();
}

// ===== HÀM VẼ ADJUSTMENT VISUAL =====
void drawAdjustmentVisual() {
  // Tắt tất cả LED
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  if (!adjustmentMode) {
    // Mode Speed: Thanh LED trắng từ trái qua phải
    uint16_t barLength = map(effectSpeed, 0, 100, 0, MAX_LEDS);
    for (uint16_t i = 0; i < barLength && i < MAX_LEDS; i++) {
      leds[i] = CRGB::White;
    }
  } else {
    // Mode FPS: Hiển thị 8 bit
    uint8_t fpsBits = map(fps, 10, 60, 0, 255);
    for (uint8_t bit = 0; bit < 8 && bit < MAX_LEDS; bit++) {
      if (fpsBits & (1 << bit)) {
        leds[bit] = (bit == 0) ? CRGB::Red : CRGB::White;
      } else {
        leds[bit] = CRGB::Black;
      }
    }
  }

  // Thêm indicator cho adjustment mode ở LED 8
  if (MAX_LEDS > 8) {
    leds[8] = adjustmentMode ? CRGB::Blue : CRGB::Yellow;  // Xanh = FPS, Vàng = Speed
  }

  FastLED.show();
}

// ===== HÀM CẬP NHẬT HIỂN THỊ LED =====
void updateLEDs() {
  // Tắt LED ngoài vùng active
  for (uint16_t i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
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

  // Bật LED báo hiệu GPIO 8
  digitalWrite(STATUS_LED_PIN, HIGH);

  Serial.print("\n[Nhận tín hiệu] Nút: ");
  Serial.println(receivedBtn.button);

  // ===== BTN_2: CHUYỂN ĐỔI CHẾ ĐỘ EDIT =====
  if (receivedBtn.button == 2) {
    unsigned long currentTime = millis();
    if (currentTime - lastEditModeChangeTime > modeChangeDebounce) {
      lastEditModeChangeTime = currentTime;
      editingMode = !editingMode;
      showEditVisual = true;
      lastEditActivity = currentTime;
      Serial.print(">>> Chế độ edit: ");
      Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
      drawEditVisual();
      saveSettings();
    }
  }

  // ===== BTN_3: GIẢM ĐỘ SÁNG =====
  else if (receivedBtn.button == 3) {
    unsigned long currentTime = millis();
    uint8_t step = 5;

    if (lastBrightnessButton == 3 && (currentTime - lastBrightnessButtonTime) < 300) {
      brightnessButtonCount++;
      if (brightnessButtonCount >= 10) {
        step = 10;
      }
    } else {
      brightnessButtonCount = 0;
    }

    lastBrightnessButton = 3;
    lastBrightnessButtonTime = currentTime;

    if (brightness > step) {
      brightness -= step;
    } else {
      brightness = 0;
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    Serial.print("Độ sáng: ");
    Serial.print(brightness);
    Serial.print(" (-");
    Serial.print(step);
    Serial.println(")");
    saveSettings();
  }

  // ===== BTN_4: TĂNG ĐỘ SÁNG =====
  else if (receivedBtn.button == 4) {
    unsigned long currentTime = millis();
    uint8_t step = 5;

    if (lastBrightnessButton == 4 && (currentTime - lastBrightnessButtonTime) < 300) {
      brightnessButtonCount++;
      if (brightnessButtonCount >= 10) {
        step = 10;
      }
    } else {
      brightnessButtonCount = 0;
    }

    lastBrightnessButton = 4;
    lastBrightnessButtonTime = currentTime;

    if (brightness <= 255 - step) {
      brightness += step;
    } else {
      brightness = 255;
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    Serial.print("Độ sáng: ");
    Serial.print(brightness);
    Serial.print(" (+");
    Serial.print(step);
    Serial.println(")");
    saveSettings();
  }

  // ===== BTN_5: CHẾ ĐỘ LED TIẾP THEO =====
  else if (receivedBtn.button == 5) {
    unsigned long currentTime = millis();
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;

      currentMode++;
      if (currentMode >= MODE_COUNT) {
        currentMode = 0;
      }
      Serial.print(">>> Chế độ LED: ");
      Serial.println(modeNames[currentMode]);
      saveSettings();
    }
  }

  // ===== BTN_6: CHẾ ĐỘ LED TRƯỚC ĐÓ =====
  else if (receivedBtn.button == 6) {
    unsigned long currentTime = millis();
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;

      if (currentMode == 0) {
        currentMode = MODE_COUNT - 1;
      } else {
        currentMode--;
      }
      Serial.print(">>> Chế độ LED: ");
      Serial.println(modeNames[currentMode]);
      saveSettings();
    }
  }

  // ===== BTN_7: GIẢM (Speed/FPS) =====
  else if (receivedBtn.button == 7) {
    if (!adjustmentMode) {
      // Giảm Speed
      if (effectSpeed > 5) {
        effectSpeed -= 5;
      } else {
        effectSpeed = 0;
      }
      Serial.print(">>> Tốc độ: ");
      Serial.println(effectSpeed);
    } else {
      // Giảm FPS
      if (fps > 10) {
        fps -= 5;
        if (fps < 10) fps = 10;
      }
      Serial.print(">>> FPS: ");
      Serial.println(fps);
    }
    saveSettings();

    // Hiển thị visual
    showAdjustmentVisual = true;
    lastAdjustmentTime = millis();
    drawAdjustmentVisual();
  }

  // ===== BTN_8: TĂNG (Speed/FPS) =====
  else if (receivedBtn.button == 8) {
    if (!adjustmentMode) {
      // Tăng Speed
      if (effectSpeed <= 95) {
        effectSpeed += 5;
      } else {
        effectSpeed = 100;
      }
      Serial.print(">>> Tốc độ: ");
      Serial.println(effectSpeed);
    } else {
      // Tăng FPS
      if (fps < 60) {
        fps += 5;
        if (fps > 60) fps = 60;
      }
      Serial.print(">>> FPS: ");
      Serial.println(fps);
    }
    saveSettings();

    // Hiển thị visual
    showAdjustmentVisual = true;
    lastAdjustmentTime = millis();
    drawAdjustmentVisual();
  }

  // ===== BTN_9: CHUYỂN ADJUSTMENT MODE =====
  else if (receivedBtn.button == 9) {
    unsigned long currentTime = millis();
    if (currentTime - lastAdjustModeChangeTime > modeChangeDebounce) {
      lastAdjustModeChangeTime = currentTime;
      adjustmentMode = !adjustmentMode;
      Serial.print(">>> Adjustment: ");
      Serial.println(adjustmentMode ? "FPS" : "SPEED");

      // Hiển thị visual 3s
      showAdjustmentVisual = true;
      lastAdjustmentTime = currentTime;
      drawAdjustmentVisual();
    }
  }

  // ===== BTN_0/1: TĂNG/GIẢM LED =====
  else if (!editingMode) {
    // Chế độ EDIT ĐUÔI
    if (receivedBtn.button == 0) {
      if (numLedsTotal > 0) {
        numLedsTotal--;
        if (numLedsStart >= numLedsTotal) {
          numLedsStart = numLedsTotal > 0 ? numLedsTotal - 1 : 0;
        }
        showEditVisual = true;
        lastEditActivity = millis();
        drawEditVisual();
        saveSettings();
      }
    } else if (receivedBtn.button == 1) {
      if (numLedsTotal < MAX_LEDS) {
        numLedsTotal++;
        showEditVisual = true;
        lastEditActivity = millis();
        drawEditVisual();
        saveSettings();
      }
    }
  } else {
    // Chế độ EDIT ĐẦU
    if (receivedBtn.button == 0) {
      if (numLedsStart < numLedsTotal - 1) {
        numLedsStart++;
        showEditVisual = true;
        lastEditActivity = millis();
        drawEditVisual();
        saveSettings();
      }
    } else if (receivedBtn.button == 1) {
      if (numLedsStart > 0) {
        numLedsStart--;
        showEditVisual = true;
        lastEditActivity = millis();
        drawEditVisual();
        saveSettings();
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

  Serial.println("\n\n=== ESP-A RECEIVER ===");

  // Cấu hình LED báo hiệu GPIO 8
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Khởi tạo WiFi
  WiFi.mode(WIFI_STA);
  Serial.print("✓ MAC: ");
  Serial.println(WiFi.macAddress());

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Lỗi ESP-NOW");
    while (1) delay(1000);
  }
  Serial.println("✓ ESP-NOW OK");

  // Đăng ký callback
  esp_now_register_recv_cb(OnDataRecv);

  // Khởi tạo FastLED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);

  // Đọc cài đặt
  loadSettings();

  // Áp dụng độ sáng
  FastLED.setBrightness(brightness);
  Serial.print("✓ Độ sáng: ");
  Serial.println(brightness);

  // Hiển thị LED
  updateLEDs();

  Serial.println("\n=== SẴN SÀNG ===");
  Serial.println("BTN_0/1: Tăng/Giảm LED");
  Serial.println("BTN_2: Chuyển edit mode");
  Serial.println("BTN_3/4: Độ sáng");
  Serial.println("BTN_5/6: Chế độ LED");
  Serial.println("BTN_7/8: Speed/FPS");
  Serial.println("BTN_9: Chuyển Speed/FPS\n");
}

void loop() {
  unsigned long currentMillis = millis();

  // Kiểm tra timeout cho edit visual (10 giây)
  if (showEditVisual && (currentMillis - lastEditActivity > 10000)) {
    showEditVisual = false;
  }

  // Kiểm tra timeout cho adjustment visual (3 giây)
  if (showAdjustmentVisual && (currentMillis - lastAdjustmentTime > 3000)) {
    showAdjustmentVisual = false;
  }

  // Ưu tiên 1: Hiển thị edit visual (BTN_0/1/2)
  if (showEditVisual) {
    drawEditVisual();
    delay(50);
  }
  // Ưu tiên 2: Hiển thị adjustment visual (BTN_7/8/9)
  else if (showAdjustmentVisual) {
    drawAdjustmentVisual();
    delay(50);
  }
  // Ưu tiên 3: Hiển thị hiệu ứng LED bình thường
  else {
    uint16_t interval = 1000 / fps;

    if (currentMillis - lastEffectUpdate > interval) {
      lastEffectUpdate = currentMillis;
      effectStep++;
      updateLEDs();
    }

    delay(5);
  }
}
