/*************************************************
 * ESP32-CAM QR + BLUETOOTH PRINTER
 * INTERMITTENT DUTY SOLENOID VERSION
 *
 * FLOW:
 * BOOT
 * -> LED_BT OFF
 * -> QR VALID
 * -> SOLENOID ON 5 DETIK (unlock)
 * -> SOLENOID OFF
 * -> USER BUKA PINTU
 * -> USER TUTUP PINTU
 * -> SOLENOID ON 5 DETIK (lock)
 * -> SOLENOID OFF
 * -> VERIFY LOCK
 * -> BLUETOOTH CONNECT
 * -> LED_BT ON            (tetap ON sampai bluetooth disconnect)
 * -> BEEP BLUETOOTH
 * -> PRINT STRUK
 * -> BLUETOOTH DISCONNECT
 * -> LED_BT OFF
 * -> KEMBALI WAIT_QR
 *
 * ============ CARA PAKAI DI ARDUINO IDE ============
 * 1. Board Manager  -> install "esp32 by Espressif Systems"
 * 2. Tools > Board   -> pilih board ESP32 yang kamu pakai
 *    (mis. "AI Thinker ESP32-CAM" atau ESP32 Dev Module)
 * 3. Library Manager -> install "Adafruit Thermal Printer Library"
 *    (BluetoothSerial sudah include otomatis dari core ESP32,
 *     tidak perlu install manual)
 * 4. Pastikan folder ini bernama sama dengan file .ino
 *    (ESP32_QR_Solenoid_Printer/ESP32_QR_Solenoid_Printer.ino)
 * 5. Sesuaikan MAC address printer di variabel printerMAC[]
 * 6. Upload seperti biasa
 *
 * CATATAN: BluetoothSerial (Bluetooth Classic/SPP) hanya jalan
 * di chip ESP32 klasik (mis. ESP32-CAM, ESP32-WROOM). Tidak
 * didukung di ESP32-S3/C3/C6 karena chip itu cuma punya BLE.
 *****************************************************/

#include <Arduino.h>
#include <BluetoothSerial.h>
#include "Adafruit_Thermal.h"

// Header FreeRTOS & driver GPIO -> wajib di-include eksplisit
// supaya QueueHandle_t, xTaskCreatePinnedToCore, dan gpio_reset_pin
// dikenali oleh compiler Arduino IDE.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth Classic tidak aktif. Pilih board ESP32 klasik (bukan S3/C3/C6) dan pastikan Bluetooth di-enable di menu Tools.
#endif

/* ================= PIN ================= */
#define SOLENOID_PIN 18
#define BUZZER_PIN   5
#define LIMIT_PIN    19
#define LED_BT       21
#define GM66_RX      16
#define GM66_TX      17

/* ================= CONFIG ================= */
#define QR_LEN       32
#define MAX_SCAN     50
#define SCAN_DELAY   2000
#define BT_MAX_RETRY 5

/* ================= SOLENOID ================= */
#define SOLENOID_PULSE_TIME 5000
#define LOCK_VERIFY_TIME    1000

/* ================= DISCOUNT ================= */
#define DISC_MIN  1000
#define DISC_MAX  100000
#define DISC_STEP 500

/* ================= PRINTER ================= */
#define QR_SIZE      12
#define QR_ECC       0x30
#define FEED_TOP     1
#define FEED_BOTTOM  4

/* ================= RTOS ================= */
QueueHandle_t qrQueue;

/* ================= BLUETOOTH ================= */
BluetoothSerial SerialBT;
Adafruit_Thermal printer(&SerialBT);
uint8_t printerMAC[6] = { 0x06, 0x05, 0x63, 0x09, 0x23, 0x9C };

/* ================= GM66 ================= */
HardwareSerial QRSerial(2);

/* ================= STATE ================= */
enum SystemState {
  WAIT_QR,
  UNLOCK_PULSE,
  WAIT_DOOR_OPEN,
  WAIT_DOOR_CLOSE,
  LOCK_PULSE,
  VERIFY_LOCK,
  PRINTING
};
SystemState state = WAIT_QR;

/* ================= PRODUCT ================= */
struct Product {
  char qr[12];
  char name[20];
  char type[10];
  uint16_t size;
  uint32_t cashbackk;
};

#define DB_SIZE 10
const Product productDB[DB_SIZE] = {
  {"PE500XYSH", "LIFEBUOY",   "PET",  500,  5000},
  {"HD500AB12", "SUNLIGHT",   "HDPE", 500,  5000},
  {"PE250K9QW", "AQUA",       "PET",  250,  2500},
  {"HD250LM88", "SOKLIN",     "HDPE", 250,  2500},
  {"PE100P0Z1", "NUTRISARI",  "PET",  100,  1000},
  {"HD100TT77", "RINSO",      "HDPE", 100,  1000},
  {"PE1L0QQQ9", "TEH BOTOL",  "PET",  1000, 10000},
  {"HD1L0W2E3", "MIZONE",     "HDPE", 1000, 10000},
  {"PE2L5MNBV", "AIR MINUM",  "PET",  2500, 25000},
  {"HD2L5R4T5", "SABUN",      "HDPE", 2500, 25000}
};

/* ================= SNAPSHOT ================= */
struct PrintSnapshot {
  Product items[MAX_SCAN];
  uint16_t count;
  char discCode[20];
  uint32_t total;
};
PrintSnapshot printSnap;

/* ================= STORAGE ================= */
char scanList[MAX_SCAN][QR_LEN];
Product scanProduct[MAX_SCAN];
uint16_t scanCount = 0;

/* ================= TIMER ================= */
unsigned long lastScanTime = 0;
unsigned long pulseStart   = 0;
unsigned long verifyTime   = 0;

/* ================= DOOR ================= */
bool doorOpened = false;

/* ================= BUZZER ================= */
bool buzzerActive = false;
bool buzzerState  = false;
unsigned long buzzerStart   = 0;
unsigned long buzzerOnTime  = 0;
unsigned long buzzerOffTime = 0;
int buzzerRepeat = 0;
int buzzerCount  = 0;

/* ================= BUZZER ENGINE ================= */
void buzzerStartPattern(int onTime, int offTime, int repeat) {
  buzzerOnTime  = onTime;
  buzzerOffTime = offTime;
  buzzerRepeat  = repeat;
  buzzerCount   = 0;
  buzzerState   = true;
  buzzerActive  = true;
  buzzerStart   = millis();
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerUpdate() {
  if (!buzzerActive) return;
  unsigned long now = millis();

  if (buzzerState) {
    if (now - buzzerStart >= (unsigned long)buzzerOnTime) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerState = false;
      buzzerStart = now;
    }
  } else {
    if (now - buzzerStart >= (unsigned long)buzzerOffTime) {
      buzzerCount++;
      if (buzzerRepeat > 0 && buzzerCount >= buzzerRepeat) {
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, HIGH);
        return;
      }
      digitalWrite(BUZZER_PIN, LOW);
      buzzerState = true;
      buzzerStart = now;
    }
  }
}

/* ================= BUZZER MODE ================= */
void beepON()        { buzzerStartPattern(2000, 0, 1); }
void beepOK()        { buzzerStartPattern(300, 0, 1); }
void beepBT()        { buzzerStartPattern(1000, 1000, 2); }
void beepBTConnect() { buzzerStartPattern(200, 150, 2); }  // beep saat bluetooth berhasil connect
void beepError()     { buzzerStartPattern(300, 300, 5); }
void beepDone()      { buzzerStartPattern(2000, 0, 1); }

/* ================= LED BLUETOOTH ================= */
void ledBTOn()  { digitalWrite(LED_BT, HIGH); }
void ledBTOff() { digitalWrite(LED_BT, LOW); }

/* ================= SOLENOID ================= */
void unlockDoor() { digitalWrite(SOLENOID_PIN, LOW); }
void lockDoor()   { digitalWrite(SOLENOID_PIN, HIGH); }

/* ================= LIMIT SWITCH ================= */
bool doorClosedStable() {
  static unsigned long t = 0;
  if (digitalRead(LIMIT_PIN) == LOW) {
    if (millis() - t > 300) {
      return true;
    }
  } else {
    t = millis();
  }
  return false;
}

/* ================= UTIL ================= */
void cleanQR(char *qr) {
  for (int i = 0; qr[i]; i++) {
    if (qr[i] < 32 || qr[i] > 126) {
      qr[i] = 0;
      break;
    }
  }
}

bool findProduct(const char *qr, Product *out) {
  for (int i = 0; i < DB_SIZE; i++) {
    if (strcmp(productDB[i].qr, qr) == 0) {
      *out = productDB[i];
      return true;
    }
  }
  return false;
}

/* ================= DISCOUNT ================= */
void generateDiscCode(uint32_t total, char *discCode) {
  if (total < DISC_MIN) total = DISC_MIN;
  if (total > DISC_MAX) total = DISC_MAX;

  total = (total / DISC_STEP) * DISC_STEP;

  uint32_t kilo = total / 1000;
  uint32_t rem  = total % 1000;

  if (rem == 0) sprintf(discCode, "DISC%luK", (unsigned long)kilo);
  else          sprintf(discCode, "DISC%lu.5K", (unsigned long)kilo);
}

/* ================= PRINTER INIT ================= */
void printerInit() {
  SerialBT.write(0x1B);
  SerialBT.write(0x40);
  delay(300);
  while (SerialBT.available()) SerialBT.read();
  printer.feed(FEED_TOP);
}

/* ================= PRINT QR ================= */
void printQR_Config(const char *data) {
  if (!data || strlen(data) == 0) return;
  int len = strlen(data);

  SerialBT.write(0x1B);
  SerialBT.write(0x61);
  SerialBT.write(0x01);
  delay(50);

  /* MODEL */
  SerialBT.write(0x1D);
  SerialBT.write(0x28);
  SerialBT.write(0x6B);
  SerialBT.write(0x04);
  SerialBT.write(0x00);
  SerialBT.write(0x31);
  SerialBT.write(0x41);
  SerialBT.write(0x32);
  SerialBT.write(0x00);
  delay(50);

  /* SIZE */
  SerialBT.write(0x1D);
  SerialBT.write(0x28);
  SerialBT.write(0x6B);
  SerialBT.write(0x03);
  SerialBT.write(0x00);
  SerialBT.write(0x31);
  SerialBT.write(0x43);
  SerialBT.write(QR_SIZE);
  delay(50);

  /* ECC */
  SerialBT.write(0x1D);
  SerialBT.write(0x28);
  SerialBT.write(0x6B);
  SerialBT.write(0x03);
  SerialBT.write(0x00);
  SerialBT.write(0x31);
  SerialBT.write(0x45);
  SerialBT.write(QR_ECC);
  delay(50);

  /* STORE DATA */
  int storeLen = len + 3;
  SerialBT.write(0x1D);
  SerialBT.write(0x28);
  SerialBT.write(0x6B);
  SerialBT.write(storeLen & 0xFF);
  SerialBT.write((storeLen >> 8) & 0xFF);
  SerialBT.write(0x31);
  SerialBT.write(0x50);
  SerialBT.write(0x30);
  for (int i = 0; i < len; i++) {
    SerialBT.write((uint8_t)data[i]);
  }
  SerialBT.flush();
  delay(300);

  /* PRINT */
  SerialBT.write(0x1D);
  SerialBT.write(0x28);
  SerialBT.write(0x6B);
  SerialBT.write(0x03);
  SerialBT.write(0x00);
  SerialBT.write(0x31);
  SerialBT.write(0x51);
  SerialBT.write(0x30);
  SerialBT.flush();
  delay(600);

  printer.feed(1);
}

/* ================= PRINT ================= */
void printAll(const PrintSnapshot &snap) {
  if (!SerialBT.connected()) {
    beepBT();
    return;
  }

  printerInit();

  printer.println("===== TOKO ABC =====");
  printer.println("   STRUK CASHBACK   ");
  printer.feed(1);

  SerialBT.write(0x1B);
  SerialBT.write(0x61);
  SerialBT.write(0x00);

  for (int i = 0; i < snap.count; i++) {
    const Product *p = &snap.items[i];
    printer.println(p->name);
    printer.printf("Type : %s\n", p->type);
    printer.printf("Size : %d ml\n", p->size);
    printer.printf("Cashback : Rp %lu\n", (unsigned long)p->cashbackk);
    printer.println("---------------------");
    printer.feed(1);
  }

  SerialBT.write(0x1B);
  SerialBT.write(0x61);
  SerialBT.write(0x01);

  printer.println("TOTAL CASHBACK");
  printer.printf("Rp %lu\n", (unsigned long)snap.total);
  printer.feed(1);

  printer.println("SCAN DISKON");
  printer.feed(1);
  printQR_Config(snap.discCode);
  printer.println(snap.discCode);
  printer.feed(1);

  printer.println("Terima Kasih");
  printer.feed(FEED_BOTTOM);

  SerialBT.flush();
  delay(500);
  beepDone();
}

/* ================= BLUETOOTH ================= */
bool connectBluetooth() {
  SerialBT.end();
  delay(500);

  SerialBT.setPin("0000", 4);
  SerialBT.begin("ESP32", true);
  delay(2000);

  for (int i = 0; i < BT_MAX_RETRY; i++) {
    Serial.printf("[BT] Attempt %d\n", i + 1);
    SerialBT.connect(printerMAC);

    int timeout = 80;
    while (!SerialBT.connected() && timeout--) {
      delay(100);
    }

    if (SerialBT.connected()) {
      Serial.println("[BT] Connected");
      delay(500);
      return true;
    }

    SerialBT.disconnect();
    delay(1500);
  }

  SerialBT.end();
  return false;
}

/* ================= TASK QR ================= */
void TaskQR(void *pv) {
  char buffer[QR_LEN];
  uint8_t idx = 0;
  memset(buffer, 0, sizeof(buffer));

  while (1) {
    if (state != WAIT_QR && state != WAIT_DOOR_OPEN && state != WAIT_DOOR_CLOSE) {
      while (QRSerial.available()) QRSerial.read();
      memset(buffer, 0, sizeof(buffer));
      idx = 0;
      vTaskDelay(50 / portTICK_PERIOD_MS);
      continue;
    }

    while (QRSerial.available()) {
      char c = QRSerial.read();
      if (c == '\n' || c == '\r') {
        if (idx > 0) {
          buffer[idx] = 0;
          cleanQR(buffer);
          if (millis() - lastScanTime >= SCAN_DELAY) {
            lastScanTime = millis();
            xQueueSend(qrQueue, buffer, 0);
            Serial.printf("[SCAN] %s\n", buffer);
          }
          memset(buffer, 0, sizeof(buffer));
          idx = 0;
        }
      } else {
        if (idx < QR_LEN - 1) buffer[idx++] = c;
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/* ================= TASK SYSTEM ================= */
void TaskSystem(void *pv) {
  char qr[QR_LEN];

  while (1) {
    buzzerUpdate();

    if (xQueueReceive(qrQueue, qr, 10)) {
      if (scanCount >= MAX_SCAN) continue;

      Product temp;
      if (!findProduct(qr, &temp)) {
        beepError();
        continue;
      }

      strcpy(scanList[scanCount], qr);
      scanProduct[scanCount] = temp;
      scanCount++;
      beepOK();

      if (state == WAIT_QR) {
        unlockDoor();
        pulseStart = millis();
        doorOpened = false;
        state = UNLOCK_PULSE;
      }
    }

    switch (state) {
      case UNLOCK_PULSE:
        if (millis() - pulseStart >= SOLENOID_PULSE_TIME) {
          lockDoor();
          Serial.println("[LOCK] Unlock pulse selesai");
          state = WAIT_DOOR_OPEN;
        }
        break;

      case WAIT_DOOR_OPEN:
        if (digitalRead(LIMIT_PIN) == HIGH) {
          Serial.println("[DOOR] OPEN");
          state = WAIT_DOOR_CLOSE;
        }
        break;

      case WAIT_DOOR_CLOSE:
        if (doorClosedStable()) {
          Serial.println("[DOOR] CLOSED");
          unlockDoor();
          pulseStart = millis();
          state = LOCK_PULSE;
        }
        break;

      case LOCK_PULSE:
        if (millis() - pulseStart >= SOLENOID_PULSE_TIME) {
          lockDoor();
          verifyTime = millis();
          Serial.println("[LOCK] Lock pulse selesai");
          state = VERIFY_LOCK;
        }
        break;

      case VERIFY_LOCK:
        if (digitalRead(LIMIT_PIN) == LOW) {
          if (millis() - verifyTime >= LOCK_VERIFY_TIME) {
            Serial.println("[LOCK] VERIFIED");
            state = PRINTING;
          }
        } else {
          Serial.println("[LOCK] NOT STABLE");
          state = WAIT_DOOR_CLOSE;
        }
        break;

      case PRINTING: {
        memset(&printSnap, 0, sizeof(printSnap));
        printSnap.count = scanCount;
        printSnap.total = 0;

        for (int i = 0; i < scanCount; i++) {
          printSnap.items[i] = scanProduct[i];
          printSnap.total += scanProduct[i].cashbackk;
        }

        generateDiscCode(printSnap.total, printSnap.discCode);

        memset(scanList, 0, sizeof(scanList));
        memset(scanProduct, 0, sizeof(scanProduct));
        scanCount = 0;

        if (connectBluetooth()) {
          ledBTOn();          // LED_BT ON begitu bluetooth connect
          beepBTConnect();    // beep tanda bluetooth tersambung
          printAll(printSnap);
          delay(500);
          SerialBT.disconnect();
          delay(300);
          SerialBT.end();
          delay(200);
          ledBTOff();         // LED_BT OFF setelah bluetooth disconnect
        } else {
          beepError();
        }

        state = WAIT_QR;
        break;
      }

      default:
        break;
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  gpio_reset_pin((gpio_num_t)15);
  gpio_reset_pin((gpio_num_t)18);
  gpio_reset_pin((gpio_num_t)19);

  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BT, OUTPUT);
  digitalWrite(SOLENOID_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_BT, LOW);   // LED_BT OFF saat BOOT
  delay(1500);

  pinMode(LIMIT_PIN, INPUT_PULLUP);

  lockDoor();
  delay(200);
  beepON();
  delay(200);

  QRSerial.begin(9600, SERIAL_8N1, GM66_RX, GM66_TX);

  qrQueue = xQueueCreate(10, QR_LEN);

  xTaskCreatePinnedToCore(TaskQR, "QR", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskSystem, "SYS", 8192, NULL, 2, NULL, 1);

  Serial.println("[BOOT] READY");
}

/* ================= LOOP ================= */
void loop() {
  // Semua logika berjalan di TaskQR & TaskSystem (FreeRTOS task),
  // loop() sengaja dikosongkan.
}
