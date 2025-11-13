/*
 * ESP32-C3 A - RECEIVER (Simplified)
 * Nhận tín hiệu từ ESP B để tăng/giảm số LED
 * - BTN_0: Giảm LED (từng LED một)
 * - BTN_1: Tăng LED (từng LED một) - Tối đa 300 LED
 * - Lưu số LED vào bộ nhớ NVS (giữ khi mất điện)
 * - LED GPIO 8: Sáng 100ms mỗi khi nhận tín hiệu
 * - Màu: Blue với độ sáng 50
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
uint16_t numLeds = 10;         // Số LED hiện tại (mặc định 10)
bool needsUpdate = false;      // Cờ cần cập nhật LED

// ===== CẤU TRÚC DỮ LIỆU NHẬN =====
// Nhận từ ESP B (button number: 0-12)
typedef struct button_message {
  uint8_t button;              // Số nút được nhấn
} button_message;

button_message receivedBtn;

// ===== HÀM LƯU/ĐỌC SỐ LED TỪ NVS =====
void saveNumLeds() {
  preferences.begin("led-store", false);
  preferences.putUShort("numLeds", numLeds);
  preferences.end();
  Serial.print("✓ Đã lưu số LED vào NVS: ");
  Serial.println(numLeds);
}

void loadNumLeds() {
  preferences.begin("led-store", true);
  numLeds = preferences.getUShort("numLeds", 10); // Mặc định 10 nếu chưa có
  preferences.end();
  Serial.print("✓ Đã đọc số LED từ NVS: ");
  Serial.println(numLeds);
}

// ===== CALLBACK KHI NHẬN DỮ LIỆU =====
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receivedBtn, incomingData, sizeof(receivedBtn));

  // Bật LED báo hiệu GPIO 8 trong 100ms
  digitalWrite(STATUS_LED_PIN, HIGH);

  Serial.print("\n[Nhận tín hiệu] Nút: ");
  Serial.println(receivedBtn.button);

  // Xử lý lệnh
  if (receivedBtn.button == 0) {
    // BTN_0: Giảm LED
    if (numLeds > 0) {
      Serial.print("Giảm LED: ");
      Serial.print(numLeds);
      Serial.print(" → ");

      // Tắt LED cuối cùng (animation)
      leds[numLeds - 1] = CRGB::Black;
      FastLED.show();

      numLeds--;
      Serial.println(numLeds);

      saveNumLeds();
      needsUpdate = true;
    } else {
      Serial.println("Đã ở mức tối thiểu (0 LED)");
    }
  }
  else if (receivedBtn.button == 1) {
    // BTN_1: Tăng LED
    if (numLeds < MAX_LEDS) {
      Serial.print("Tăng LED: ");
      Serial.print(numLeds);
      Serial.print(" → ");

      numLeds++;

      // Sáng LED mới (animation)
      leds[numLeds - 1] = CRGB::Blue;
      FastLED.show();

      Serial.println(numLeds);

      saveNumLeds();
      needsUpdate = true;
    } else {
      Serial.println("Đã đạt tối đa (300 LED)");
    }
  }
  else {
    Serial.println("Nút không xử lý (chỉ hỗ trợ BTN_0 và BTN_1)");
  }

  // Tắt LED báo hiệu sau 100ms
  delay(100);
  digitalWrite(STATUS_LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n=== ESP-A RECEIVER (Simplified) ===");

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
  FastLED.setBrightness(50); // Độ sáng 50
  Serial.println("✓ FastLED đã khởi tạo (Độ sáng: 50)");

  // Đọc số LED từ bộ nhớ
  loadNumLeds();

  // Hiển thị LED ban đầu (màu Blue)
  for (int i = 0; i < numLeds && i < MAX_LEDS; i++) {
    leds[i] = CRGB::Blue;
  }
  // Tắt các LED còn lại
  for (int i = numLeds; i < MAX_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();

  Serial.println("✓ LED đã sẵn sàng");
  Serial.println("\n=== SẴN SÀNG NHẬN TÍN HIỆU ===");
  Serial.println("BTN_0: Giảm LED");
  Serial.println("BTN_1: Tăng LED\n");
}

void loop() {
  // Không cần làm gì trong loop
  // Tất cả xử lý được thực hiện trong callback OnDataRecv
  delay(10);
}
