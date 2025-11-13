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
 * - BTN_2: Chuyển đổi giữa 2 chế độ
 * - BTN_3: Giảm độ sáng LED (ấn thường: -5, giữ 2s: -10)
 * - BTN_4: Tăng độ sáng LED (ấn thường: +5, giữ 2s: +10)
 * - Lưu tất cả vào NVS (giữ khi mất điện)
 * - LED GPIO 8: Sáng 100ms mỗi khi nhận tín hiệu
 * - Màu: Blue
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

// ===== BIẾN ĐIỀU KHIỂN =====
uint16_t numLedsTotal = 50;    // Tổng số LED được điều khiển (0-300)
uint16_t numLedsStart = 0;     // Số LED tắt từ đầu (vị trí bắt đầu)
bool editingMode = false;      // false = edit đuôi, true = edit đầu
uint8_t brightness = 50;       // Độ sáng LED (0-255), mặc định 50

// Biến theo dõi ấn giữ nút (cho BTN_3 và BTN_4)
unsigned long lastBrightnessButtonTime = 0;  // Thời gian nhận nút cuối
uint8_t brightnessButtonCount = 0;           // Số lần nhấn liên tục
uint8_t lastBrightnessButton = 255;          // Nút cuối (3 hoặc 4)

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
  preferences.end();

  Serial.println("✓ Đã lưu cài đặt vào NVS:");
  Serial.print("  - Tổng LED: ");
  Serial.println(numLedsTotal);
  Serial.print("  - LED tắt từ đầu: ");
  Serial.println(numLedsStart);
  Serial.print("  - Chế độ: ");
  Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
  Serial.print("  - Độ sáng: ");
  Serial.println(brightness);
}

void loadSettings() {
  preferences.begin("led-store", true);
  numLedsTotal = preferences.getUShort("numTotal", 50);  // Mặc định 50
  numLedsStart = preferences.getUShort("numStart", 0);   // Mặc định 0
  editingMode = preferences.getBool("editMode", false);  // Mặc định edit đuôi
  brightness = preferences.getUChar("brightness", 50);   // Mặc định 50
  preferences.end();

  Serial.println("✓ Đã đọc cài đặt từ NVS:");
  Serial.print("  - Tổng LED: ");
  Serial.println(numLedsTotal);
  Serial.print("  - LED tắt từ đầu: ");
  Serial.println(numLedsStart);
  Serial.print("  - Chế độ: ");
  Serial.println(editingMode ? "EDIT ĐẦU" : "EDIT ĐUÔI");
  Serial.print("  - Độ sáng: ");
  Serial.println(brightness);
}

// ===== HÀM CẬP NHẬT HIỂN THỊ LED =====
void updateLEDs() {
  // Tắt tất cả LED trước
  fill_solid(leds, MAX_LEDS, CRGB::Black);

  // Sáng các LED từ numLedsStart đến (numLedsTotal - 1)
  for (uint16_t i = numLedsStart; i < numLedsTotal && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Blue;
  }

  FastLED.show();

  // In ra trạng thái hiện tại
  Serial.print("LED: ");
  if (numLedsStart < numLedsTotal) {
    Serial.print(numLedsStart);
    Serial.print("-");
    Serial.print(numLedsTotal - 1);
    Serial.print(" (");
    Serial.print(numLedsTotal - numLedsStart);
    Serial.println(" LED sáng)");
  } else {
    Serial.println("TẤT CẢ TẮT");
  }
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
  Serial.println("BTN_2: Chuyển đổi chế độ");
  Serial.println("BTN_3: Giảm độ sáng (ấn: -5, giữ 2s: -10)");
  Serial.println("BTN_4: Tăng độ sáng (ấn: +5, giữ 2s: +10)");
  Serial.println("\nChế độ EDIT ĐUÔI:");
  Serial.println("  BTN_0: Giảm tổng số LED");
  Serial.println("  BTN_1: Tăng tổng số LED");
  Serial.println("\nChế độ EDIT ĐẦU:");
  Serial.println("  BTN_0: Tắt thêm LED từ đầu");
  Serial.println("  BTN_1: Bật thêm LED từ đầu\n");
}

void loop() {
  // Không cần làm gì trong loop
  // Tất cả xử lý được thực hiện trong callback OnDataRecv
  delay(10);
}
