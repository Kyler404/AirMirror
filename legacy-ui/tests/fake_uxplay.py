#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""假 UxPlay：只打印日志，用于验证进程管控链路（不参与正式逻辑）"""
import sys
import time

print("uxplay (fake) argv:", " ".join(sys.argv[1:]), flush=True)
for i in range(1, 100000):
    print("[fake] frame %d  running" % i, flush=True)
    time.sleep(0.5)
