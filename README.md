# iPhone → Windows 投屏

产品形态：**一个接收端程序 + 一个启动脚本**。手机通过 AirPlay 把画面投到 Windows，接收端窗口里可以调设置。

```
iPhone ──AirPlay(H.264 + 音频)──►  AirMirror 接收端（Windows, Qt 界面）
                                   ├─ 六页界面：总览 / 画面 / 音乐 / 录制 / 设置 / 系统与网络
                                   └─ 同一窗口内嵌画面（纯画面模式隐藏周边）
```

界面用 **Qt Widgets + QSS**（2026-10-27 起）：布局由 Qt 排布、HiDPI 自动处理；
视频是 QOpenGLWidget + YUV shader 直绘。构建：`.\build-client.ps1 -Qt`
（Qt 6.8.3 在 `build\Qt\`，缺了用 aqtinstall 重装）。

## 启动

双击 **`start-client.bat`**（或直接双击 `client\airplay-windows.exe`）。

接收端启动后就在监听（TCP 7000），手机：

```
控制中心 → 屏幕镜像 → 选「Windows 投屏」
```

## 窗口与热键

视频和界面在**同一个窗口**里（这是接收端本身就有的设计，没有独立视频窗口功能）。

| 按键 | 作用 |
|---|---|
| `H` | 隐藏界面 → 纯画面；再按一次切回设置界面 |
| `F11` | 全屏切换 |
| `ESC` | 退出全屏 → 退出纯画面 → 退出程序 |

日常用法：连上后按 `H` 看画面，要改设置再按 `H` 回来。

录制入口在**总览右栏的「录制」卡片**（状态行 + 开始/停止按钮），不占用热键。

## 关闭与托盘

点窗口关闭按钮时：

- 默认弹出询问框，可选「最小化到托盘（服务继续）」或「退出程序」，勾选「不再询问」后记住选择；
- 「系统与网络 → 关闭行为」里可以随时改成：每次询问 / 最小化到托盘 / 直接退出；
- 托盘运行时投屏服务保持开启，**左键点托盘图标**恢复窗口，右键菜单可真正退出。

> 注意：窗口关闭 ≠ 服务停止。要完全停止接收，请退出程序。

## 首次使用：防火墙

首次启动时 Windows 会弹「是否允许访问网络」，点「允许访问」即可，之后不再询问。

如果没弹窗或误点了取消：在程序内黄色横幅点「一键放行」（只弹一次 UAC），不需要另开脚本。

> 例外：如果之前点过「取消」，Windows 可能留下这个程序的 **Block 规则**（Block 永远压过 Allow）。
> 这种情况到「Windows 安全中心 → 防火墙和网络保护 → 允许应用通过防火墙」删掉阻止项，
> 再回程序点「一键放行」。

## 目录

| 路径 | 说明 |
|---|---|
| `client-src/` | 源码树（改代码只改这里，然后跑 `build-client.ps1`） |
| `client/` | 构建产物（exe + DLL + assets） |
| `start-client.bat` | 启动脚本 |
| `LICENSE` | GPL-3.0（与 `client-src/LICENSE` 同一协议） |
| `legacy-ui/` | 早期做的 Web 控制端（进程管控 + 虚拟触控板 + ESP32 HID 通道），已从主流程移除，代码保留 |
| `legacy-ui/firmware/` | ESP32 BLE HID 固件（鼠标+键盘复合设备，串口 JSON 驱动） |

## 关于 `legacy-ui/`

那套 Web 控制端做的事：托管接收端进程、检测手机连接后自动弹窗、虚拟触控板 + ESP32 蓝牙 HID 反控。
目前主流程不用它（需求改为"直接跑接收端"），但代码是完整的，随时可以拿回来：

```powershell
# 恢复用法（需要 Python）
python legacy-ui\client.py --dev
```

## 排错

| 现象 | 原因 | 处理 |
|---|---|---|
| 手机能看到设备但连不上 | 防火墙未放行 / 残留 Block 规则 | 程序内点「一键放行」；仍不行就到防火墙设置删掉阻止项 |
| 设备在列表里但点了没反应 | mDNS 缓存过期，或广告里混入虚拟网卡地址 | 手机 Wi-Fi 关掉再开；或用 `Get-NetIPAddress`、zeroconf 检查广播的 A 记录 |
| 窗口打不开 / 闪退 | 缺少 DLL | 确保 `receiver/airplay-windows/` 里面的 DLL 都在原位 |
| 设置改不了 | 接收端写 `%APPDATA%\AirPlay-Windows\settings.ini` 失败 | 手动建目录：`New-Item -ItemType Directory "$env:APPDATA\AirPlay-Windows"` |

抓包定位（需要管理员）：

```powershell
pktmon filter add -p 7000
pktmon start --capture --pkt-size 128 --file-name cap.etl
pktmon stop; pktmon etl2txt cap.etl -o cap.txt
```

- 有 SYN 进来但没回应 → 防火墙丢包
- 一个包都没有 → 手机拨到了错误地址（mDNS 广播里的脏地址）

## 界面

界面按 airmirror 设计原型实现（2026-10-26 重写）：暖白背景 `#F5F5F3`、白色卡片、墨色正文 `#20211F`，暖橙 `#D86E49` 与交互蓝 `#3A88F6` 只做少量强调；圆角 10/17/22，4px 间距网格，26 枚自绘线性图标。

结构为六页导航：

| 分组 | 页面 | 内容 |
|---|---|---|
| WORKSPACE | 总览 | 舞台卡（镜像画面）+ 右栏三卡（连接设备 / 正在播放 / 屏幕录制） |
| | 画面 | 镜像预览（设备边框）、画面缩放、画面方向 |
| | 音乐 | 正在播放（封面/进度/传输控制）、音频接收、遥控说明 |
| | 录制 | 录制状态、编码选项、保存位置 |
| PREFERENCES | 设置 | 接收设备名称、镜像分辨率、渲染与性能 |
| | 系统与网络 | 防火墙、诊断与维护、网络信息 |

窗口控制（最小化/最大化/关闭）内嵌在顶栏右侧；诊断统计并入底部状态条。

## UI 测试

界面布局和名称保存用官方 **Dear ImGui Test Engine** 做数值回归（8 个用例，不靠截图）：

```powershell
.\build-client.ps1 -UiTests
client\airplay-windows.exe --ui-tests --demo   # 或在构建目录 ctest -C Release -R ui-tests
```

结果写到 exe 旁 `ui-tests-results.xml`，退出码 0/1。测试版才编译测试引擎；普通 `.\build-client.ps1` 不会把它带进分发包。
