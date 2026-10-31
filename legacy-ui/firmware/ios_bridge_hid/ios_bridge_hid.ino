/*
 * ios_bridge_hid.ino — ESP32 蓝牙 HID 桥（鼠标 + 键盘复合设备）
 *
 * 作用：把串口收到的 JSON Lines 转成 BLE HID 报告发给 iPhone。
 *       PC 端不直接蓝牙，因为 Windows 不能当 HID peripheral；这块板子顶替这个角色。
 *
 * 环境：
 *   Arduino IDE 2.x
 *   esp32 core 3.x（Board Manager 装 "esp32 by Espressif"）
 *   库：NimBLE-Arduino 2.x（库管理器搜 NimBLE-Arduino，作者 h2zero）
 *
 * 串口协议（115200，一行一条 JSON）：
 *   {"t":"m","dx":3,"dy":-2}         相对移动
 *   {"t":"b","b":1,"d":1}            按键 b:1左 2右 4中；d:1按下 0抬起
 *   {"t":"w","d":1}                  滚轮，正数向下
 *   {"t":"k","u":4,"m":2,"d":1}      键盘 u=HID usage，m=修饰位，d=按下/抬起
 *
 * 注意：本文件按 NimBLE-Arduino 2.x 的 API 写（ESP32 core 3.x）。
 *       1.4 版的回调签名不同（onConnect 用 ble_gap_conn_desc*），见 README。
 */

#include <NimBLEDevice.h>

#define DEVICE_NAME   "iOS Bridge HID"
#define WHEEL_INVERT  0        // 滚轮方向反了就改 1
#define SERIAL_BAUD   115200

// ---------------------------------------------------------------- HID 描述符
// 复合设备：Report ID 1 = 鼠标（5 键 + X/Y/滚轮），Report ID 2 = 键盘（6KRO）
static const uint8_t REPORT_MAP[] = {
  // ---- 鼠标
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Button)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x05,        //     Usage Maximum (5)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x75, 0x01,        //     Report Size (1)
  0x95, 0x05,        //     Report Count (5)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0x75, 0x03,        //     Report Size (3)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x01,        //     Input (Const)  —— 补齐 1 字节
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data,Var,Rel)
  0xC0,              //   End Collection
  0xC0,              // End Collection

  // ---- 键盘
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x02,        //   Report ID (2)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0xE0,        //   Usage Minimum (LeftControl)
  0x29, 0xE7,        //   Usage Maximum (RightGUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs)  —— 修饰键
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const)         —— 保留字节
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0xFF,        //   Logical Maximum (255)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0xFF,        //   Usage Maximum (255)
  0x81, 0x00,        //   Input (Data,Array,Abs) —— 6 键无冲
  0xC0               // End Collection
};

// ---------------------------------------------------------------- 全局状态
static NimBLECharacteristic* chrMouse = nullptr;
static NimBLECharacteristic* chrKeyboard = nullptr;
static volatile bool gConnected = false;
static volatile bool gNeedWiggle = false;   // 连上后抖一下指针，放到 loop 里做，别阻塞 BLE 任务

static uint8_t gButtons = 0;
static uint8_t gMods = 0;
static uint8_t gKeys[6] = {0, 0, 0, 0, 0, 0};

// ---------------------------------------------------------------- HID 发送
static void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!gConnected || !chrMouse) return;
  // HOGP 的 Report 特征值要带 Report ID 前缀
  uint8_t rep[5] = {0x01, buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
  chrMouse->setValue(rep, sizeof(rep));
  chrMouse->notify();
}

static void sendKeyboard() {
  if (!gConnected || !chrKeyboard) return;
  uint8_t rep[9] = {0x02, gMods, 0x00};
  for (int i = 0; i < 6; i++) rep[3 + i] = gKeys[i];
  chrKeyboard->setValue(rep, sizeof(rep));
  chrKeyboard->notify();
}

static void keyDown(uint8_t usage) {
  for (int i = 0; i < 6; i++) if (gKeys[i] == usage) return;   // 已按下，忽略重复
  for (int i = 0; i < 6; i++) {
    if (gKeys[i] == 0) { gKeys[i] = usage; sendKeyboard(); return; }
  }
  // 6 键满：挤掉最早的一个
  for (int i = 0; i < 5; i++) gKeys[i] = gKeys[i + 1];
  gKeys[5] = usage;
  sendKeyboard();
}

static void keyUp(uint8_t usage) {
  for (int i = 0; i < 6; i++) {
    if (gKeys[i] == usage) {
      gKeys[i] = 0;
      sendKeyboard();
      return;
    }
  }
}

static void clearKeys() {
  for (int i = 0; i < 6; i++) gKeys[i] = 0;
  gMods = 0;
  sendKeyboard();
}

// ---------------------------------------------------------------- 服务端
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    gConnected = true;
    gNeedWiggle = true;          // 真正的抖动在 loop() 里做，避免阻塞协议栈
    Serial.println("[ble] connected");
  }

  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    gConnected = false;
    gButtons = 0;
    clearKeys();
    Serial.printf("[ble] disconnected, reason=%d\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

// ---------------------------------------------------------------- 串口解析
// 只认自己的固定格式，不引 JSON 库：找 "key" 后面第一个数字
static int findInt(const char* s, const char* key, int def) {
  const char* p = strstr(s, key);
  if (!p) return def;
  p += strlen(key);
  while (*p && (*p == ' ' || *p == ':' || *p == '"')) p++;
  if (*p != '-' && (*p < '0' || *p > '9')) return def;
  return atoi(p);
}

static char findType(const char* s) {
  const char* p = strstr(s, "\"t\"");
  if (!p) return 0;
  p += 3;
  while (*p && (*p == ' ' || *p == ':')) p++;
  if (*p != '"') return 0;
  p++;
  return *p;
}

static void handleLine(const char* line) {
  char t = findType(line);
  if (t == 'm') {
    int dx = findInt(line, "\"dx\"", 0);
    int dy = findInt(line, "\"dy\"", 0);
    while (dx || dy) {                       // 单次报告上限 ±127，超了拆包
      int8_t sx = (int8_t)constrain(dx, -127, 127);
      int8_t sy = (int8_t)constrain(dy, -127, 127);
      sendMouse(gButtons, sx, sy, 0);
      dx -= sx;
      dy -= sy;
      delay(4);
    }
  } else if (t == 'b') {
    int bit = findInt(line, "\"b\"", 0);
    int d = findInt(line, "\"d\"", 0);
    if (d) gButtons |= (uint8_t)bit; else gButtons &= (uint8_t)~bit;
    sendMouse(gButtons, 0, 0, 0);
  } else if (t == 'w') {
    int d = findInt(line, "\"d\"", 0);
#if WHEEL_INVERT
    d = -d;
#endif
    sendMouse(gButtons, 0, 0, (int8_t)constrain(d, -127, 127));
  } else if (t == 'k') {
    uint8_t usage = (uint8_t)findInt(line, "\"u\"", 0);
    uint8_t mods = (uint8_t)findInt(line, "\"m\"", 0);
    int down = findInt(line, "\"d\"", 1);
    gMods = mods;
    if (!usage) { sendKeyboard(); return; }
    if (down) keyDown(usage); else keyUp(usage);
  }
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  Serial.begin(SERIAL_BAUD);

  NimBLEDevice::init(DEVICE_NAME);
  // iOS 的 HOGP 要求加密；Just Works 配对即可
  NimBLEDevice::setSecurityAuth(true, false, true);   // bonding, no MITM, SC
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  NimBLEService* hid = server->createService(BLEUUID((uint16_t)0x1812));

  // HID Information: bcdHID=1.11, country=0, flags=0x02(可被连接)
  uint8_t info[4] = {0x11, 0x01, 0x00, 0x02};
  hid->createCharacteristic(BLEUUID((uint16_t)0x2A4A), NIMBLE_PROPERTY::READ)
      ->setValue(info, sizeof(info));

  hid->createCharacteristic(BLEUUID((uint16_t)0x2A4B), NIMBLE_PROPERTY::READ)
      ->setValue((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));

  hid->createCharacteristic(BLEUUID((uint16_t)0x2A4C), NIMBLE_PROPERTY::WRITE_NR);

  uint8_t protocolMode = 0x01;   // Report protocol
  hid->createCharacteristic(BLEUUID((uint16_t)0x2A4E),
                            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR)
      ->setValue(&protocolMode, 1);

  chrMouse = hid->createCharacteristic(
      BLEUUID((uint16_t)0x2A4D), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint8_t mouseRef[2] = {0x01, 0x01};          // report id 1, input
  chrMouse->createDescriptor(BLEUUID((uint16_t)0x2908))->setValue(mouseRef, 2);

  chrKeyboard = hid->createCharacteristic(
      BLEUUID((uint16_t)0x2A4D), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint8_t kbRef[2] = {0x02, 0x01};             // report id 2, input
  chrKeyboard->createDescriptor(BLEUUID((uint16_t)0x2908))->setValue(kbRef, 2);

  hid->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setName(DEVICE_NAME);
  advData.setAppearance(0x03C1);               // HID Keyboard
  advData.addServiceUUID(hid->getUUID());
  NimBLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->start();

  Serial.println("[boot] iOS Bridge HID ready");
}

void loop() {
  // 刚连上：轻微抖一下指针，让 iOS 把鼠标指针亮出来
  if (gNeedWiggle) {
    gNeedWiggle = false;
    delay(300);
    if (gConnected) {
      sendMouse(gButtons, 1, 0, 0);
      delay(40);
      sendMouse(gButtons, -1, 0, 0);
    }
  }

  static char buf[256];
  static size_t len = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len) {
        buf[len] = 0;
        handleLine(buf);
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    } else {
      len = 0;                    // 超长行，丢弃重来
    }
  }
}
