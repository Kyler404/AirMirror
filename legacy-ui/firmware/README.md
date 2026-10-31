# ESP32 BLE HID 固件

把 ESP32 变成 iPhone 认识的蓝牙鼠标 + 键盘，串口收 JSON 就动。

## 需要的东西

- **一块 ESP32**：任意带 BLE 的型号都行（ESP32-WROOM-32 / C3 / S3 / S2）。经典 ESP32 最稳。
- **Arduino IDE 2.x**
  - 板管理器装 `esp32 by Espressif`（3.x）
  - 库管理器装 `NimBLE-Arduino`（作者 h2zero，2.x）
- USB 线（能传数据的那种，不是纯充电线）

## 烧录

1. 打开 [ios_bridge_hid.ino](ios_bridge_hid.ino)
2. 开发板选你的型号，端口选 COMx
3. 上传
4. 打开串口监视器（115200），看到 `[boot] iOS Bridge HID ready` 就成了

## 配对

iPhone：**设置 → 蓝牙 → 选 `iOS Bridge HID`**。

连上后屏幕上会出现鼠标指针（iOS 13+ 原生支持指针设备）。连上的瞬间固件会自己抖一下指针，确认链路通了。

> 如果蓝牙列表里搜不到：先看串口有没有 `ready`，再确认 iPhone 距离在 1 米内。板子重启后如果连不上，去蓝牙设置里「忽略此设备」重配对一次（配对信息存在 iPhone 侧，固件重烧后对不上）。

## 串口协议

115200，一行一条 JSON，字段和启动台完全一致：

| 事件 | 示例 | 说明 |
|---|---|---|
| 移动 | `{"t":"m","dx":3,"dy":-2}` | 相对位移；超过 ±127 固件自动拆包 |
| 按键 | `{"t":"b","b":1,"d":1}` | b: 1 左 2 右 4 中；d: 1 按下 0 抬起 |
| 滚轮 | `{"t":"w","d":1}` | 正数向下（反了改 `WHEEL_INVERT`） |
| 键盘 | `{"t":"k","u":4,"m":0,"d":1}` | u: HID usage id；m: 修饰位 Ctrl1/Shift2/Alt4/GUI8 |

固件里没有 JSON 库，只有针对这四种消息的极简字段提取（`findType` / `findInt`）。要加字段就照着写。

## 台架验证（不接手机也能测一半）

```powershell
python tests/serial_probe.py --list          # 看有没有串口
python tests/serial_probe.py COM5            # 画方框 → 单击 → 滚轮 → 输入 hello
python tests/serial_probe.py COM5 --move-only
```

先关掉 Arduino 串口监视器和启动台，否则 COM 口被占用会打不开。

## HID 报告结构

| 报告 | ID | 载荷 |
|---|---|---|
| 鼠标 | 1 | `[01][buttons][dx][dy][wheel]` — 5 字节 |
| 键盘 | 2 | `[02][mods][00][k1..k6]` — 9 字节 |

HOGP 规定 **Report 特征值要带 Report ID 前缀**，少这个字节 iOS 会当成非法报告直接丢掉。

描述符是复合设备（一个 HID service，两个 Report ID），不是两个独立的 HID 服务——iOS 对后者支持很差。

## 排错

| 现象 | 原因 / 处理 |
|---|---|
| 蓝牙里搜不到 | 没在广播。看串口有无 `[boot] ready`；烧录后要断电重启一次 |
| 连上但指针不动 | 90% 是 Report 特征值少了 Report ID 前缀；其次是 iOS 辅助功能里「触控 → 辅助触控 → 设备」把指针设备禁了 |
| 指针方向反了 | 交换 `dx`/`dy` 符号，或在启动台前端翻转 |
| 滚轮方向反了 | `#define WHEEL_INVERT 1` |
| 连上几秒就断 | 配对信息过期。iPhone「忽略此设备」后重连；固件侧可以调 `NimBLEDevice::deleteAllBonds()` 清一次 |
| 打出来的字不对 | 固件按 **US 布局** 发 usage id。iPhone 键盘布局改成 English (US) 就对了 |
| 编译报 `onConnect` 签名错误 | 你装的是 NimBLE-Arduino **1.4**。要么升级到 2.x，要么把回调改成 `onConnect(NimBLEServer*, ble_gap_conn_desc*)`、`onDisconnect(NimBLEServer*, ble_gap_conn_desc*, int)` |

## 这套方案的硬限制（先知道，省得后面返工）

- **只有相对位移**：BLE 鼠标是相对坐标设备，没有绝对定位，也就没有「点哪指哪」的映射。
- **没有多点手势**：双指缩放、滑动返回、三指切换这些做不了。真需要只能走 WDA（go-ios）那条自动化通道，延迟会上去。
- **指针速度由 iOS 决定**：系统设置里的「指针速度」会叠加在固件输出上，调手感要去 iPhone 上调。
- **部分 app 不响应**：HID 指针在 App Store 类 app 里正常，少数游戏/全屏 app 会忽略指针事件。
