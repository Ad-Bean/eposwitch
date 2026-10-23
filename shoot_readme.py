# -*- coding: utf-8 -*-
"""Capture English-UI screenshots for the GitHub README."""
import os
import time
import ctypes
import subprocess
import threading
from ctypes import wintypes

from PIL import ImageGrab

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, 'docs', 'img')
user32 = ctypes.windll.user32
WM_CHAR = 0x0102
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0200, 0x0201, 0x0202
MK_LBUTTON = 1


def find():
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        b = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(h, b, 256)
        if b.value == 'EpoSwitchUI':
            r = wintypes.RECT()
            user32.GetWindowRect(h, ctypes.byref(r))
            out.append((h, r))
        return True

    user32.EnumWindows(cb, 0)
    return out


def lp(x, y):
    return (int(y) << 16) | (int(x) & 0xFFFF)


def click(h, x, y, t=0.5):
    user32.PostMessageW(h, WM_MOUSEMOVE, 0, lp(x, y))
    time.sleep(0.05)
    user32.PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, lp(x, y))
    time.sleep(0.05)
    user32.PostMessageW(h, WM_LBUTTONUP, 0, lp(x, y))
    time.sleep(t)


def shot(r, name):
    p = os.path.join(IMG, name)
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom)).save(p)
    print(' ', name)


def nav(h, i, t=0.7):
    click(h, 60, 60 + 14 + i * 42 + 19, t)


p = subprocess.Popen([os.path.join(HERE, 'eposwitch.exe')], cwd=HERE)
time.sleep(11)
h, r = find()[0]

nav(h, 0)                       # Overview
shot(r, 'overview.png')

nav(h, 1)                       # Sound (EQ)
shot(r, 'sound.png')

nav(h, 3)                       # Devices
time.sleep(0.5)
shot(r, 'devices.png')

# Mixer with a live session (beep thread)
stop = threading.Event()
def beep():
    try:
        import winsound
        while not stop.is_set():
            winsound.Beep(880, 900)
    except Exception:
        pass
t = threading.Thread(target=beep, args=(stop,), daemon=True)
t.start()
nav(h, 4)
time.sleep(2.5)
nav(h, 4)
time.sleep(1.2)
shot(r, 'mixer.png')
stop.set()

# App rules: capture foreground + type + pick preset
nav(h, 5)
time.sleep(0.6)
click(h, 690, 269)              # "Type a process name" button
time.sleep(0.3)
for ch in 'cs2.exe':
    user32.PostMessageW(h, WM_CHAR, ord(ch), 0)
    time.sleep(0.02)
user32.PostMessageW(h, WM_CHAR, 13, 0)
time.sleep(0.4)
click(h, 744, 227)              # choose preset dropdown
time.sleep(0.5)
shot(r, 'apps.png')
user32.PostMessageW(h, WM_MOUSEMOVE, 0, lp(560, 500))   # 收起下拉
time.sleep(0.4)

# Settings (hotkeys)
nav(h, 7)
time.sleep(0.6)
shot(r, 'settings.png')

p.kill()
print('done')
