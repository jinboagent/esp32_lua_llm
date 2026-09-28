# -*- coding: utf-8 -*-
"""Host-side LLM block diagram for article/index.html — the message-Prompt
loop: device + user inputs -> MESSAGE Prompt -> context assembly -> cloud LLM
-> typed envelopes routed back (answers, gated Lua, capped tool programs).

English labels, palette matched to build_simple_diagram_en.py.
Output: host_llm_message_prompt_en.zcode.png / .svg next to this script.

Author: zcode - 2026-09-28
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
    "user_bg": "#ECEFF1", "user_ec": "#546E7A",
    "gate_bg": "#FDEFE7", "gate_ec": "#E8590C",
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


def box(x, y, w, h, title, lines, fc, ec, tsize=13, lsize=9.5, tdy=2.0,
        gap=3.0):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0,rounding_size=0.9",
        fc=fc, ec=ec, lw=2.0, mutation_aspect=FIG_W / FIG_H))
    ax.text(x + w / 2, y + h - tdy, title, ha="center", va="center",
            fontsize=tsize, fontweight="bold", color=ec)
    top = y + h - tdy - 3.6
    for i, ln in enumerate(lines):
        ax.text(x + w / 2, top - i * gap, ln, ha="center", va="center",
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
label(50, 97, "Host-side LLM — the message-Prompt loop", C["ink"], size=17,
      weight="bold")
label(50, 93.6, "device lines + user input -> one prompt -> context assembly "
                "-> cloud LLM -> typed envelopes routed back",
      "#78909C", size=8.5)

# ---- inputs ----
box(2, 58, 15, 22, "ESP32-S3 dongle",
    ["adv JSON lines", '"src":"conn" lines', "USB CDC · streaming",
     "Ctrl+C = interrupt"],
    C["dongle_bg"], C["dongle_ec"], tsize=13, lsize=9)
box(2, 34, 15, 20, "User",
    ["console input", "free text or /commands", "answers clarify",
     'confirms deploy? [y/N]'],
    C["user_bg"], C["user_ec"], tsize=13, lsize=9)

# ---- stage 1: MESSAGE Prompt ----
box(25, 40, 21, 42, "MESSAGE Prompt",
    ["assistant.py", "",
     "ingress validation", "bounded buffers", "(20 adv + 30 conn)",
     "rolling history", "routes typed replies", "back to you"],
    C["box"], C["host_ec"], tsize=13.5, lsize=9.5)

# ---- stage 2: context assembly ----
box(53, 43, 18, 36, "Context assembly",
    ["history", "device snapshot", "tool catalog", "(registered packs)",
     "", "ONE request:"], C["host_bg"], C["host_ec"], tsize=13, lsize=9.5)
label(62, 45.5, "system + history + snapshot", C["host_ec"], size=8.5)

# ---- stage 3: cloud LLM ----
box(78, 43, 20, 36, "Cloud LLM",
    ["OpenAI-compatible", "(Qwen · DeepSeek ·", "Ollama ...)", "",
     "replies ONLY in", "typed JSON envelopes"],
    C["llm_bg"], C["llm_ec"], tsize=13.5, lsize=9.5)

# ---- main flow arrows ----
arrow(17.5, 72, 24.4, 72, lw=2.8, mutation=22)
label(21, 75.0, "USB", C["dongle_ec"], size=9.5, weight="bold")
arrow(17.5, 47, 24.4, 47, lw=2.4, mutation=18)
label(21, 49.8, "console input", C["user_ec"], size=9)
arrow(46.6, 64, 52.4, 64, lw=2.8, mutation=22)
label(49.5, 67.0, "prompt", C["host_ec"], size=9.5, weight="bold")
arrow(71.6, 61, 77.4, 61, lw=2.8, mutation=22)
label(74.5, 64.0, "context", C["host_ec"], size=9.5, weight="bold")

# ---- envelope bus (return paths) ----
arrow(88, 42.4, 88, 34, lw=2.6, mutation=20, color=C["llm_ec"])
ax.add_patch(FancyArrowPatch((19, 34), (88, 34), arrowstyle="-",
             color=C["llm_ec"], lw=2.2, zorder=5))
label(53.5, 36.2, "typed envelopes: answer · clarify · lua · error",
      C["llm_ec"], size=10, weight="bold")

box(8, 12, 20, 16, "answer · clarify",
    ["shown to you", "clarify asks you back", "error: session survives"],
    C["box"], C["user_ec"], tsize=11.5, lsize=9)
arrow(18, 28.2, 18, 34, style="-", color=C["llm_ec"], lw=2.2)

box(35, 12, 28, 16, "lua  (filter / transform / pack)",
    ["deploy? [y/N]  human gate", "SCRIPT LOAD - per-line sandbox scan",
     "device whitelist has final word (-612)"],
    C["gate_bg"], C["gate_ec"], tsize=11.5, lsize=9)
arrow(49, 28.2, 49, 34, style="-", color=C["llm_ec"], lw=2.2)

box(70, 12, 24, 16, "tool program",
    ["runs on the dongle", "max 3 consecutive runs", "results = next context"],
    C["box"], C["llm_ec"], tsize=11.5, lsize=9)
arrow(82, 28.2, 82, 34, style="-", color=C["llm_ec"], lw=2.2)

# results feed back into context assembly (dashed)
arrow(82, 34, 82, 39.5, lw=2.0, ls=(0, (5, 3)), color="#8E24AA", mutation=16)
ax.add_patch(FancyArrowPatch((82, 39.5), (62, 39.5), arrowstyle="-",
             color="#8E24AA", lw=2.0, linestyle=(0, (5, 3)), zorder=5))
arrow(62, 39.5, 62, 42.4, lw=2.0, ls=(0, (5, 3)), color="#8E24AA",
      mutation=16)
label(72, 41.2, "results", "#8E24AA", size=8.8, weight="bold")

# ---- footer ----
label(50, 5.5, "Safety chain: strict typed-JSON parse -> human deploy? [y/N] "
               "-> per-line sandbox scan -> device whitelist (-612) · "
               "tool autonomy capped at 3 runs/turn",
      "#546E7A", size=10, weight="bold")
label(50, 2.2, "the dongle keeps streaming the whole time — every turn carries "
               "a fresh device snapshot (N3: port stays open)",
      "#78909C", size=8.8)

out = os.path.dirname(os.path.abspath(__file__))
fig.savefig(os.path.join(out, "host_llm_message_prompt_en.zcode.png"),
            dpi=DPI, facecolor="white")
fig.savefig(os.path.join(out, "host_llm_message_prompt_en.zcode.svg"),
            facecolor="white")
print("written:", os.path.join(out, "host_llm_message_prompt_en.zcode.png"))
