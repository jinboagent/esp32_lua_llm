# -*- coding: utf-8 -*-
"""System architecture diagram for the ESP32-S3 BLE bridge dongle x LLM
framework. Generates a detailed, four-domain block diagram plus two focus
panels (Lua tool registration loop; first-order plant estimation case).

Output: system_architecture.zcode.png / .svg next to this script.

Author: zcode - 2026-09-16
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams["font.family"] = ["Microsoft YaHei", "SimHei", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False

# ---- palette ---------------------------------------------------------------
C = {
    "llm_bg": "#EDE7F6", "llm_ec": "#7E57C2",
    "host_bg": "#E3F2FD", "host_ec": "#1E88E5",
    "dongle_bg": "#E8F5E9", "dongle_ec": "#43A047",
    "adv_fc": "#F1F8E9", "adv_ec": "#7CB342",
    "conn_fc": "#E0F7FA", "conn_ec": "#00ACC1",
    "lua_fc": "#FFF8E1", "lua_ec": "#F9A825",
    "ble_bg": "#FFF3E0", "ble_ec": "#FB8C00",
    "panel_bg": "#FAFAFA", "panel_ec": "#9E9E9E",
    "box_fc": "#FFFFFF", "ink": "#263238", "arrow": "#455A64",
    "usb": "#37474F",
}

FIG_W, FIG_H = 24.0, 15.0
DPI = 150

fig = plt.figure(figsize=(FIG_W, FIG_H))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 100)
ax.set_ylim(0, 100)
ax.axis("off")
ax.add_patch(plt.Rectangle((0, 0), 100, 100, fc="white", ec="none"))


# ---- helpers ---------------------------------------------------------------
def box(x, y, w, h, title, lines, fc, ec, tsize=9.5, lsize=7.6,
        title_color=None, lw=1.4, rad=0.6, title_dy=None):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle=f"round,pad=0,rounding_size={rad}",
        fc=fc, ec=ec, lw=lw, mutation_aspect=FIG_W / FIG_H))
    tc = title_color or ec
    dy = title_dy if title_dy is not None else h - 1.15
    ax.text(x + w / 2, y + dy, title, ha="center", va="center",
            fontsize=tsize, fontweight="bold", color=tc)
    if lines:
        top = y + h - (2.2 if title_dy is None else title_dy - 0.9)
        step = 1.62
        for i, ln in enumerate(lines):
            ax.text(x + w / 2, top - i * step, ln, ha="center", va="center",
                    fontsize=lsize, color=C["ink"])
    return (x, y, w, h)


def domain(x, y, w, h, label, bg, ec):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0,rounding_size=1.2",
        fc=bg, ec=ec, lw=1.8, alpha=0.55, mutation_aspect=FIG_W / FIG_H))
    ax.text(x + w / 2, y + h - 1.5, label, ha="center", va="center",
            fontsize=12.5, fontweight="bold", color=ec)


def arrow(x1, y1, x2, y2, label=None, style="-|>", color=None, lw=2.0,
          ls="-", lpos=0.5, ldy=1.1, lsize=7.6, lcolor=None, ha="center",
          mutation=16, zorder=5):
    color = color or C["arrow"]
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle=style, color=color, lw=lw,
        linestyle=ls, mutation_scale=mutation, zorder=zorder,
        shrinkA=1, shrinkB=1))
    if label:
        lx = x1 + (x2 - x1) * lpos
        ly = y1 + (y2 - y1) * lpos + ldy
        ax.text(lx, ly, label, ha=ha, va="center", fontsize=lsize,
                color=lcolor or color, style="italic", zorder=6)


def side_label(x, y, text, color, lsize=7.6, weight="normal", ha="left"):
    ax.text(x, y, text, ha=ha, va="center", fontsize=lsize, color=color,
            style="italic", fontweight=weight, zorder=6)


# ---- title -----------------------------------------------------------------
ax.text(50, 98.4, "ESP32-S3 BLE 桥接 Dongle × LLM 系统架构(全链路数据流 + 工具闭环)",
        ha="center", va="center", fontsize=17.5, fontweight="bold",
        color=C["ink"])
ax.text(50, 95.9, "adv plane: 被动扫描 → 解析 → C 过滤 → Lua 钩子 → JSON ·  "
                  "conn plane: GATT 中央连接重流(绕过 Lua 钩子)·  "
                  "LLM: 分析流 / 写 Lua / 调工具   ·  zcode 2026-09-16",
        ha="center", va="center", fontsize=8.8, color="#607D8B")

# =========================================================================
# MAIN DIAGRAM (upper area, y 32..94)
# =========================================================================

# ---- domain 1: LLM cloud ---------------------------------------------------
domain(1.5, 47, 14.5, 47, "LLM(云端)", C["llm_bg"], C["llm_ec"])
box(2.8, 82.5, 12, 8.2, "Chat Completions",
    ["OpenAI 兼容 /chat/completions",
     "qwen · OpenAI · …(密钥来自 .llm_env)",
     "temperature 0.2 · JSON 输出约束"],
    C["box_fc"], C["llm_ec"], tsize=9)
box(2.8, 69.5, 12, 10.5, "模型的三类输出",
    ["① 类型化信封 answer | lua |",
     "    clarify | error(assistant)",
     "② tool_calls JSON(native tools)",
     "③ 估计 JSON {tau_est, k_est,",
     "    step_at_est, method}(run_case)"],
    C["box_fc"], C["llm_ec"], tsize=9)
box(2.8, 57.5, 12, 10, "模型的输入",
    ["system prompt + 设备数据快照",
     "tools 数组(JSON Schema)",
     "role:\"tool\" 结果 / 工具程序结果",
     "广播/连接流样本(≤60 行)"],
    C["box_fc"], C["llm_ec"], tsize=9)
box(2.8, 49, 12, 6.5, "凭据约定",
    ["环境变量按组优先,.llm_env 兜底;",
     "401→文件自愈,404→/compatible-mode"],
    C["llm_bg"], C["llm_ec"], tsize=8.5)

# ---- domain 2: PC host -------------------------------------------------------
domain(18, 31, 22.5, 63, "PC Host(Python,自包含脚本)", C["host_bg"], C["host_ec"])
box(19.2, 78.5, 20, 12.8, "assistant.py — 交互会话",
    ["信封协议路由;type:\"lua\" → 展示代码",
     "人工确认 y/N → SCRIPT LOAD 逐行上传",
     "→ SCRIPT END(试编译)→ SCRIPT RUN",
     "/tools load · PACK · --native-tools",
     "--mutating-gate(变更工具逐次确认)"],
    C["box_fc"], C["host_ec"], tsize=9.5)
box(19.2, 64.5, 20, 12.3, "llm_loop.py — 批处理回路",
    ["capture 采集 JSON 流 → analyze(LLM)",
     "→ 生成 on_adv / transform 脚本",
     "→ deploy(SCRIPT LOAD/RUN)",
     "→ verify:部署前后输出流对照"],
    C["box_fc"], C["host_ec"], tsize=9.5)
box(19.2, 48.5, 20, 14.3, "run_case.py — 仿真 case(H5.2)",
    ["FirstOrderCase:欧拉积分 plant",
     "   y += (Δt/τ)·(K·u − y),u 阶跃 0→1",
     "GattPeer:WinRT GATT server 扮演外设",
     "每 interval 秒 notify {\"t\",\"u\",\"y\"}",
     "check():63.2% 时间常数点/稳态校验",
     "--estimate:LLM 估计 τ,K 对真值判分"],
    C["box_fc"], C["host_ec"], tsize=9.5)
box(19.2, 32.8, 20, 14.2, "公共层(约定共享,代码各持副本)",
    ["LLM 客户端:凭据解析 / 401、404 自愈",
     " / QWEN_ENABLE_THINKING",
     "工具注册表镜像:manifest 缓存 →",
     " tools_array(JSON Schema)→ table 字面量",
     "串口会话:pyserial COM12,N3 全程保持",
     "打开(关口即复位芯片)"],
    C["host_bg"], C["host_ec"], tsize=9)

# ---- domain 3: dongle ---------------------------------------------------------
domain(42, 31, 30.5, 63, "USB Dongle(ESP32-S3 固件)", C["dongle_bg"], C["dongle_ec"])
box(43.2, 85, 28, 7.2, "usb(CDC 控制台)+ cli(命令分发)",
    ["状态机 IDLE / SCANNING / SCRIPT_RUNNING · 每条响应严格 JSON",
     "命令:SCAN · FILTER · SCRIPT · LUA · PACK · CONN · POWER · STATUS"],
    C["box_fc"], C["dongle_ec"], tsize=9.5, lsize=7.4)

# adv pipeline mini-boxes
ax.text(44.6, 82.6, "adv 数据面(被动扫描流水线,每条广播)", ha="left",
        va="center", fontsize=8.6, fontweight="bold", color=C["adv_ec"])
adv_steps = [("ble_scan", "NimBLE 被动扫描", "INTERVAL 调节"),
             ("proto 解析", "AD 结构拆解", "name/uuid/manu"),
             ("C filter", "NAME/UUID/RSSI", "MAC,同或异与"),
             ("Lua 钩子", "on_adv 抑制", "transform 改写"),
             ("json_enc", "严格 JSON 行", "envelope"),
             ("usb 输出", "{\"src\":\"adv\"}", "每行一对象")]
aw, ag = 4.42, 0.42
for i, (t1, t2, t3) in enumerate(adv_steps):
    bx = 43.4 + i * (aw + ag)
    box(bx, 75.6, aw, 6.4, t1, [t2, t3], C["adv_fc"], C["adv_ec"],
        tsize=8.2, lsize=7.2)
    if i < len(adv_steps) - 1:
        arrow(bx + aw, 78.8, bx + aw + ag, 78.8, lw=1.5, mutation=11)
side_label(43.4, 74.2, "Lua 无脚本时:存在性缓存,零开销旁路", C["adv_ec"], lsize=7.3)

# conn plane
ax.text(44.6, 72.2, "conn 数据面(GATT 中央,连接任意 peer,重流)", ha="left",
        va="center", fontsize=8.6, fontweight="bold", color=C["conn_ec"])
conn_steps = [("ble_conn", "CONN TARGET(UUID)", "连接+svc/chr/CCCD 发现"),
              ("订阅/轮询", "CCCD notify", "或 INTERVAL 轮询 read"),
              ("json_encode_conn", "payload 合并入 envelope", "{\"src\":\"conn\"}"),
              ("usb 输出", "与 adv 同流", "绕过 Lua 钩子(仅分析)")]
cw, cg = 6.9, 0.55
for i, (t1, t2, t3) in enumerate(conn_steps):
    bx = 43.4 + i * (cw + cg)
    box(bx, 65.2, cw, 6.4, t1, [t2, t3], C["conn_fc"], C["conn_ec"],
        tsize=8.2, lsize=7.2)
    if i < len(conn_steps) - 1:
        arrow(bx + cw, 68.4, bx + cw + cg, 68.4, lw=1.5, mutation=11)

# Lua runtime row
ax.text(44.6, 60.8, "Lua 运行时 + 存储(工具/钩子/脚本)", ha="left", va="center",
        fontsize=8.6, fontweight="bold", color=C["lua_ec"])
lua_steps = [("lua engine", "白名单沙箱·静态内存池", "指令/内存预算·引擎锁"),
             ("hw.* 绑定", "gpio · adc · kv · millis", "configure 模式掉电保存"),
             ("script_mgmt", "上传逐行 fail-closed 扫描", "试编译→LittleFS→RUN/STOP"),
             ("pack_store", "PACK CLI·LittleFS", "boot autorun(重启复原工具)"),
             ("工具全局 + manifest()", "约定名即注册", "单 table 参数约定")]
lw_, lg = 5.45, 0.5
for i, (t1, t2, t3) in enumerate(lua_steps):
    bx = 43.4 + i * (lw_ + lg)
    box(bx, 52.4, lw_, 6.9, t1, [t2, t3], C["lua_fc"], C["lua_ec"],
        tsize=7.9, lsize=7.0)
side_label(43.4, 50.9, "并发:CLI 任务(改)/ pipeline 任务(评估)/ conn_worker(阻塞收尾),各持其锁",
           C["lua_ec"], lsize=7.3)
box(43.2, 32.8, 28, 9.2, "storage — LittleFS",
    ["/littlefs/script.lua(钩子脚本,文本持久,重启后 RUN 重新注册)",
     "/littlefs/packs/*(工具包 + .autorun 标记,boot 时重放注册)",
     "注册态在 RAM(lua_State 全局表),脚本文本在 flash —— 重启=注销但不清盘"],
    C["box_fc"], C["dongle_ec"], tsize=9, lsize=7.4)

# ---- domain 4: BLE environment ------------------------------------------------
domain(74.5, 31, 24, 63, "BLE 环境(2.4 GHz 空口)", C["ble_bg"], C["ble_ec"])
box(75.8, 63.8, 21.4, 22.7, "任意 BLE 设备 ×N(广播者)",
    ["advertisement 广播帧(单向,无连接)",
     " · 完整/缩短 name",
     " · UUID16 列表(如 180A)",
     " · 厂商数据 manu_id + manu_data",
     " · RSSI / 地址类型 public·random",
     "温度计、手环、信标、手机……",
     "dongle 被动接收,不发射、不连接"],
    C["box_fc"], C["ble_ec"], tsize=9.5)
box(75.8, 44, 21.4, 15.5, "任意 GATT 外设(可连接)",
    ["notify / indicate / read 特征值",
     "由 dongle 发起连接(CONN TARGET",
     "按服务 UUID 自动发现,或直连地址)",
     "其中一环 = PC 自己:WinRT",
     "GattServiceProvider 广播的可连接",
     "外设(run_case 仿真对象,见虚线)"],
    C["box_fc"], C["ble_ec"], tsize=9.5)
box(75.8, 32.8, 21.4, 8.8, "同板双角色",
    ["被动扫描(adv 面)+ 单连接中央角色",
     "(conn 面);一个 GAP 过程一次,",
     "连接期间暂停扫描,完成即恢复"],
    C["ble_bg"], C["ble_ec"], tsize=9)

# ---- main arrows --------------------------------------------------------------
# LLM <-> host
arrow(15.2, 74.8, 18.8, 74.8, lw=2.2)
side_label(17, 76.6, "① 信封 / tool_calls\n    / 估计 JSON", C["llm_ec"],
           lsize=7.2)
arrow(18.8, 62.5, 15.2, 62.5, lw=2.2)
side_label(17, 60.7, "prompt + 快照 / tools\n数组 / tool 结果", C["host_ec"],
           lsize=7.2)

# host <-> dongle USB bus
arrow(41.3, 68, 42.7, 68, style="<|-|>", lw=3.2, color=C["usb"], mutation=18)
side_label(42, 73.0, "USB CDC(COM12)全双工", C["usb"], lsize=8.2,
           weight="bold", ha="center")
side_label(42, 71.0, "↑ 流:{\"src\":\"adv\"} · {\"src\":\"conn\"} 行 + 响应",
           C["usb"], lsize=7.4, ha="center")
side_label(42, 66.2, "↓ 命令 + 脚本 / 工具程序 / 工具包文本", C["usb"],
           lsize=7.4, ha="center")

# BLE adv -> dongle (radio, one-way)
arrow(75.5, 76, 72.9, 76, lw=2.4, color=C["ble_ec"])
side_label(74.2, 77.5, "被动扫描\nadvertisement", C["ble_ec"], lsize=7.2)

# GATT peer <-> dongle conn plane
arrow(75.5, 54.5, 72.9, 54.5, style="<|-|>", lw=2.4, color=C["ble_ec"])
side_label(74.2, 56.2, "GATT connect\nnotify·indicate·read", C["ble_ec"],
           lsize=7.2)

# run_case plant -> GATT peer (PC plays peripheral) -- the loop the user asked for
arrow(36.0, 63.2, 81.5, 63.2, lw=2.0, ls=(0, (5, 3)), color="#8E24AA")
ax.add_patch(FancyArrowPatch((81.5, 63.2), (81.5, 59.9), arrowstyle="-|>",
             color="#8E24AA", lw=2.0, linestyle=(0, (5, 3)), mutation_scale=16,
             zorder=5))
side_label(58.8, 64.9, "仿真回环:PC 模拟一阶对象,经 WinRT GATT server 以真 BLE 外设身份接入空口,数据经 dongle 重流回 USB",
           "#8E24AA", lsize=7.2, weight="bold", ha="center")

# =========================================================================
# PANEL A: Lua tool registration loop (x 1.5..48.5, y 3..29)
# =========================================================================
ax.add_patch(FancyBboxPatch((1.5, 2.5), 47, 27.2,
             boxstyle="round,pad=0,rounding_size=1.0",
             fc=C["panel_bg"], ec=C["panel_ec"], lw=1.6,
             mutation_aspect=FIG_W / FIG_H))
ax.text(25, 28.1, "特写 A · Lua 工具的注册与调用闭环(H6.1:Lua = LLM 的公用工具)",
        ha="center", va="center", fontsize=12, fontweight="bold", color="#5E35B1")

box(2.6, 18.6, 13.8, 7.6, "① 加载与注册(约定即接口)",
    ["工具包 .lua:工具函数 + manifest()",
     "host /tools load 逐行 LUA EXEC,或",
     "PACK STORE 持久化 + boot autorun",
     "逐行 fail-closed 扫描(os./io.→-612)"],
    C["lua_fc"], C["lua_ec"], tsize=8.4, lsize=7.2)
box(18, 18.6, 13.8, 7.6, "② 发现:manifest 分片拉取",
    ["LUA EXEC 结果路径仅 256 B →",
     "string.sub 切片,逐片 hex 编码回传",
     "host 校验 JSON → 注册表镜像",
     "(工具名/参数/doc/mutating)"],
    C["box_fc"], C["lua_ec"], tsize=8.4, lsize=7.2)
box(33.4, 18.6, 13.8, 7.6, "③ 翻译成模型方言",
    ["tools_array():manifest → OpenAI",
     "tools 数组(JSON Schema,enum 保留)",
     "mutating 工具描述加 [mutating]",
     "chat 请求带 tools + tool_choice=auto"],
    C["llm_bg"], C["llm_ec"], tsize=8.4, lsize=7.2)

box(6.5, 9.6, 17.5, 7.6, "④a native tools(--native-tools)",
    ["LLM 发 tool_calls{name, args}",
     "→ lua_args_literal:args JSON 转单个",
     "Lua table 字面量 → return tool({...})",
     "→ LUA EXEC → role:\"tool\" 回填循环"],
    C["box_fc"], C["llm_ec"], tsize=8.4, lsize=7.2)
box(26.5, 9.6, 17.5, 7.6, "④b generate-and-execute(默认)",
    ["LLM 回 type:\"lua\" 工具程序",
     "单行→LUA EXEC;多行→LUA BEGIN/END",
     "(局部变量跨行存活)",
     "结果字符串回填,模型继续推理"],
    C["box_fc"], C["lua_ec"], tsize=8.4, lsize=7.2)
arrow(16.4, 22.4, 18, 22.4, lw=1.6, mutation=12)
arrow(31.8, 22.4, 33.4, 22.4, lw=1.6, mutation=12)
arrow(37.5, 18.6, 35, 17.2, lw=1.6, mutation=12)
arrow(14.5, 18.6, 16.5, 17.2, lw=1.6, mutation=12)

box(2.6, 3.6, 44.6, 5.0, "护栏:模型只产生协议文本,副作用全部挤过窄通道",
    ["--mutating-gate:mutating 工具逐次 y/N 确认 · TOOL_EXEC_CAP 每轮执行预算 + 轮数上限 · "
     "沙箱白名单 + 指令/内存预算",
     "与部署型互补:on_adv / transform 经 SCRIPT LOAD/RUN 常驻管线(数据触发);"
     "工具程序 / tool_calls 即写即跑(模型触发)"],
    "#EFEBF7", "#5E35B1", tsize=8.4, lsize=7)

# =========================================================================
# PANEL B: first-order estimation case (x 51.5..98.5, y 3..29)
# =========================================================================
ax.add_patch(FancyBboxPatch((51.5, 2.5), 47, 27.2,
             boxstyle="round,pad=0,rounding_size=1.0",
             fc=C["panel_bg"], ec=C["panel_ec"], lw=1.6,
             mutation_aspect=FIG_W / FIG_H))
ax.text(75, 28.1, "特写 B · 一阶系统估计仿真(run_case.py --case first_order --estimate)",
        ha="center", va="center", fontsize=12, fontweight="bold", color="#00695C")

box(52.6, 17.6, 13.5, 8.6, "① plant:一阶对象(Δt 增量仿真)",
    ["y += (Δt/τ)·(K·u − y)  欧拉积分",
     "u:t=step_at 阶跃 0→1",
     "--tau --k --step-at --interval",
     "每步 payload {\"t\",\"u\",\"y\"}(≤253 B)"],
    C["box_fc"], "#00897B", tsize=8.4, lsize=7.2)
box(67.6, 17.6, 13.5, 8.6, "② GattPeer:PC 扮演 BLE 外设",
    ["WinRT GattServiceProvider",
     "is_connectable 广播(可被连接)",
     "notify 每 interval 秒推送一次",
     "poll 变体:仅 READ → dongle 轮询"],
    C["ble_bg"], C["ble_ec"], tsize=8.4, lsize=7.2)
box(82.6, 17.6, 14.8, 8.6, "③ dongle conn 面接入",
    ["CONN TARGET(svc/chr UUID 对)",
     "CONN START → 自动发现+CCCD 订阅",
     "(poll:CONN INTERVAL 周期读)",
     "重流 {\"src\":\"conn\"} 行 → USB"],
    C["conn_fc"], C["conn_ec"], tsize=8.4, lsize=7.2)
arrow(66.1, 21.9, 67.6, 21.9, lw=1.6, mutation=12)
arrow(81.1, 21.9, 82.6, 21.9, lw=1.6, mutation=12)

box(52.6, 9.4, 20.5, 7.0, "④ host 采集与物理校验(case.check)",
    ["plant_capture.jsonl 收集 conn 行(≥90% 到达)",
     "y(step_at+τ) ≈ 63.2%·K(±10%)",
     "稳态 y(∞) ≈ K·u(±10%)"],
    C["box_fc"], "#1E88E5", tsize=8.4, lsize=7.2)
box(52.6, 3.6, 20.5, 5.0, "⑤ --estimate:LLM 估计 vs ground truth",
    ["样本(≤60 行)→ prompt → {tau_est, k_est,",
     "step_at_est, method} → τ ±30% 判 PASS/FAIL"],
    C["llm_bg"], C["llm_ec"], tsize=8.4, lsize=7.2)
arrow(62.8, 17.6, 62.8, 16.4, lw=1.6, mutation=12)

# step-response inset
inx = [74.0, 98.2]
iny = [3.4, 15.6]
iax = fig.add_axes([inx[0] / 100, iny[0] / 100,
                    (inx[1] - inx[0]) / 100, (iny[1] - iny[0]) / 100])
iax.set_facecolor("white")
tau, k, t0step = 10, 1, 5
ts = [i * 0.1 for i in range(0, 801)]
ys = []
for t in ts:
    u = 1.0 if t >= t0step else 0.0
    dt = 0.1
    y = ys[-1] + (dt / tau) * (k * u - ys[-1]) if ys else 0.0
    ys.append(y)
iax.plot(ts, ys, color="#00897B", lw=2.2)
iax.axvline(t0step, color="#FB8C00", lw=1.2, ls="--")
iax.plot([t0step + tau], [0.632 * k], "o", color="#D84315", ms=6)
iax.annotate("y(τ+step)≈63.2%·K\n(校验点 ±10%)",
             xy=(t0step + tau, 0.632 * k), xytext=(t0step + tau + 9, 0.22),
             fontsize=8, color="#D84315",
             arrowprops=dict(arrowstyle="->", color="#D84315", lw=1.2))
iax.axhline(k, color="#90A4AE", lw=1.0, ls=":")
iax.text(2, k + 0.05, "稳态 ≈ K·u", fontsize=8, color="#607D8B")
iax.text(t0step + 0.3, 0.06, "u: 0→1 阶跃 @ step_at", fontsize=8,
         color="#FB8C00", rotation=90, va="bottom")
iax.set_title("一阶阶跃响应(仿真轨迹)", fontsize=9, color="#00695C")
iax.set_xlabel("t (s)", fontsize=8)
iax.set_ylabel("y", fontsize=8)
iax.set_xlim(0, 80)
iax.set_ylim(0, 1.25)
iax.tick_params(labelsize=7)
for s in iax.spines.values():
    s.set_color("#B0BEC5")
iax.grid(alpha=0.25, lw=0.5)

# ---- save -------------------------------------------------------------------
out = os.path.dirname(os.path.abspath(__file__))
fig.savefig(os.path.join(out, "system_architecture.zcode.png"), dpi=DPI,
            facecolor="white")
fig.savefig(os.path.join(out, "system_architecture.zcode.svg"),
            facecolor="white")
print("written:", os.path.join(out, "system_architecture.zcode.png"))
