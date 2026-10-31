#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""放大键语义修正：主键盘 '=' 需 Alt+Shift（真「+」）；小键盘 +/- 热键。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 主键盘 '='：必须带 Shift（即真正的「+」）才放大
old = """                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.10f);
                        break;"""
new = """                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.10f);
                        break;
                    case SDLK_EQUALS:
                        // 主键盘的「+」= Shift+'='：要求 Alt+Shift 才算
                        if ((e.key.keysym.mod & KMOD_ALT) &&
                            (e.key.keysym.mod & KMOD_SHIFT)) scale_phone(1.10f);
                        break;"""
assert old in t, "A"
t = t.replace(old, new)

# 2) 热键注册改小键盘 + / -
old = "RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0xBB);  // '='"
assert old in t, "B1"
t = t.replace(old, "RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +")
old = "RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0xBD);  // '-'"
assert old in t, "B2"
t = t.replace(old, "RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -")

# 3) 帮助文案
old = 'ImGui::BulletText("Alt+= / Alt+-    放大 / 缩小画面");'
assert old in t, "C"
t = t.replace(old, 'ImGui::BulletText("Alt+加(+) / Alt+减(-)    放大 / 缩小画面");')

p.write_text(t, encoding="utf-8")
print("plus/minus semantics fixed")
