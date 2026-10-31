#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Alt+加号 最终修正：按住 Alt 直接按主键盘 +/= 物理键即放大。
放弃 Shift 要求（用户实际按法就是 Alt+该键）。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 热键：主键盘 +/= 键（0xBB）注册为 MOD_ALT（与小键盘 VK_ADD 并存）
old = """        RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +
        RegisterHotKey(hwnd, kAltShiftPlus,
                       MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 0xBB);         // 主键盘 +(Shift+=)"""
new = """        RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +
        RegisterHotKey(hwnd, kAltShiftPlus, MOD_ALT | MOD_NOREPEAT, 0xBB);  // 主键盘 +/= 键"""
assert old in t, "A"
t = t.replace(old, new)

# 2) 注册结果日志（便于发现被其他软件占用的组合）
old = """        MSG msg;"""
new = """        {
            char reg[64];
            std::snprintf(reg, sizeof(reg), "hotkey reg H=%d NP+=%d NM-=%d MK+=%d",
                          (int)(reg1 != 0), (int)(reg2 != 0),
                          (int)(reg3 != 0), (int)(reg4 != 0));
            LOG_WARN << reg;
        }
        MSG msg;"""
assert old in t, "B"
t = t.replace(old, new)

# 3) 注册调用接收返回值
old = """        RegisterHotKey(hwnd, kAltH,     MOD_ALT | MOD_NOREPEAT, 0x48);  // H
        RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +
        RegisterHotKey(hwnd, kAltShiftPlus, MOD_ALT | MOD_NOREPEAT, 0xBB);  // 主键盘 +/= 键
        RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -"""
new = """        const BOOL reg1 = RegisterHotKey(hwnd, kAltH,     MOD_ALT | MOD_NOREPEAT, 0x48);  // H
        const BOOL reg2 = RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +
        const BOOL reg4 = RegisterHotKey(hwnd, kAltShiftPlus, MOD_ALT | MOD_NOREPEAT, 0xBB);  // 主键盘 +/=
        const BOOL reg3 = RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -"""
assert old in t, "C"
t = t.replace(old, new)

# 4) SDL 兜底：EQUALS 带任意 Shift 状态都算（撤销 Shift 强制）
old = """                    case SDLK_EQUALS:
                        // 主键盘的「+」= Shift+'='：要求 Alt+Shift 才算
                        if ((e.key.keysym.mod & KMOD_ALT) &&
                            (e.key.keysym.mod & KMOD_SHIFT)) scale_phone(1.10f);
                        break;"""
new = """                    case SDLK_EQUALS:
                        // 主键盘 +/= 物理键：按住 Alt 直接按即放大
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.10f);
                        break;"""
assert old in t, "D"
t = t.replace(old, new)

# 5) 顺手把主键盘 '-'（0xBD）也注册热键（与减号对称）
old = """        const BOOL reg3 = RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -"""
new = """        const BOOL reg3 = RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -
        RegisterHotKey(hwnd, 5, MOD_ALT | MOD_NOREPEAT, 0xBD);  // 主键盘 -"""
assert old in t, "E"
t = t.replace(old, new)
old = """        UnregisterHotKey(hwnd, kAltShiftPlus);"""
new = """        UnregisterHotKey(hwnd, kAltShiftPlus);
        UnregisterHotKey(hwnd, 5);"""
assert old in t, "F"
t = t.replace(old, new)
old = """                    case HotkeyThread::kAltShiftPlus: scale_phone(1.10f); break;"""
new = """                    case HotkeyThread::kAltShiftPlus: scale_phone(1.10f); break;
                    case 5: scale_phone(1.0f / 1.10f); break;"""
assert old in t, "G"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("alt+physical-plus fixed")
