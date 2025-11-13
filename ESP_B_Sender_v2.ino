/*
 * ESP32-C3 B - SENDER (Simple Button-Based)
 * Chỉ gửi SỐ NÚT được nhấn qua ESP-NOW
 * ESP A sẽ tự xử lý logic
 *
 * DÙNG TỐI ĐA 13 NÚT (GPIO 0-10, 20-21):
 * - GPIO 0:  Nút 0
 * - GPIO 1:  Nút 1
 * - GPIO 2:  Nút 2
 * - GPIO 3:  Nút 3
 * - GPIO 4:  Nút 4
 * - GPIO 5:  Nút 5
 * - GPIO 6:  Nút 6
 * - GPIO 7:  Nút 7
 * - GPIO 8:  Nút 8  (Strapping pin - OK nếu cẩn thận)
 * - GPIO 9:  Nút 9  (Strapping pin - OK nếu cẩn thận)
 * - GPIO 10: Nút 10
 * - GPIO 20: Nút 11
 * - GPIO 21: Nút 12
 *
 * ❌ GPIO 18, 19: USB JTAG (KHÔNG dùng vì cần debug)
 */

#include <esp_now.h>
#include <WiFi.h>

// ===== CẤU HÌNH CHÂN NÚT BẤM (13 nút) =====
#define BTN_0 0
#define BTN_1 1
#define BTN_2 2
#define BTN_3 3
#define BTN_4 4
#define BTN_5 5
#define BTN_6 6
#define BTN_7 7
#define BTN_8 8    // GPIO 8 (Strapping pin)
#define BTN_9 9    // GPIO 9 (Strapping pin)
#define BTN_10 10  // GPIO 10
#define BTN_11 20  // GPIO 20
#define BTN_12 21  // GPIO 21

// ===== ĐỊA CHỈ MAC CỦA ESP A (RECEIVER) =====
// MAC Address của ESP A: 20:6E:F1:6F:75:7C
uint8_t receiverMac[] = {0x20, 0x6E, 0xF1, 0x6F, 0x75, 0x7C};

// Cấu trúc dữ liệu gửi (chỉ chứa số nút)
typedef struct button_message {
  uint8_t button; // Số nút được nhấn (0-12)
} button_message;

button_message btnToSend;

// Debounce
unsigned long lastDebounceTime[13] = {0};  // 13 nút (0-12)
const unsigned long debounceDelay = 200; // 200ms

esp_now_peer_info_t peerInfo;

// Callback khi gửi dữ liệu (ESP32 Arduino Core v3.x)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("  → ESP-A received ✓");
  } else {
    Serial.println("  → ESP-A NOT received ❌");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);  // Chờ Serial Monitor sẵn sàng

  Serial.println("\n\n=== ESP-B SENDER STARTING ===");

  // Cấu hình các nút bấm
  pinMode(BTN_0, INPUT_PULLUP);
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);
  pinMode(BTN_4, INPUT_PULLUP);
  pinMode(BTN_5, INPUT_PULLUP);
  pinMode(BTN_6, INPUT_PULLUP);
  pinMode(BTN_7, INPUT_PULLUP);
  pinMode(BTN_8, INPUT_PULLUP);   // GPIO 8
  pinMode(BTN_9, INPUT_PULLUP);   // GPIO 9
  pinMode(BTN_10, INPUT_PULLUP);  // GPIO 10
  pinMode(BTN_11, INPUT_PULLUP);  // GPIO 20
  pinMode(BTN_12, INPUT_PULLUP);  // GPIO 21

  Serial.println("✓ 13 buttons configured");
  Serial.println("  GPIO: 0-10, 20-21");

  // Khởi tạo WiFi ở chế độ Station
  WiFi.mode(WIFI_STA);
  Serial.println("✓ WiFi initialized");

  // In ra địa chỉ MAC
  Serial.print("✓ ESP-B MAC: ");
  Serial.println(WiFi.macAddress());

  // Khởi tạo ESP-NOW
  Serial.print("Initializing ESP-NOW... ");
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ FAILED!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("✓ OK");

  // Đăng ký callback gửi dữ liệu
  esp_now_register_send_cb(OnDataSent);

  // Thêm peer (ESP A)
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  Serial.print("Adding peer ESP-A... ");
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ FAILED!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("✓ OK");

  Serial.print("✓ Target ESP-A: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", receiverMac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  Serial.println("\n=== READY! Press any button (0-12) ===\n");
}

void loop() {
  // Kiểm tra các nút bấm - gửi số nút đến ESP A
  checkButton(BTN_0, 0);
  checkButton(BTN_1, 1);
  checkButton(BTN_2, 2);
  checkButton(BTN_3, 3);
  checkButton(BTN_4, 4);
  checkButton(BTN_5, 5);
  checkButton(BTN_6, 6);
  checkButton(BTN_7, 7);
  checkButton(BTN_8, 8);
  checkButton(BTN_9, 9);
  checkButton(BTN_10, 10);
  checkButton(BTN_11, 11);
  checkButton(BTN_12, 12);

  delay(10); // Delay nhỏ để giảm tải CPU
}

// ===== HÀM KIỂM TRA NÚT BẤM =====
void checkButton(int pin, uint8_t buttonNum) {
  if (digitalRead(pin) == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - lastDebounceTime[buttonNum] > debounceDelay) {
      lastDebounceTime[buttonNum] = currentTime;

      // In ra nút đang được nhấn
      Serial.print("\n[BTN ");
      Serial.print(buttonNum);
      Serial.print("] Pin ");
      Serial.print(pin);
      Serial.print(" pressed -> ");

      sendButton(buttonNum);
    }
  }
}

// ===== HÀM GỬI SỐ NÚT =====
void sendButton(uint8_t buttonNum) {
  btnToSend.button = buttonNum;

  esp_err_t result = esp_now_send(receiverMac, (uint8_t *) &btnToSend, sizeof(btnToSend));

  if (result == ESP_OK) {
    Serial.print("Sent button ");
    Serial.println(buttonNum);
  } else {
    Serial.print("❌ ERROR: Failed to send button ");
    Serial.println(buttonNum);
  }
}
