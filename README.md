# ESP32 GM66 QR Cashback Door & Bluetooth Printer

An ESP32-based automated bottle return system that combines **QR code scanning, door/solenoid control, limit switch monitoring, cashback calculation, and Bluetooth thermal receipt printing**.

The system uses a **GM66 QR scanner** to identify registered bottle QR codes. After a valid QR code is detected, the door is unlocked using a solenoid actuator. The system then monitors the door using a limit switch, locks the door after the user finishes inserting bottles, calculates the total cashback, and prints a cashback receipt through a Bluetooth thermal printer.

---

## Features

* QR code scanning using **GM66 UART QR Scanner**
* Product lookup using an embedded product database
* Automatic door unlocking using a 12 V solenoid
* Door open/close detection using a limit switch
* Automatic door locking after the door is closed
* Lock verification before printing
* Multiple QR scans during one transaction
* Cashback calculation based on registered bottle data
* Automatic discount code generation
* Bluetooth Classic thermal printer support
* Bluetooth connection status LED
* Buzzer notifications for system events
* FreeRTOS-based multitasking
* Non-blocking buzzer pattern engine
* Automatic transaction reset after printing

---

## System Workflow

```text
                    ┌───────────────┐
                    │     BOOT      │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │   WAIT QR     │
                    └───────┬───────┘
                            │
                       Valid QR
                            │
                            ▼
                    ┌───────────────┐
                    │ Unlock Door    │
                    │ 5 Seconds      │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ Wait Door Open │
                    └───────┬───────┘
                            │
                       Door Open
                            │
                            ▼
                    ┌───────────────┐
                    │ Wait Door Close│
                    │ Scan QR Codes  │
                    └───────┬───────┘
                            │
                       Door Closed
                            │
                            ▼
                    ┌───────────────┐
                    │  Lock Door     │
                    │ 5 Seconds      │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ Verify Lock    │
                    └───────┬───────┘
                            │
                       Lock OK
                            │
                            ▼
                    ┌───────────────┐
                    │ Bluetooth      │
                    │ Printer Connect │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ Print Receipt  │
                    │ + QR Discount  │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ Bluetooth      │
                    │ Disconnect     │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │    WAIT QR     │
                    └───────────────┘
```

---

## Hardware

### Main Controller

* ESP32 Classic

  * ESP32-CAM
  * ESP32-WROOM
  * ESP32 DevKit / compatible ESP32 Classic board

### Peripherals

| Component       | Function                        |
| --------------- | ------------------------------- |
| GM66            | QR code scanner                 |
| Solenoid        | Door locking/unlocking actuator |
| Limit Switch    | Door position detection         |
| Thermal Printer | Cashback receipt printer        |
| Bluetooth       | Wireless printer communication  |
| Buzzer          | System status notification      |
| LED             | Bluetooth connection indicator  |

---

## Pin Configuration

| ESP32 GPIO | Device       | Function             |
| ---------: | ------------ | -------------------- |
|    GPIO 18 | Solenoid     | Door actuator        |
|     GPIO 5 | Buzzer       | Audible notification |
|    GPIO 19 | Limit Switch | Door status          |
|    GPIO 21 | LED          | Bluetooth status     |
|    GPIO 16 | GM66 RX      | QR scanner UART RX   |
|    GPIO 17 | GM66 TX      | QR scanner UART TX   |

### Logic

#### Solenoid

```text
LOW  = Solenoid ON
HIGH = Solenoid OFF
```

The solenoid is activated for:

```text
5 seconds
```

for both the unlock and lock pulse.

#### Limit Switch

```text
LOW  = Door closed
HIGH = Door open
```

The closed condition is considered stable after approximately:

```text
300 ms
```

of continuous LOW input.

---

## Software Architecture

The firmware uses **FreeRTOS tasks** to separate QR scanning from the main system control.

### TaskQR

Responsible for:

* Reading QR data from the GM66
* Processing incoming UART characters
* Detecting end-of-scan characters
* Cleaning invalid characters
* Applying scan delay protection
* Sending QR data to the system queue

### TaskSystem

Responsible for:

* Processing QR scan results
* Product database lookup
* Door state machine
* Solenoid control
* Limit switch monitoring
* Lock verification
* Cashback calculation
* Bluetooth printer connection
* Receipt printing
* Transaction reset

### Main State Machine

The system uses the following states:

```cpp
WAIT_QR
UNLOCK_PULSE
WAIT_DOOR_OPEN
WAIT_DOOR_CLOSE
LOCK_PULSE
VERIFY_LOCK
PRINTING
```

---

## QR Transaction Handling

The system supports up to:

```text
50 QR codes
```

in a single transaction.

The QR scanner has a minimum scan interval of:

```text
2 seconds
```

configured by:

```cpp
#define SCAN_DELAY 2000
```

A scanned QR code must exist in the product database to be accepted.

Invalid or unregistered QR codes trigger the error buzzer and are not added to the transaction.

---

## Product Database

The current firmware contains a small demonstration database:

```cpp
const Product productDB[DB_SIZE]
```

Each product contains:

```text
QR Code
Product Name
Material Type
Bottle Size
Cashback Value
```

Example:

| QR        | Product   | Material |    Size | Cashback |
| --------- | --------- | -------- | ------: | -------: |
| PE500XYSH | LIFEBUOY  | PET      |  500 ml |  Rp5,000 |
| HD500AB12 | SUNLIGHT  | HDPE     |  500 ml |  Rp5,000 |
| PE250K9QW | AQUA      | PET      |  250 ml |  Rp2,500 |
| PE100P0Z1 | NUTRISARI | PET      |  100 ml |  Rp1,000 |
| PE1L0QQQ9 | TEH BOTOL | PET      | 1000 ml | Rp10,000 |

The database can be expanded or replaced with an external database implementation for larger deployments.

---

## Cashback Calculation

The total cashback is calculated from all valid QR codes collected during the transaction.

For example:

```text
Bottle 1 = Rp5,000
Bottle 2 = Rp2,500
Bottle 3 = Rp2,500
--------------------
Total    = Rp10,000
```

The total is stored in:

```cpp
printSnap.total
```

---

## Discount Code Generation

The firmware automatically generates a discount code from the total cashback.

Current configuration:

```cpp
#define DISC_MIN  1000
#define DISC_MAX  100000
#define DISC_STEP 500
```

Example:

```text
Rp1,000  → DISC1K
Rp2,500  → DISC2.5K
Rp5,000  → DISC5K
Rp10,000 → DISC10K
```

The generated code is printed as a QR code on the thermal receipt.

---

## Bluetooth Thermal Printer

The system uses:

```cpp
BluetoothSerial
```

for Bluetooth Classic communication and:

```cpp
Adafruit_Thermal
```

for thermal printer control.

The printer MAC address is configured using:

```cpp
uint8_t printerMAC[6] = {
  0x06, 0x05, 0x63, 0x09, 0x23, 0x9C
};
```

Replace this address with the MAC address of your own printer before deployment.

### Bluetooth Connection Sequence

```text
Bluetooth Start
      ↓
Connect to Printer
      ↓
Connection Successful
      ↓
LED ON
      ↓
Bluetooth Connected Beep
      ↓
Print Receipt
      ↓
Disconnect
      ↓
LED OFF
```

The system attempts to connect up to:

```cpp
#define BT_MAX_RETRY 5
```

times.

---

## Thermal Receipt

The receipt contains:

```text
===== TOKO ABC =====
   STRUK CASHBACK

Product Name
Type
Size
Cashback

---------------------

TOTAL CASHBACK
Rp XXXXX

SCAN DISKON

[Discount QR]

DISCXXK

Terima Kasih
```

The store name can be changed inside:

```cpp
printAll()
```

For example:

```cpp
printer.println("===== TOKO ABC =====");
```

---

## Buzzer Notifications

The buzzer uses a non-blocking pattern engine.

Available patterns include:

| Function          | Purpose                          |
| ----------------- | -------------------------------- |
| `beepON()`        | System startup                   |
| `beepOK()`        | Valid QR detected                |
| `beepBT()`        | Bluetooth error/status           |
| `beepBTConnect()` | Bluetooth successfully connected |
| `beepError()`     | Invalid QR / connection error    |
| `beepDone()`      | Printing completed               |

The buzzer engine is controlled through:

```cpp
buzzerStartPattern()
buzzerUpdate()
```

allowing the system to continue processing other tasks while the buzzer pattern is running.

---

## Installation

### 1. Install Arduino IDE

Install the latest compatible version of Arduino IDE.

### 2. Install ESP32 Board Package

Open:

```text
Tools
→ Board
→ Boards Manager
```

Search for:

```text
esp32
```

Install:

```text
esp32 by Espressif Systems
```

### 3. Select the Board

Choose the appropriate ESP32 board under:

```text
Tools → Board
```

Examples:

```text
AI Thinker ESP32-CAM
```

or:

```text
ESP32 Dev Module
```

depending on the hardware being used.

### 4. Install Thermal Printer Library

Open:

```text
Sketch
→ Include Library
→ Manage Libraries
```

Search for:

```text
Adafruit Thermal Printer Library
```

and install it.

`BluetoothSerial` is included with the ESP32 Arduino Core and does not require a separate library installation.

### 5. Open the Project

Make sure the project folder and `.ino` file have the same name.

Example:

```text
ESP32_QR_Solenoid_Printer/
└── ESP32_QR_Solenoid_Printer.ino
```

### 6. Configure Printer MAC Address

Modify:

```cpp
uint8_t printerMAC[6] = {
  0x06, 0x05, 0x63, 0x09, 0x23, 0x9C
};
```

with the MAC address of the Bluetooth thermal printer.

### 7. Upload

Connect the ESP32 to the computer and upload the firmware through Arduino IDE.

---

## Important Bluetooth Compatibility

This project uses **Bluetooth Classic / SPP** through `BluetoothSerial`.

Therefore, the firmware requires an ESP32 chip that supports Bluetooth Classic.

### Compatible examples

* ESP32-WROOM-32
* ESP32-WROVER
* ESP32-CAM based on the original ESP32
* ESP32 DevKit using the original ESP32

### Not compatible with BluetoothSerial Classic

The following ESP32 families do not provide Bluetooth Classic SPP:

* ESP32-C3
* ESP32-S3
* ESP32-C6

These chips use BLE rather than the Bluetooth Classic interface required by this implementation.

---

## Configuration

The most important parameters can be adjusted at the beginning of the source code.

### QR Scanner

```cpp
#define QR_LEN       32
#define MAX_SCAN     50
#define SCAN_DELAY   2000
```

### Solenoid

```cpp
#define SOLENOID_PULSE_TIME 5000
#define LOCK_VERIFY_TIME    1000
```

### Bluetooth

```cpp
#define BT_MAX_RETRY 5
```

### Cashback

```cpp
#define DISC_MIN  1000
#define DISC_MAX  100000
#define DISC_STEP 500
```

### Printer QR

```cpp
#define QR_SIZE 12
#define QR_ECC  0x30
```

---

## Serial Monitor

The firmware outputs system information through:

```text
115200 baud
```

Example messages:

```text
[BOOT] READY
[SCAN] PE500XYSH
[LOCK] Unlock pulse selesai
[DOOR] OPEN
[DOOR] CLOSED
[LOCK] Lock pulse selesai
[LOCK] VERIFIED
[BT] Attempt 1
[BT] Connected
```

These messages can be used for debugging and monitoring the system during development.

---

## Transaction Example

A typical transaction works as follows:

### 1. System Ready

```text
WAIT_QR
```

The machine waits for a valid QR code.

### 2. First QR Scan

The GM66 reads a registered bottle QR code.

The system:

* validates the QR code
* adds the bottle to the transaction
* activates the solenoid
* unlocks the door

### 3. Door Opens

The limit switch changes to:

```text
HIGH
```

The system recognizes that the door has been opened.

### 4. Bottle Insertion

Additional registered QR codes can be scanned while the transaction is active.

Each valid bottle is added to the transaction.

### 5. Door Closes

The limit switch returns to:

```text
LOW
```

The system waits for approximately 300 ms of stable closed status.

### 6. Door Locking

The solenoid is activated for:

```text
5 seconds
```

and the door is locked.

### 7. Lock Verification

The system verifies that the limit switch remains LOW for:

```text
1 second
```

before continuing.

### 8. Receipt Printing

The ESP32 connects to the Bluetooth thermal printer and prints:

* Bottle information
* Cashback per bottle
* Total cashback
* Discount QR code
* Discount code
* Thank-you message

### 9. Transaction Reset

After printing and Bluetooth disconnection, the system returns to:

```text
WAIT_QR
```

and is ready for the next transaction.

---

## Project Structure

Recommended repository structure:

```text
ESP32_QR_Solenoid_Printer/
│
├── src/
│   └── ESP32_QR_Solenoid_Printer_1.ino
├── README.md
└── LICENSE
```

Additional files can be added later if the project is separated into modules:

```text
ESP32_QR_Solenoid_Printer/
│
├── src/
│   └── database/
│   └── docs/
│   └── ESP32_QR_Solenoid_Printer_1.ino
│   └── include/
├── README.md
└── LICENSE
```

---

## Development Notes

This firmware is intended as a control-system prototype/reference implementation for an automated QR-based bottle collection and cashback system.

The current product database is stored directly in program memory:

```cpp
const Product productDB[DB_SIZE]
```

For a larger deployment, the database can be migrated to an external storage solution such as:

* MicroSD
* Binary database
* Flash storage
* NVS
* External server/database

This can allow the system to handle a significantly larger number of QR records without requiring the product data to be hard-coded into the firmware.

---

## Safety Considerations

The solenoid actuator can draw significant current. Use an appropriate:

* MOSFET or relay driver
* Flyback protection
* Power supply
* Fuse/protection
* Common-ground arrangement where required

Do not drive a high-current solenoid directly from an ESP32 GPIO.

The ESP32 GPIO should only control the appropriate driver circuit.

The final mechanical locking system should also include suitable physical safety mechanisms and protection against actuator or sensor failure.

---

## License

Choose an appropriate open-source license depending on how you want this project to be reused.

For a permissive hardware/software project, the **MIT License** is a straightforward option.

---

## Author

**Kurnia Aditya Reynaldi**

Electrical Engineer
Indonesia

---

## Project Status

**Development / Prototype**

The firmware is intended for continued development and hardware-specific testing. Pin assignments, database structure, printer configuration, and mechanical parameters may need to be adjusted according to the final hardware implementation.
