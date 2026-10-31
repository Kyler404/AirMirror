# 移交文档 — iPhone 投屏客户端（Windows）

最后更新：2026-10-27（**UI 后端迁移 Qt Widgets**：QSS 界面 + QOpenGLWidget 视频画布，SDL 降级为音频后端；SDL+ImGui 版本保留为回退）
工作目录：`C:\Users\ABC\AppData\Roaming\CherryStudio\Data\Agents\system\2026-10-26\72b16c2e-bf44-42e3-b588-b5650189c43d\mirror-launcher`

> **2026-10-26 搬迁记录**：源码与工作文件（client-src / patches / tools / shots / legacy-ui / 脚本 / 文档）已移到上述新目录。
> `build/`（vcpkg 依赖 + 编译树）、`client/`、`dist/`、`receiver/` 留在原 ZCode 工作区；
> `build-client.ps1` 找不到本地 `build\` 时会自动回退到原编译树，增量构建已在新区验证通过（15 文件 / 26.8 MB）。
> 注意：防火墙规则仍绑定原 `client\airplay-windows.exe` 路径；新目录首次启动时 Windows 会弹窗询问，点「允许访问」即可，或点程序内黄色横幅的「一键放行」，无需额外脚本。
>
> **2026-10-27 UI 后端迁移 Qt（当前主线）**：用户裁定 ImGui 手写坐标不可控，迁移到 Qt Widgets + QSS。
> - **数据面** `src/video/stream_hub.{h,cpp}`：从渲染器抽出的纯数据层（帧槽 I420/NV12/AVFrame、封面/元数据/进度、EMA 统计、设备跟踪、录制馈送）。`VideoRenderer` 在 Qt 模式由 `video_renderer_bridge.cpp` 提供转发壳（协议栈照旧通过 `DeviceContext.renderer` 调它，SDL 窗口线程不编译）。
> - **UI 面** `src/qtui/`：`theme.{h,cpp}`（airmirror 原型 QSS）、`main_window.{h,cpp}`（顶栏/侧栏六页/右栏/页脚，全部布局交给 Qt layout 引擎）、`video_canvas.{h,cpp}`（QOpenGLWidget + GLSL BT.601 YUV shader，30fps 定时重绘，latest-wins 语义保留）。
> - **main.cpp 双轨**：`QT_UI` 编译期二选一（Qt 版 main 在 98 行起；legacy main 在 `#ifndef QT_UI` 内完整保留）。
> - **构建**：`.\build-client.ps1 -Qt`（Qt 6.8.3 LTS 经 aqtinstall 装在 `build\Qt\`，脚本自动 `CMAKE_PREFIX_PATH` + `windeployqt`）；不带 `-Qt` 编 legacy。SDL 在 Qt 模式只出音频（SDL_INIT_AUDIO）。
> - **状态**：两个配置均编译链接通过；Qt 版 --demo 窗口存活验证 OK。**待真机验证**：镜像显示、颜色、延迟、音频、录制、DPI。
> - Qt 6.8.3 开源许可为 LGPL-3.0（动态链接分发合规：随附 Qt DLL + 允许用户替换）；项目其余部分 GPL-3.0 不受影响。

---

## 一、产品目标

做一个把 iPhone 画面投到 Windows 的客户端：
- 镜像走 AirPlay（H.264/HEVC 解码）
- 反控计划走 BLE HID（ESP32 固件已写好，未烧录联调）
- **界面必须中文**
- **必须完全自主可控**：源码在我们手上，一条命令能编出 exe

当前形态：基于开源项目 **moieric11/AirPlay-Windows**（GPL-3.0）fork 改造。

---

## 二、目录结构

```
mirror-launcher/
├─ client-src/              ★ 我们的源码树（94 文件 / 15,979 行）
│  ├─ src/video/video_renderer.cpp    界面全在这里（~2400 行，UI 在后 700 行）
│  ├─ src/video/zh_glyph_ranges.h     自动生成的中文字形表
│  ├─ src/main.cpp                    入口 + 服务启停接线
│  ├─ src/platform/firewall.cpp       防火墙自检
│  ├─ src/ui/ui_tests.cpp             官方 ImGui Test Engine 用例（仅测试版编译）
│  ├─ src/mdns/                       走 Windows DnsServiceRegister
│  ├─ src/airplay/                    RTSP + 会话
│  └─ third_party/imgui_test_engine/  官方测试引擎 v1.91.5（仅 -UiTests 构建）
├─ build-client.ps1        ★ 一键构建：同步源码→编译→输出 client/
├─ client/                 ★ 产物（airplay-windows.exe + 14 DLL，26.7 MB）
├─ start-client.bat        启动（指向 client/）
├─ patches/                历次补丁脚本（localize / fonts / dpi / glyph / service）
├─ build/
│  ├─ AirPlay-Windows/     第三方克隆 + 编译树（含 vcpkg 依赖，别删，重编要 30-60 分钟）
│  └─ overlay-ports/openssl   去掉 PREFER_JOM 的 overlay（jom 下不下来）
├─ legacy-ui/              早期 Web 控制端（已弃用，代码保留）
└─ receiver/airplay-windows/  原版英文二进制（对照用）

关键：改代码只改 client-src/，然后 `.\build-client.ps1`（增量约 3 秒）。
```

---

## 三、构建链路（已跑通，别推倒重来）

### 依赖
- **Visual Studio 18 Community（VS2026）**：`C:\Program Files\Microsoft Visual Studio\18\Community`
  - MSVC 14.51 / Windows SDK 10.0.26100 / CMake 4.3.1 都在
- **vcpkg**：`build/AirPlay-Windows/build/vcpkg`，baseline `9bdf6223...`
- 已编译依赖：ffmpeg、SDL2、SDL2_ttf、OpenSSL、libplist（x64-windows-release triplet）

### 命令
```powershell
cd mirror-launcher
.\build-client.ps1          # 增量，3 秒
.\build-client.ps1 -Full    # 重新配置（依赖变了才用）
.\build-client.ps1 -UiTests # 构建带官方 ImGui Test Engine 的 UI 测试版
```

### 编译环境的六个坑（都已在脚本里规避，别改回去）
1. **`%TEMP%` 不可写** → cl 报 `D8018 无法创建链接器响应文件` / `D8050 无法执行 c1.dll`。脚本把 TMP/TEMP 指到 `build/tmp`。
2. **PowerShell 5.1 读无 BOM 的 UTF-8 会按 GBK 解析** → 中文注释让 .ps1 语法崩。所有脚本必须存 **UTF-8 with BOM**。
3. **`$ErrorActionPreference="Stop"` + git/cmake** → 它们往 stderr 写进度，被当成致命错误。改用 `$LASTEXITCODE` 判断。
4. **生成器名**：VS2026 是 `Visual Studio 18 2026`，不是 `17 2022`。
5. **debug triplet 的 /Z7 + /MP** → 尽量用 `x64-windows-release`（目标 + 宿主都要）。
6. **OpenSSL 需要 Qt 的 jom**，`download.qt.io` 连不上，国内镜像的包哈希对不上 → 用 `build/overlay-ports/openssl` 去掉 `PREFER_JOM`，回退 nmake（慢但能编）。
7. **项目目录不能随便移动/复制**：`build/out/CMakeCache.txt`、`out/CMakeFiles/`、`out/_deps/*-subbuild/`（ImGui 的 FetchContent 子构建）里都记录了绝对路径，移动后编译必失败。处理：删掉这三样（**保留 `out/vcpkg_installed/`，那是编好的依赖**），再跑 `.\build-client.ps1 -Full` 重新配置。防火墙规则 `MirrorClient` 也是按 exe 绝对路径绑的，移动后要重新指向。

---

### UI 设计系统（2026-10-26 按 airmirror 原型重写）

- 四件套：`app_ui/theme.h`（色板 `#F5F5F3` 背景 / `#FFFFFF` 卡片 / `#20211F` 墨色 / `#D86E49` 暖橙 / `#3A88F5` 蓝 / `#24966B` 绿；圆角 10/17/22；布局常量 SidebarW=190、RightRailW=292、TopbarH=54、FooterH=23）。
- `app_ui/icons.h`：26 枚手绘线性图标（24 网格、1.7px 描边），页面不得自画符号。
- `app_ui/widgets.h`：Button（白药丸/深色主按钮/危险）、PrimaryButton、RecordButton、IconButton、NavItem、Switch（31×18 绿）、ProgressBar、CardBegin/CardEnd（通道分层：背景在内容之下）、StageCard、StatusDot。
- `app_ui/app_ui.h`：六个页面（总览/画面/音乐/录制/设置/系统与网络）+ 顶栏 + 侧栏 + 右栏 + 页脚。页面只读 `Context` 快照、调 action 回调；渲染器每帧填快照、接回调。
- 响应式：详情页 `##au_detail` 为可滚动顶层窗口，800px 窄窗不横向溢出（测试覆盖 800/1400）。

### UI 自动化测试（官方 Dear ImGui Test Engine）

- 依赖：`client-src/third_party/imgui_test_engine`（官方 v1.91.5，与 ImGui v1.91.5 对齐；测试版才编译）。
- 构建：`.\build-client.ps1 -UiTests`；运行：`client\airplay-windows.exe --ui-tests --demo` 或构建目录 `ctest -C Release -R ui-tests --output-on-failure`（ctest 在 VS 的 CMake 目录里，不在 PATH）。
- 结果：exe 旁 `ui-tests-results.xml`（JUnit），退出码 0/1。
- 现有用例（8 个，2026-10-26 全绿）：`dpi_consistency`（侧栏 190/顶栏 54/导航行 37 随 DPI 缩放）、`nav_pages`（六页切换 + 导航对齐）、`overview_stage`（全屏/纯画面按钮、右栏 292、传输控件）、`settings_controls`（名称框 + GPU 开关回环）、`responsive_home`（800/1400 全部六页无横向溢出）、`music_layout`（音频设备切换 + 窄窗）、`receiver_name_autosave`（失活自动保存）、`receiver_name_length_guard`（超长拒绝）。
- 生产构建自动切回 OFF；已验证 exe 内 `ImGuiTestEngine` 符号为 0。
- 用法要点：断言是数值，不要截图肉眼量。窗口用绝对路径 `//##au_side`、`//##au_main`、`//##au_detail`、`//##au_rail`——**详情容器必须是顶层窗口**（BeginChild 的名字带 hash 后缀，测试引擎按路径找不到）。播放/暂停共用同一 item ID（`##au_rplay`），图标按状态换。导航点击写入 `view.pending_page` 帧末生效：测试里点导航前要 `SetRef("##au_side")`。
- 许可证：Dear ImGui Test Engine License（LICENSE 在 third_party 内）。

## 四、已实现的功能

| 功能 | 状态 | 说明 |
|---|---|---|
| 中文界面 | ✅ | 126 条词条 / 139 处替换，`patches/localize.py` |
| 中文字形 | ✅ | `patches/gen_glyph_range.py` 已改为**扫描全部源码**（473 码点），新增中文文案后重跑即可，不会再出问号 |
| DPI 清晰度 | ✅ | `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` + 字体/样式/布局 × g_ui_scale |
| 布局与视觉 | ✅ 2026-10-26 按 airmirror 原型重写 | 六页结构（总览/画面/音乐/录制/设置/系统与网络）、顶栏+双列导航+总览右栏三卡、自绘 26 枚线性图标、页面切换渐隐、诊断并入页脚；全部功能保留且不再重复入口 |
| 服务开关 | ✅ | 侧栏底部「停止接收/开始接收」，改从窗口底边向上排，**不再被裁掉** |
| 防火墙横幅 | ✅ 重做 | 紧凑提示条；Unknown 状态（本机 COM 检测必失败）也有「一键放行」（netsh 提权，`firewall.cpp` 的 `netsh_rule_request`）和「不再提示」；Managed 状态用中性灰。**已放行过就不再闪回**：会话内 `handled_` 闩锁 + exe 旁 `firewall_ok.marker` 标记（2026-10-24 用户实测反馈"点完消失一小会又回来"后修复；marker 存在则启动也不弹。规则若被手动删除，删掉 marker 即恢复检测） |
| 设置页面 | ✅ 重写 | 独立「设置」页（接收设备/镜像分辨率/渲染与性能卡片），不再用抽屉；滚动正常 |
| 滚动策略 | ✅ | 固定工具栏/侧栏无滚动条；首页、音乐页和设置面板按需纵向滚动，窄窗口不会裁切内容 |
| 英文残留 | ✅ 基本清零 | 旋转模式/设置提示/录制标签/诊断栏/横幅/对话框标题/PAUSED 等全部中文化 |
| 默认名称 | ✅ | 接收端名（iPhone 上显示）=「Windows 投屏」，窗口标题=「AirPlay 投屏」 |
| 隐藏界面提示 | ✅ | 进入隐藏模式后底部浮现「按 H 或 Esc 恢复界面 · 双击全屏」约 4 秒后淡出（用户反馈"不知道怎么恢复"后加的；快捷键 H / Esc，见设置→快捷键） |
| 代码签名 | ✅ | exe 自签名（`CN=AirPlay Mirror Client`），公钥在 `tools/mirror-client-code.cer`，`build-client.ps1` 自动签名。**注意：签名≠免弹窗**——SmartScreen 按微软云信誉拦截，自签名程序永远攒不到信誉。分发到其他电脑的三个选项（详见第七节）；本机已用 `tools/disable-smartscreen.ps1` 关闭检查（`-Restore` 可恢复） |
| 投屏边框 | ✅ | 镜像画面外套设备外壳，合成方式与 Bezel 官方 HTML 基准一致（用户提供）：**整张边框 PNG 为定位容器**，屏幕内容区由**构建期清单**精确定位（patches/gen_frame_manifest.py：洪泛填充连通域分析测出每张素材的屏幕区=设备原生屏幕分辨率，生成 frame_manifest.h，运行时按文件名查表；查不到退回 5.333%/2.5%/89.333%/95% 固定比例）；画面 cover 填充内容区，整张边框（含机身、按键、**灵动岛**）叠在上层。实现要点：① 画面以 SDL_RenderGeometry 三角扇按圆角矩形绘制（UV 归一化 0..1），圆角半径=内容区宽的 3.65%（屏幕孔真实圆角，素材实测 48px/1313）——圆角外不绘制，画面显示到屏幕圆角边缘（此前 14.925% 大半径的黑角/填角方案全部废弃，会吃进屏幕或凸出机身）；② NV12/YUV 视频纹理不能直接走几何管线（D3D11 只绑主 SRV），每帧先转绘到同尺寸 RGBA 离屏纹理再采样；③ 加载时屏幕区四角、45° 斜切线（弦长=屏宽的 3.7%——素材实测孔角是斜切倒角而非圆弧，遮罩必须同形，圆弧遮罩会留楔形缝）外的像素填黑——方角画面的斜角遮罩（与素材黑边框融合，斜切线内正常显示）；④ 绘制源矩形=整张纹理——素材自带的半透明投影叠在纯黑窗口底上不可见（无需裁剪）。窗口/全屏/隐藏界面统一布局：外壳完整可见尽量大。**透明实现（最终定版：窗口区域 SetWindowRgn）**：隐藏界面按机身轮廓（素材逐行不透明范围，含侧键/圆角）生成 Win32 区域裁剪主窗口——区域外不属于窗口，桌面直接透出。画面用孔形掩码（洪泛填充，构建期可由 tools/frame_compositor.py --emit-masks 预生成）逐像素裁剪 alpha（SDL_ComposeCustomBlendMode：色不变、a=掩码.a×dst.a），边框 PNG 整张叠加零修改。**本环境三大发现**：① ULW/分层窗口在本机被系统性禁用（连最小自建窗口都 gle=31，与 settings.ini 写入被拒同源的沙箱限制）；② ULW 与 WS_THICKFRAME 冲突（若在其他机器恢复 ULW 方案，覆盖窗口不可带 RESIZABLE）；③ 色键/blur-behind/DWM-alpha 在 D3D11/GL 交换链窗口一律无效。渲染器=OpenGL（GPU 解码回退 CPU）。全屏仍主窗口全屏。**调试工作流（用户要求的高效方式，勿再截图肉眼调）**：改几何 → tools/frame_compositor.py 离线合成验证（毫秒级）→ 编译 → tools/verify_render.py 数值断言（孔内有画面/黑边/四角透桌面；注意断言带方向别取反、黑边仅约 6px 窄带）。ulw_probe.py/ulw_min_test.py 留档用于分层 API 环境诊断。**隐藏界面（H）的窗口吸附「机身比例」（body_aspect，而非画面比例），且外壳加载后补一次吸附（按 H 时设备可能未连）+ 机身过扫描 5%（圆角/边缘裁到窗口外，否则窗口四角露出背景色贴片——用户实测踩过）**：手机完整填满窗口、四周无背景（参考图 2）；全屏同样机身铺满（上下超出裁掉）。设置→投屏边框：**自动（跟随机型，默认）/ 关闭 / 手动指定**；自动匹配经 `kModelAliases` 表（标识符→营销名），结果记日志。素材：7 个 DMG 已全解，`assets/frame/` 122 张（iPhone 15-18 全系 + 4 款 iPad）。stb_image 解码（vcpkg ffmpeg 的 PNG 解码器被禁用） |
| `--demo` 模式 | ✅ | `airplay-windows.exe --demo`：本地合成镜像流（渐变+移动色块）并伪造 Mirror 设备，无需真机即可调试投屏显示路径（边框/全屏/录制 UI 等），main.cpp 内 ~40 行 |
| 投屏 | ✅ 已验证 | 手机能连上、画面正常（本轮改动前验证；核心流未动） |

验证工具：`tools/screenshot.ps1`（PrintWindow 抓窗口，含 DPI 感知）、`tools/click.ps1`（按窗口相对坐标点击）。截图在 `shots/`。

---

## 五、踩过的坑（按重要度）

1. **防火墙规则是按“程序路径”绑定的**。换了 exe 目录（receiver\ → client\）规则就失效，表现为“进程在跑、端口在听、但手机 SYN 石沉大海”。抓包能看到 SYN 重传、无 SYN-ACK。
   - 现在规则名 `MirrorClient`，指向 `client\airplay-windows.exe`
   - **Block 规则优先于 Allow**：曾经因为弹窗被点「取消」，生成了两条 Block 规则，导致后续所有 Allow 规则全废，且不再弹窗
2. **mDNS 广播里的脏地址**：WSL 虚拟网卡 `172.19.48.1` 会被一起广播，且曾排在前面。iOS 选到就永远连不上。
   - 缓解：把 `vEthernet (WSL)` 的 InterfaceMetric 从 15 改成 9999，广播顺序变成 LAN 优先
   - 更彻底：禁用 WSL 网卡（但用户需要 WSL）
   - 应用层无法过滤：地址由 Windows DnsServiceRegister 自动发布
3. **ImGui 字形范围**：`GetGlyphRangesChineseSimplifiedCommon()` 只有约 2500 字，界面用字可能不在里面 → 显示问号。且范围数组是 **[起,止] 成对**，平铺单码点会被解释成区间。
4. **DPI**：不声明感知 → Windows 位图拉伸 → 整个界面发虚。
5. **SDL 舞台与 ImGui 面板的几何必须同源**：`chrome_top/chrome_left/chrome_bottom` 曾用未缩放常量、面板用缩放值，1.25 下视频与侧栏/工具栏重叠。现已统一从缩放后的 chrome 值推导。
6. **垂直布局的魔法数字必须 S() 缩放**，且内容底部要贴底钳制——否则防火墙横幅一出现，三步指引就被顶出窗口。
7. **ImGui 对中文只按 ASCII 空格断行**：长中文句子要在自然停顿处放空格，否则折行破碎（如「屏幕镜像」被拦腰截断）。
8. **固定面板标志与可滚动容器要分开**——`kPanelFlags` 含 NoScrollbar/NoScrollWithMouse，设置等详情页必须用去掉这两个标志的 `kScroll`，否则下半部分够不着。改完必须滚一遍验证。
9. **Push/Pop 必须逐路径平衡**：`DrawRightRail` 曾推 2 个 StyleVar 只弹 1 个（ItemSpacing 在卡片循环后未弹），每帧报 Missing PopStyleVar 且状态泄漏。静态审计脚本（统计 Push/PopStyleVar/Color/Font 配对数）+ 测试日志双重把关。

---

## 六、未解决 / 待办

### P0 — 用户明确抱怨的
1. ~~主页显示是否正确没验证过~~ → **2026-10-24 已验证并修复**：原版有防火墙横幅把三步指引顶出窗口、侧栏服务开关被裁、DPI 几何错位等问题，全部修复（见「已实现的功能」表和 `shots/` 前后对比）。
2. ~~UI/UX 被评价为“反人类”~~ → 第一轮修复完成（布局硬伤 + 英文残留 + 设置结构 + 防火墙横幅）。**用户是否满意待确认**，视觉口吻（大标题文案「你的设备。再大一点。」等）未动，等用户反馈。
3. ~~左右两侧滚动条~~ → 已验证消失；代价是设置面板滚动曾被误杀，已用 `kScrollableFlags` 修复。

### P1 — 功能
4. ~~防火墙自检不可靠~~ → 大幅改善：Unknown 状态现在有「一键放行」（netsh 提权，本机环境可用）；**按钮流程未实测**（需要点 UAC），首次真机连接时验证。
5. ~~设置持久化失败~~ → 2026-10-26 复查：`%APPDATA%\AirPlay-Windows` 已可写，切换 GPU 开关后 `settings.ini` 立即更新。名称保存另外修了两处：输入框从 51 字节扩到 256 字节（原来超长中文会被截断成非法 UTF-8，导致保存失败），并改为失焦/回车自动保存（原提示写“自动保存”但实际要手动点「应用名称」）。
6. ~~窗口标题/设备名仍是英文~~ → 已改中文（「AirPlay 投屏」/「Windows 投屏」）。
7. ~~零散英文残留~~ → 已清零（字形表改为扫描源码，新增文案安全）。

### P2 — 计划中的
8. **ESP32 BLE HID 反控**：固件已写好（`legacy-ui/firmware/ios_bridge_hid/`），未烧录。串口协议 JSON Lines，复合 HID（Report ID 1=鼠标，2=键盘）。
9. 最小化到托盘、开机自启。

---

## 七、分发到其他电脑（SmartScreen 问题的现实选项）

SmartScreen 的拦截依据是**微软云端的文件信誉**（全球运行量），不是本地信任。私有分发的程序永远没有信誉，所以：

1. **目标机跑一次 `tools/disable-smartscreen.ps1`**（管理员/UAC）：关闭「检查应用和文件」，之后不再弹。适合自己的电脑、家人朋友等你能当面操作的少量机器。恢复默认：`tools/disable-smartscreen.ps1 -Restore`。代价：那台机器对**所有**未知 exe 都不再拦截。**已选定此方案**，标准流程：`build-client.ps1` → `tools/make-dist.ps1` 生成 `dist\AirPlay投屏-分发包.zip`（内含一键关闭 SmartScreen 的 bat + 中文使用说明；防火墙由系统首次启动弹窗或程序内「一键放行」处理，不再附脚本），发给对方，对方按包内「使用说明.txt」操作即可。
2. **引导用户点一次**（不动对方安全设置的标准做法）：首次弹窗时点「更多信息 → 仍要运行」。缺点：没有云信誉时**每次启动都会弹**，不只是第一次。
3. **EV 代码签名证书**（唯一根治方案）：付费（约 ¥1500-3000/年，需企业/个人身份验证），签发后云信誉即时生效，任何机器都不弹。买了证书后用 `signtool sign /fd SHA256 /a client\airplay-windows.exe` 替换自签名（或改 `build-client.ps1` 里的签名段），自签名证书即可弃用。

签名本身仍保留价值：弹窗时发行者显示我们的名字而非「未知」，UAC/安装类提示也更友好；将来买 EV 证书是同一套机制的无缝升级。

---

## 八、接手建议

1. **先启动看一眼**：`.\start-client.bat`，然后问用户主页哪里不对——不要自己猜。
2. **改代码流程**：改 `client-src/` → `.\build-client.ps1` → 重启 `start-client.bat`。3 秒一轮，迭代很快。
3. **别删 `build/AirPlay-Windows/build/vcpkg`**：里面是编好的依赖，重来要 30–60 分钟。
4. **验证连接问题先抓包**：
   ```powershell
   pktmon filter add -p 7000
   pktmon start --capture --pkt-size 128 --file-name cap.etl
   # 手机连一次
   pktmon stop; pktmon etl2txt cap.etl -o cap.txt
   ```
   - 有 SYN 无 SYN-ACK → 防火墙（先查规则路径是否匹配当前 exe）
   - 完全没包 → 手机拨错地址（查 mDNS 广播）
5. **诚实记录**：上一任在这些地方翻过车——用错误的检测手段（`Get-NetFirewallRule` 在该环境返回空集，648 条规则读不到，要用 `netsh`）下过三次错误结论；补丁写坏过语法两次（正则少括号、粘到 `#include` 上）。所以：**每次改完必须编译 + 实际启动验证**，不要只看代码。

---

## 九、关键证据

- 成功的投屏日志：`decoded 3200 frames (398x864)`
- 连接失败的抓包：手机发 84 个 SYN，电脑 0 个 SYN-ACK
- 改路径后失效的防火墙规则：旧规则 `AirPlay-Windows` 指向 `receiver\`，新 exe 在 `client\`
