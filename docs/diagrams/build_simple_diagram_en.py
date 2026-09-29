# -*- coding: utf-8 -*-
"""ENGLISH variant of the SIMPLE system block diagram, for the public
article (article/index.html). Mirrors build_simple_diagram.py exactly —
same layout, same palette, English labels. The Chinese original stays
the repo-internal version (see README in this folder).

Output: system_architecture_simple_en.zcode.png / .svg next to this script.

Author: zcode - 2026-09-25
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams["font.family"] = ["DejaVu Sans", "Microsoft YaHei", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False

C = {
    "llm_bg": "#EDE7F6", "llm_ec": "#7E57C2",
    "host_bg": "#E3F2FD", "host_ec": "#1E88E5",
    "dongle_bg": "#E8F5E9", "dongle_ec": "#43A047",
    "ble_bg": "#FFF3E0", "ble_ec": "#FB8C00",
    "box": "#FFFFFF", "ink": "#263238", "arrow": "#455A64", "usb": "#37474F",
}

FIG_W, FIG_H = 16.0, 9.0
DPI = 150

fig = plt.figure(figsize=(FIG_W, FIG_H))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 100)
ax.set_ylim(0, 100)
ax.axis("off")
ax.add_patch(plt.Rectangle((0, 0), 100, 100, fc="white", ec="none"))


def box(x, y, w, h, title, lines, fc, ec, tsize=13, lsize=9.5):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0,rounding_size=0.9",
        fc=fc, ec=ec, lw=2.0, mutation_aspect=FIG_W / FIG_H))
    ax.text(x + w / 2, y + h - 2.0, title, ha="center", va="center",
            fontsize=tsize, fontweight="bold", color=ec)
    top = y + h - 5.6
    for i, ln in enumerate(lines):
        ax.text(x + w / 2, top - i * 3.0, ln, ha="center", va="center",
                fontsize=lsize, color=C["ink"])


def arrow(x1, y1, x2, y2, style="-|>", color=None, lw=2.4, ls="-",
          mutation=20):
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle=style, color=color or C["arrow"],
        lw=lw, linestyle=ls, mutation_scale=mutation, zorder=5,
        shrinkA=2, shrinkB=2))


def label(x, y, text, color, size=9.5, weight="normal", ha="center"):
    ax.text(x, y, text, ha=ha, va="center", fontsize=size, color=color,
            fontweight=weight, zorder=6)


# ---- title ----
label(50, 97, "ESP32-S3 BLE Bridge System · Simple Architecture",
      C["ink"], size=17, weight="bold")
label(50, 93.6, "LLM cloud / PC host (3 sub-blocks) / USB dongle / BLE peers",
      "#78909C", size=8.5)

# ---- four blocks ----
box(2, 40, 15, 44, "LLM (cloud)",
    ["Chat Completions", "(OpenAI-compatible)", "",
     "in: data snapshots", "     + tool catalog",
     "out: answers", "     Lua code", "     tool calls"],
    C["llm_bg"], C["llm_ec"], tsize=14, lsize=9.5)

# PC Host domain with 3 sub-blocks
ax.add_patch(FancyBboxPatch(
    (22, 28), 24, 59, boxstyle="round,pad=0,rounding_size=1.2",
    fc=C["host_bg"], ec=C["host_ec"], lw=2.2, alpha=0.6,
    mutation_aspect=FIG_W / FIG_H))
ax.text(34, 85.0, "PC Host (Python)", ha="center", va="center",
        fontsize=14, fontweight="bold", color=C["host_ec"])
box(23.5, 69.5, 21, 13, "1 · BLE server simulation",
    ["run_case.py: first-order plant", "y += (dt/tau)*(K*u - y)",
     "PC = GATT server, generates data"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)
box(23.5, 50.2, 21, 16.3, "2 · User interface",
    ["assistant.py: chat session", "llm_loop.py: batch loop",
     "Lua deploy (human-confirmed)", "tool calls / results"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)
box(23.5, 30.5, 21, 18.5, "3 · USB link",
    ["pyserial · COM12",
     "down: commands + Lua", "up: JSON data stream",
     "port held open throughout"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)

box(52, 40, 20, 44, "USB Dongle",
    ["ESP32-S3 firmware", "",
     "passive BLE scan + GATT", "AD parse -> C filters",
     "-> Lua hooks -> JSON lines", "LittleFS: scripts/packs"],
    C["dongle_bg"], C["dongle_ec"], tsize=14, lsize=9.5)

box(79, 40, 19, 44, "BLE peers",
    ["any BLE device", "",
     "advertisements", "GATT server (notify/read)",
     "a real sensor - or a PC", "simulated one (run_case)"],
    C["ble_bg"], C["ble_ec"], tsize=14, lsize=9.5)

# ---- main arrows ----
Y = 60
arrow(17.6, Y, 21.4, Y, style="<|-|>", lw=2.8, mutation=22)
label(19.5, 65.5, "HTTPS", C["llm_ec"], size=10, weight="bold")
label(19.5, 55.0, "analysis /\nLua + tools", C["llm_ec"], size=8.5)

arrow(46.6, Y, 51.4, Y, style="<|-|>", lw=3.2, color=C["usb"], mutation=24)
label(49, 65.5, "USB CDC", C["usb"], size=10, weight="bold")
label(49, 54.8, "^ JSON stream\nv cmds / Lua", C["usb"], size=8.5)

arrow(72.6, Y, 78.4, Y, style="<|-|>", lw=2.8, color=C["ble_ec"],
      mutation=22)
label(75.5, 65.5, "BLE radio", C["ble_ec"], size=10, weight="bold")
label(75.5, 54.8, "adverts /\nGATT data", C["ble_ec"], size=8.5)

# ---- dashed simulation loop: PC's simulated GATT server -> the dongle ----
arrow(41, 82.6, 41, 90.8, lw=2.0, ls=(0, (5, 3)), color="#8E24AA",
      mutation=16)
ax.add_patch(FancyArrowPatch((41, 90.8), (62, 90.8), arrowstyle="-",
             color="#8E24AA", lw=2.0, linestyle=(0, (5, 3)), zorder=5))
arrow(62, 90.8, 62, 84.6, lw=2.0, ls=(0, (5, 3)), color="#8E24AA",
      mutation=16)
label(51.5, 88.0, "simulation: PC = GATT server (peripheral)\n"
                  "dongle (client) connects over the air",
      "#8E24AA", size=8.8, weight="bold")

# ---- footer ----
label(50, 22, "Closed loop: the LLM analyzes the stream -> generates/calls "
              "Lua -> deploys it to the dongle -> the stream changes -> repeat",
      "#546E7A", size=10, weight="bold")
label(50, 17.5, "adv plane (scan pipeline) and conn plane (GATT re-stream) "
                "share one USB JSON stream; Lua tools register via manifest() "
                "as LLM-callable tools",
      "#78909C", size=8.8)

out = os.path.dirname(os.path.abspath(__file__))
fig.savefig(os.path.join(out, "system_architecture_simple_en.zcode.png"),
            dpi=DPI, facecolor="white")
fig.savefig(os.path.join(out, "system_architecture_simple_en.zcode.svg"),
            facecolor="white")
print("written:", os.path.join(out, "system_architecture_simple_en.zcode.png"))
