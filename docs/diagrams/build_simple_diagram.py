# -*- coding: utf-8 -*-
"""SIMPLE system block diagram (user-requested abstraction level, mirroring
harness/00-global-context/project_overview.md): LLM cloud / PC Host (3
sub-blocks) / USB Dongle / BLE client, one dashed loop for the simulated
BLE peripheral. Companion of the detailed system_architecture diagram.

Output: system_architecture_simple.zcode.png / .svg next to this script.

Author: zcode - 2026-09-16
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams["font.family"] = ["Microsoft YaHei", "SimHei", "sans-serif"]
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
label(50, 97, "ESP32-S3 BLE 桥接系统 · 简明架构图", C["ink"], size=17,
      weight="bold")
label(50, 93.6, "与 harness/00-global-context/project_overview.md 同层级 · "
                "细节版见 docs/diagrams/system_architecture.zcode.png",
      "#78909C", size=8.5)

# ---- four blocks ----
box(2, 40, 15, 44, "LLM 云端",
    ["Chat Completions", "(OpenAI 兼容 API)",
     "",
     "输入:数据快照", "      + 工具目录",
     "输出:分析结论", "      Lua 代码", "      工具调用"],
    C["llm_bg"], C["llm_ec"], tsize=14, lsize=9.5)

# PC Host domain with 3 sub-blocks
ax.add_patch(FancyBboxPatch(
    (22, 28), 24, 59, boxstyle="round,pad=0,rounding_size=1.2",
    fc=C["host_bg"], ec=C["host_ec"], lw=2.2, alpha=0.6,
    mutation_aspect=FIG_W / FIG_H))
ax.text(34, 85.0, "PC Host(Python)", ha="center", va="center",
        fontsize=14, fontweight="bold", color=C["host_ec"])
box(23.5, 69.5, 21, 13, "① BLE 数据仿真",
    ["run_case.py:一阶系统", "y += (Δt/τ)·(K·u − y)",
     "WinRT GATT server 扮演外设"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)
box(23.5, 51.5, 21, 15, "② 用户界面处理",
    ["assistant.py 交互会话", "llm_loop.py 批处理回路",
     "Lua 部署(人工确认)", "工具调用 / 结果管理"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)
box(23.5, 30.5, 21, 18.5, "③ USB 连接",
    ["pyserial · COM12",
     "下行:命令 + Lua 脚本", "上行:JSON 数据流",
     "端口全程保持打开"],
    C["box"], C["host_ec"], tsize=11.5, lsize=9)

box(52, 40, 20, 44, "USB Dongle",
    ["ESP32-S3 固件",
     "",
     "BLE 被动扫描 + GATT 连接", "AD 解析 → C 过滤", "→ Lua 钩子 → JSON 行",
     "LittleFS 存脚本/工具包"],
    C["dongle_bg"], C["dongle_ec"], tsize=14, lsize=9.5)

box(79, 40, 19, 44, "BLE 客户端",
    ["任意 BLE 设备",
     "",
     "advertisement 广播", "GATT 外设(notify/read)",
     "真实传感器,或 PC 仿真", "(WinRT 扮演,见虚线)"],
    C["ble_bg"], C["ble_ec"], tsize=14, lsize=9.5)

# ---- main arrows ----
Y = 60
arrow(17.6, Y, 21.4, Y, style="<|-|>", lw=2.8, mutation=22)
label(19.5, 65.5, "HTTPS", C["llm_ec"], size=10, weight="bold")
label(19.5, 55.0, "分析请求 /\nLua · 工具调用", C["llm_ec"], size=8.5)

arrow(46.6, Y, 51.4, Y, style="<|-|>", lw=3.2, color=C["usb"], mutation=24)
label(49, 65.5, "USB CDC", C["usb"], size=10, weight="bold")
label(49, 54.8, "↑ JSON 数据流\n↓ 命令 / Lua", C["usb"], size=8.5)

arrow(72.6, Y, 78.4, Y, style="<|-|>", lw=2.8, color=C["ble_ec"],
      mutation=22)
label(75.5, 65.5, "BLE 空口", C["ble_ec"], size=10, weight="bold")
label(75.5, 54.8, "广播接收 /\nGATT 连接", C["ble_ec"], size=8.5)

# ---- dashed simulation loop: BLE data simulation -> BLE client ----
arrow(34, 82.6, 34, 90.8, lw=2.0, ls=(0, (5, 3)), color="#8E24AA",
      mutation=16)
ax.add_patch(FancyArrowPatch((34, 90.8), (88.5, 90.8), arrowstyle="-",
             color="#8E24AA", lw=2.0, linestyle=(0, (5, 3)), zorder=5))
arrow(88.5, 90.8, 88.5, 84.6, lw=2.0, ls=(0, (5, 3)), color="#8E24AA",
      mutation=16)
label(61, 92.6, "仿真回环:PC 模拟的 BLE 数据经 WinRT GATT 广播上空口 → "
                "dongle 接收重流 → USB 回到 PC",
      "#8E24AA", size=8.8, weight="bold")

# ---- footer ----
label(50, 22, "闭环:LLM 分析数据流 → 生成/调用 Lua → 部署回 dongle → "
              "改变数据流 → 再分析",
      "#546E7A", size=10, weight="bold")
label(50, 17.5, "adv 面(扫描流水线)与 conn 面(GATT 重流)共用同一条 USB JSON 流;"
                "Lua 工具经 manifest() 注册为 LLM 可调用工具",
      "#78909C", size=8.8)

out = os.path.dirname(os.path.abspath(__file__))
fig.savefig(os.path.join(out, "system_architecture_simple.zcode.png"),
            dpi=DPI, facecolor="white")
fig.savefig(os.path.join(out, "system_architecture_simple.zcode.svg"),
            facecolor="white")
print("written:", os.path.join(out, "system_architecture_simple.zcode.png"))
