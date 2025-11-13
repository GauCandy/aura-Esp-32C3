/*
 * ESP32-C3 A - RECEIVER (17-Bit Smart Adjustment System)
 *
 * HỆ THỐNG ĐIỀU CHỈNH THÔNG MINH:
 * - BTN_2: Cycle qua các chế độ điều chỉnh (Brightness, LED Head, LED Tail, Speed, FPS, Color)
 * - BTN_0: Giảm giá trị (mặc định: -1, giữ: -5)
 * - BTN_1: Tăng giá trị (mặc định: +1, giữ: +5)
 * - BTN_3: Save nhanh và thoát adjustment mode
 * - BTN_5/6: Cycle qua các LED effect modes
 *
 * 17-BIT INDICATORS (luôn căn giữa):
 * - LED 0: Đỏ (chỉ hướng đọc bit)
 * - LED 1-8: Giá trị (0-255) - 8 bits
 * - LED 9-16: Chế độ hiện tại - 8 bits pattern
 *
 * Khi ở adjustment mode: TẮT toàn bộ dải LED, chỉ hiển thị 17-bit indicators
 * Timeout: 10s không tương tác → auto save và quay về hiệu ứng bình thường
 */

#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

// ===== CẤU HÌNH LED =====
#define LED_PIN 2              // Chân LED WS2812B
#define MAX_LEDS 300           // Số LED tối đa
#define MIN_LEDS 17            // Số LED tối thiểu (để hiển thị 17-bit indicators)
#define STATUS_LED_PIN 8       // LED báo hiệu (LED thường)

CRGB leds[MAX_LEDS];
Preferences preferences;       // Lưu trữ vào NVS

// ===== ĐỊNH NGHĨA CÁC CHẾ ĐỘ ĐIỀU CHỈNH =====
enum AdjustMode {
  ADJUST_BRIGHTNESS = 0,  // Độ sáng
  ADJUST_LED_TAIL,        // Số LED (edit đuôi)
  ADJUST_LED_HEAD,        // Số LED (edit đầu)
  ADJUST_SPEED,           // Tốc độ hiệu ứng
  ADJUST_FPS,             // FPS
  ADJUST_COLOR,           // Chế độ màu
  ADJUST_COUNT            // Tổng số chế độ
};

const char* adjustModeNames[] = {
  "Brightness",
  "LED Tail",
  "LED Head",
  "Speed",
  "FPS",
  "Color"
};

// ===== ĐỊNH NGHĨA CÁC CHẾ ĐỘ LED =====
enum LedMode {
  MODE_SOLID = 0,      // Đơn sắc
  MODE_RAINBOW,        // Cầu vồng
  MODE_BREATHING,      // Breathing (hít thở)
  MODE_CHASE,          // Chạy đuổi
  MODE_SPARKLE,        // Lấp lánh
  MODE_FIRE,           // Lửa
  MODE_THEATER_CHASE,  // Theater chase
  MODE_FADE,           // Fade in/out
  MODE_COUNT           // Tổng số mode
};

const char* ledModeNames[] = {
  "Solid",
  "Rainbow",
  "Breathing",
  "Chase",
  "Sparkle",
  "Fire",
  "Theater Chase",
  "Fade"
};

// ===== ĐỊNH NGHĨA CÁC CHẾ ĐỘ MÀU =====
enum ColorMode {
  COLOR_BLUE = 0,      // Xanh dương
  COLOR_RED,           // Đỏ
  COLOR_GREEN,         // Xanh lá
  COLOR_CYAN,          // Cyan
  COLOR_MAGENTA,       // Magenta
  COLOR_YELLOW,        // Vàng
  COLOR_WHITE,         // Trắng
  COLOR_RGB,           // RGB (đa sắc)
  COLOR_COUNT
};

const char* colorModeNames[] = {
  "Blue",
  "Red",
  "Green",
  "Cyan",
  "Magenta",
  "Yellow",
  "White",
  "RGB"
};

// ===== BIẾN ĐIỀU KHIỂN =====
uint16_t numLedsTotal = 50;    // Tổng số LED được điều khiển (17-300)
uint16_t numLedsStart = 0;     // Số LED tắt từ đầu (vị trí bắt đầu)
uint8_t brightness = 128;      // Độ sáng LED (0-255), mặc định 128
uint8_t currentLedMode = MODE_SOLID;    // Chế độ LED hiện tại
uint8_t currentColorMode = COLOR_BLUE;  // Chế độ màu hiện tại
uint8_t effectSpeed = 128;     // Tốc độ hiệu ứng (0-255), mặc định 128
uint8_t fps = 30;              // FPS (10-60), mặc định 30

// Biến cho hiệu ứng
unsigned long lastEffectUpdate = 0;
uint16_t effectStep = 0;

// Biến adjustment mode
uint8_t currentAdjustMode = ADJUST_BRIGHTNESS;  // Chế độ điều chỉnh hiện tại
bool inAdjustmentMode = false;                  // Đang ở adjustment mode?
unsigned long lastAdjustActivity = 0;           // Thời gian hoạt động cuối
bool needsSave = false;                         // Cần save?
unsigned long blinkTimer = 0;                   // Timer cho nhấp nháy

// Biến theo dõi ấn giữ nút (cho BTN_0 và BTN_1)
unsigned long lastButtonTime = 0;      // Thời gian nhận nút cuối
uint8_t buttonPressCount = 0;          // Số lần nhấn liên tục
uint8_t lastButton = 255;              // Nút cuối (0 hoặc 1)

// Biến debounce cho nút chuyển mode
unsigned long lastModeChangeTime = 0;         // Thời gian chuyển LED mode cuối
unsigned long lastAdjustModeChangeTime = 0;   // Thời gian chuyển adjust mode cuối
const unsigned long modeChangeDebounce = 500; // Debounce 500ms

// ===== CẤU TRÚC DỮ LIỆU NHẬN =====
typedef struct button_message {
  uint8_t button;              // Số nút được nhấn
} button_message;

button_message receivedBtn;

// ===== HÀM LƯU VÀ ĐỌC CÀI ĐẶT =====
void saveSettings() {
  preferences.begin("led-store", false);
  preferences.putUShort("numTotal", numLedsTotal);
  preferences.putUShort("numStart", numLedsStart);
  preferences.putUChar("brightness", brightness);
  preferences.putUChar("ledMode", currentLedMode);
  preferences.putUChar("colorMode", currentColorMode);
  preferences.putUChar("speed", effectSpeed);
  preferences.putUChar("fps", fps);
  preferences.end();
  needsSave = false;
  Serial.println("✓ Đã lưu cài đặt");
}

void loadSettings() {
  preferences.begin("led-store", true);
  numLedsTotal = preferences.getUShort("numTotal", 50);
  numLedsStart = preferences.getUShort("numStart", 0);
  brightness = preferences.getUChar("brightness", 128);
  currentLedMode = preferences.getUChar("ledMode", MODE_SOLID);
  currentColorMode = preferences.getUChar("colorMode", COLOR_BLUE);
  effectSpeed = preferences.getUChar("speed", 128);
  fps = preferences.getUChar("fps", 30);
  preferences.end();

  // Đảm bảo ít nhất MIN_LEDS
  if (numLedsTotal < MIN_LEDS) numLedsTotal = MIN_LEDS;

  Serial.println("✓ Đã đọc cài đặt từ NVS");
}

// ===== HÀM LẤY MÀU THEO COLOR MODE =====
CRGB getColorForMode() {
  switch (currentColorMode) {
    case COLOR_BLUE:    return CRGB::Blue;
    case COLOR_RED:     return CRGB::Red;
    case COLOR_GREEN:   return CRGB::Green;
    case COLOR_CYAN:    return CRGB::Cyan;
    case COLOR_MAGENTA: return CRGB::Magenta;
    case COLOR_YELLOW:  return CRGB::Yellow;
    case COLOR_WHITE:   return CRGB::White;
    case COLOR_RGB:     return CRGB::White;  // RGB mode không dùng màu cố định
    default:            return CRGB::Blue;
  }
}

// ===== CÁC HÀM RENDER CHO TỪNG CHẾ ĐỘ LED =====

void renderModeSolid() {
  CRGB color = getColorForMode();
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = color;
  }
}

void renderModeRainbow() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    if (currentColorMode == COLOR_RGB) {
      leds[i] = CHSV((effectStep + (i - numLedsStart) * 5) % 256, 255, 255);
    } else {
      // Rainbow với màu chủ đạo
      uint8_t hue = (effectStep + (i - numLedsStart) * 5) % 256;
      leds[i] = CHSV(hue, 255, 255);
    }
  }
}

void renderModeBreathing() {
  uint8_t speed = map(effectSpeed, 0, 255, 5, 50);
  uint8_t breath = beatsin8(speed, 50, 255);
  CRGB color = getColorForMode();
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = color;
    leds[i].fadeToBlackBy(255 - breath);
  }
}

void renderModeChase() {
  uint16_t activeLeds = numLedsTotal - numLedsStart;
  if (activeLeds > 0) {
    for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    fadeToBlackBy(&leds[numLedsStart], activeLeds, 20);
    uint16_t pos = numLedsStart + (effectStep % activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = getColorForMode();
    }
  }
}

void renderModeSparkle() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i].fadeToBlackBy(50);
  }
  if (random8() < 80 && numLedsTotal > numLedsStart) {
    uint16_t activeLeds = numLedsTotal - numLedsStart;
    uint16_t pos = numLedsStart + random16(activeLeds);
    if (pos < MAX_LEDS) {
      leds[pos] = getColorForMode();
    }
  }
}

void renderModeFire() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    if (currentColorMode == COLOR_RGB) {
      leds[i] = CHSV(random8(0, 30), 255, random8(100, 255));
    } else {
      leds[i] = getColorForMode();
      leds[i].fadeToBlackBy(random8(0, 155));
    }
  }
}

void renderModeTheaterChase() {
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    if ((i + effectStep) % 3 == 0) {
      leds[i] = getColorForMode();
    } else {
      leds[i] = CRGB::Black;
    }
  }
}

void renderModeFade() {
  uint8_t speed = map(effectSpeed, 0, 255, 5, 40);
  uint8_t fadeBrightness = beatsin8(speed, 0, 255);
  CRGB color = getColorForMode();
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = color;
    leds[i].fadeToBlackBy(255 - fadeBrightness);
  }
}

// ===== HÀM VẼ 17-BIT INDICATORS =====
void draw17BitIndicators() {
  // Tắt tất cả LED trước
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  // Tính vị trí căn giữa
  uint16_t centerStart = 0;
  if (numLedsTotal >= 17) {
    centerStart = (numLedsTotal - 17) / 2;
  }

  // LED 0: Đỏ (chỉ hướng đọc)
  leds[centerStart] = CRGB::Red;

  // LED 1-8 và LED 9-16 tùy thuộc vào chế độ
  switch (currentAdjustMode) {
    case ADJUST_BRIGHTNESS:
      drawBrightnessIndicator(centerStart);
      break;
    case ADJUST_LED_TAIL:
      drawLEDTailIndicator(centerStart);
      break;
    case ADJUST_LED_HEAD:
      drawLEDHeadIndicator(centerStart);
      break;
    case ADJUST_SPEED:
      drawSpeedIndicator(centerStart);
      break;
    case ADJUST_FPS:
      drawFPSIndicator(centerStart);
      break;
    case ADJUST_COLOR:
      drawColorIndicator(centerStart);
      break;
  }

  FastLED.show();
}

// ===== HÀM VẼ CHO TỪNG CHẾ ĐỘ ĐIỀU CHỈNH =====

void drawBrightnessIndicator(uint16_t start) {
  // LED 1-8: Hiển thị độ sáng từ sáng → tối dần
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t pos = start + 1 + i;
    if (pos < MAX_LEDS) {
      // Tính độ sáng cho mỗi LED (LED 1 sáng nhất, LED 8 tối nhất)
      uint8_t ledBrightness = map(brightness, 0, 255, 0, 255 - (i * 30));
      leds[pos] = CRGB(ledBrightness, ledBrightness, ledBrightness);
    }
  }

  // LED 9-16: Pattern cho brightness (màu vàng)
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t pos = start + 9 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Yellow;
    }
  }
}

void drawLEDTailIndicator(uint16_t start) {
  // LED 1-6: Trắng
  for (uint8_t i = 0; i < 6; i++) {
    uint16_t pos = start + 1 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::White;
    }
  }
  // LED 7-8: Đỏ (chỉ báo tail)
  for (uint8_t i = 6; i < 8; i++) {
    uint16_t pos = start + 1 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Red;
    }
  }

  // LED 9-16: 8-bit binary hiển thị numLedsTotal
  for (uint8_t bit = 0; bit < 8; bit++) {
    uint16_t pos = start + 9 + bit;
    if (pos < MAX_LEDS) {
      if (numLedsTotal & (1 << bit)) {
        leds[pos] = CRGB::Cyan;
      }
    }
  }

  // Nhấp nháy LED ở vị trí numLedsTotal-1
  if ((millis() / 500) % 2 == 0 && numLedsTotal > 0) {
    uint16_t blinkPos = numLedsTotal - 1;
    if (blinkPos < MAX_LEDS && (blinkPos < start || blinkPos >= start + 17)) {
      leds[blinkPos] = CRGB::White;
    }
  }
}

void drawLEDHeadIndicator(uint16_t start) {
  // LED 1-6: Đỏ (chỉ báo head)
  for (uint8_t i = 0; i < 6; i++) {
    uint16_t pos = start + 1 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::Red;
    }
  }
  // LED 7-8: Trắng
  for (uint8_t i = 6; i < 8; i++) {
    uint16_t pos = start + 1 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = CRGB::White;
    }
  }

  // LED 9-16: 8-bit binary hiển thị numLedsStart
  for (uint8_t bit = 0; bit < 8; bit++) {
    uint16_t pos = start + 9 + bit;
    if (pos < MAX_LEDS) {
      if (numLedsStart & (1 << bit)) {
        leds[pos] = CRGB::Cyan;
      }
    }
  }

  // Nhấp nháy LED ở vị trí numLedsStart
  if ((millis() / 500) % 2 == 0) {
    uint16_t blinkPos = numLedsStart;
    if (blinkPos < MAX_LEDS && (blinkPos < start || blinkPos >= start + 17)) {
      leds[blinkPos] = CRGB::White;
    }
  }
}

void drawSpeedIndicator(uint16_t start) {
  // LED 1-8: 8-bit binary hiển thị effectSpeed
  for (uint8_t bit = 0; bit < 8; bit++) {
    uint16_t pos = start + 1 + bit;
    if (pos < MAX_LEDS) {
      if (effectSpeed & (1 << bit)) {
        leds[pos] = CRGB::White;
      }
    }
  }

  // LED 9-16: Đốm sáng chạy từ trái qua phải
  uint8_t dotPos = map(effectSpeed, 0, 255, 0, 7);
  uint16_t pos = start + 9 + dotPos;
  if (pos < MAX_LEDS) {
    leds[pos] = CRGB::Green;
  }
}

void drawFPSIndicator(uint16_t start) {
  // LED 1-8: 8-bit binary hiển thị FPS (map 10-60 → 0-255)
  uint8_t fpsValue = map(fps, 10, 60, 0, 255);
  for (uint8_t bit = 0; bit < 8; bit++) {
    uint16_t pos = start + 1 + bit;
    if (pos < MAX_LEDS) {
      if (fpsValue & (1 << bit)) {
        leds[pos] = CRGB::White;
      }
    }
  }

  // LED 9-16: Nhấp nháy để báo đây là chế độ FPS
  if ((millis() / 300) % 2 == 0) {
    for (uint8_t i = 0; i < 8; i++) {
      uint16_t pos = start + 9 + i;
      if (pos < MAX_LEDS) {
        leds[pos] = CRGB::Blue;
      }
    }
  }
}

void drawColorIndicator(uint16_t start) {
  // LED 1-8: Không sáng (hoặc hiển thị index màu)
  uint8_t colorIndex = currentColorMode;
  for (uint8_t bit = 0; bit < 8; bit++) {
    uint16_t pos = start + 1 + bit;
    if (pos < MAX_LEDS) {
      if (colorIndex & (1 << bit)) {
        leds[pos] = CRGB::White;
      }
    }
  }

  // LED 9-16: Hiển thị màu hiện tại
  CRGB displayColor = getColorForMode();
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t pos = start + 9 + i;
    if (pos < MAX_LEDS) {
      leds[pos] = displayColor;
    }
  }
}

// ===== HÀM CẬP NHẬT HIỂN THỊ LED BÌNH THƯỜNG =====
void updateNormalLEDs() {
  // Tắt LED ngoài vùng active
  for (uint16_t i = 0; i < numLedsStart && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  for (uint16_t i = numLedsTotal; i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }

  // Render theo chế độ hiện tại
  switch (currentLedMode) {
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

  // ===== BTN_2: CYCLE QUA CÁC ADJUSTMENT MODES =====
  if (receivedBtn.button == 2) {
    unsigned long currentTime = millis();
    if (currentTime - lastAdjustModeChangeTime > modeChangeDebounce) {
      lastAdjustModeChangeTime = currentTime;

      // Cycle qua các adjustment modes
      currentAdjustMode++;
      if (currentAdjustMode >= ADJUST_COUNT) {
        currentAdjustMode = 0;
      }

      // Vào adjustment mode
      inAdjustmentMode = true;
      lastAdjustActivity = currentTime;
      needsSave = true;

      Serial.print(">>> Adjustment Mode: ");
      Serial.println(adjustModeNames[currentAdjustMode]);

      draw17BitIndicators();
    }
  }

  // ===== BTN_3: SAVE NHANH VÀ THOÁT ADJUSTMENT MODE =====
  else if (receivedBtn.button == 3) {
    if (needsSave) {
      saveSettings();
    }
    inAdjustmentMode = false;
    Serial.println(">>> Đã save và thoát adjustment mode");
  }

  // ===== BTN_5: CHẾ ĐỘ LED TIẾP THEO =====
  else if (receivedBtn.button == 5) {
    unsigned long currentTime = millis();
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;
      currentLedMode++;
      if (currentLedMode >= MODE_COUNT) {
        currentLedMode = 0;
      }
      needsSave = true;
      Serial.print(">>> LED Mode: ");
      Serial.println(ledModeNames[currentLedMode]);
    }
  }

  // ===== BTN_6: CHẾ ĐỘ LED TRƯỚC ĐÓ =====
  else if (receivedBtn.button == 6) {
    unsigned long currentTime = millis();
    if (currentTime - lastModeChangeTime > modeChangeDebounce) {
      lastModeChangeTime = currentTime;
      if (currentLedMode == 0) {
        currentLedMode = MODE_COUNT - 1;
      } else {
        currentLedMode--;
      }
      needsSave = true;
      Serial.print(">>> LED Mode: ");
      Serial.println(ledModeNames[currentLedMode]);
    }
  }

  // ===== BTN_0/1: TĂNG/GIẢM GIÁ TRỊ =====
  else if (receivedBtn.button == 0 || receivedBtn.button == 1) {
    unsigned long currentTime = millis();
    int8_t step = 1;  // Mặc định: ±1

    // Kiểm tra nếu đang giữ nút (nhận liên tục trong < 300ms)
    if (lastButton == receivedBtn.button && (currentTime - lastButtonTime) < 300) {
      buttonPressCount++;
      // Nếu giữ >= 1.5s (khoảng 5 lần với debounce 300ms) → bước nhảy 5
      if (buttonPressCount >= 5) {
        step = 5;
      }
    } else {
      // Reset nếu nhấn mới hoặc đổi nút
      buttonPressCount = 0;
    }

    lastButton = receivedBtn.button;
    lastButtonTime = currentTime;

    // Vào adjustment mode nếu chưa vào
    if (!inAdjustmentMode) {
      inAdjustmentMode = true;
      Serial.print(">>> Vào Adjustment Mode: ");
      Serial.println(adjustModeNames[currentAdjustMode]);
    }

    lastAdjustActivity = currentTime;
    needsSave = true;

    // Áp dụng thay đổi tùy theo chế độ
    bool isDecrease = (receivedBtn.button == 0);
    adjustValue(step, isDecrease);

    draw17BitIndicators();
  }

  // Tắt LED báo hiệu sau 100ms
  delay(100);
  digitalWrite(STATUS_LED_PIN, LOW);
}

// ===== HÀM ĐIỀU CHỈNH GIÁ TRỊ =====
void adjustValue(int8_t step, bool decrease) {
  int16_t change = decrease ? -step : step;

  switch (currentAdjustMode) {
    case ADJUST_BRIGHTNESS:
      {
        int16_t newBrightness = (int16_t)brightness + change;
        if (newBrightness < 0) newBrightness = 0;
        if (newBrightness > 255) newBrightness = 255;
        brightness = newBrightness;
        FastLED.setBrightness(brightness);
        Serial.print("Brightness: ");
        Serial.println(brightness);
      }
      break;

    case ADJUST_LED_TAIL:
      {
        int16_t newTotal = (int16_t)numLedsTotal + change;
        if (newTotal < MIN_LEDS) newTotal = MIN_LEDS;
        if (newTotal > MAX_LEDS) newTotal = MAX_LEDS;
        numLedsTotal = newTotal;
        // Đảm bảo numLedsStart không vượt quá numLedsTotal
        if (numLedsStart >= numLedsTotal) {
          numLedsStart = numLedsTotal > 0 ? numLedsTotal - 1 : 0;
        }
        Serial.print("LED Total: ");
        Serial.println(numLedsTotal);
      }
      break;

    case ADJUST_LED_HEAD:
      {
        int16_t newStart = (int16_t)numLedsStart + change;
        if (newStart < 0) newStart = 0;
        if (newStart >= numLedsTotal - 1) newStart = numLedsTotal - 1;
        numLedsStart = newStart;
        Serial.print("LED Start: ");
        Serial.println(numLedsStart);
      }
      break;

    case ADJUST_SPEED:
      {
        int16_t newSpeed = (int16_t)effectSpeed + change;
        if (newSpeed < 0) newSpeed = 0;
        if (newSpeed > 255) newSpeed = 255;
        effectSpeed = newSpeed;
        Serial.print("Speed: ");
        Serial.println(effectSpeed);
      }
      break;

    case ADJUST_FPS:
      {
        int16_t newFPS = (int16_t)fps + change;
        if (newFPS < 10) newFPS = 10;
        if (newFPS > 60) newFPS = 60;
        fps = newFPS;
        Serial.print("FPS: ");
        Serial.println(fps);
      }
      break;

    case ADJUST_COLOR:
      {
        // Cycle qua các color modes
        if (decrease) {
          if (currentColorMode == 0) {
            currentColorMode = COLOR_COUNT - 1;
          } else {
            currentColorMode--;
          }
        } else {
          currentColorMode++;
          if (currentColorMode >= COLOR_COUNT) {
            currentColorMode = 0;
          }
        }
        Serial.print("Color Mode: ");
        Serial.println(colorModeNames[currentColorMode]);
      }
      break;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n=== ESP-A RECEIVER (17-Bit Smart Adjustment) ===");

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
  Serial.print("✓ Brightness: ");
  Serial.println(brightness);
  Serial.print("✓ LED Total: ");
  Serial.println(numLedsTotal);

  // Hiển thị LED
  updateNormalLEDs();

  Serial.println("\n=== SẴN SÀNG ===");
  Serial.println("BTN_2: Cycle Adjustment Modes");
  Serial.println("BTN_0: Giảm giá trị (-1, giữ: -5)");
  Serial.println("BTN_1: Tăng giá trị (+1, giữ: +5)");
  Serial.println("BTN_3: Save nhanh");
  Serial.println("BTN_5/6: Cycle LED Modes\n");
}

// ===== LOOP =====
void loop() {
  unsigned long currentMillis = millis();

  // Kiểm tra timeout cho adjustment mode (10 giây)
  if (inAdjustmentMode && (currentMillis - lastAdjustActivity > 10000)) {
    inAdjustmentMode = false;
    if (needsSave) {
      saveSettings();
    }
    Serial.println(">>> Timeout: Thoát adjustment mode");
  }

  // Hiển thị tùy theo mode
  if (inAdjustmentMode) {
    draw17BitIndicators();
    delay(50);
  } else {
    // Cập nhật hiệu ứng LED bình thường
    uint16_t interval = 1000 / fps;

    if (currentMillis - lastEffectUpdate > interval) {
      lastEffectUpdate = currentMillis;
      effectStep++;
      updateNormalLEDs();
    }

    delay(5);
  }
}
