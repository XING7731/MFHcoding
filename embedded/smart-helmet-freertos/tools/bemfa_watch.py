#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_watch.py —— 巴法云主题状态实时监控（"云端示波器"）

一句话用途
    把它挂在电脑上运行, 然后去给开发板复位/烧录。板子只要真的连上并上报,
    这里会**立刻**把状态变化打出来, 不需要 OLED、不需要 USB-TTL、不用盯控制台。

为什么需要它
    "控制台显示离线"是个很含糊的现象 —— 它可能是四种完全不同的原因:
        ① 板子压根没连上云端          (online=False pubOnline=False time 不动)
        ② 板子连上了但固件是旧版(没订阅) (online=False pubOnline=True time 在更新)
        ③ 板子连上了、订阅了, 但你没刷新页面(online=True)
        ④ 板子掉线了                  (time 停在很久以前)
    这个脚本每秒把 online / pubOnline / time 三个字段放在一起看,
    由 time 是否在推进来判定"板子此刻到底在不在发包", 于是四种原因立刻分开。

判读表(脚本会自动打印结论)
    online=True                          → 设备在线, 一切正常
    online=False pubOnline=True time新   → 在发包但没订阅(旧固件 / 订阅失败)
    online=False pubOnline=True time旧   → 之前发过, 现在停了
    online=False pubOnline=False         → 云端毫无活动, 设备没连上

⚠ 运行期间不要用 MQTTX / mqtt.fx 等工具以同一私钥连接
    同一私钥只允许一个 MQTT 连接, 你的调试客户端会把板子顶下线,
    观察到的状态就是调试客户端的, 无法判读。

用法
    python bemfa_watch.py                       # 默认私钥/主题, 每 3 秒采样
    python bemfa_watch.py --interval 2          # 加快采样
    python bemfa_watch.py --topic helmet001     # 换主题
    python bemfa_watch.py --once                # 只查一次就退出
"""

import argparse
import json
import sys
import time
import urllib.request
from datetime import datetime

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"
DEFAULT_TOPIC = "temp001"


def fetch(uid, topic):
    """取主题对象; 失败返回 None, 主题不存在返回 'NOTFOUND'"""
    url = ("http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid)
    try:
        raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8")
        j = json.loads(raw)
    except Exception as e:
        return ("ERR", str(e))
    for it in (j.get("data", {}) or {}).get("data", []) or []:
        if it.get("topic") == topic:
            return it
    return "NOTFOUND"


def age_sec(tstr):
    """云端 time 距现在多少秒; 解析失败返回 None"""
    try:
        return (datetime.now() - datetime.strptime(tstr, "%Y-%m-%d %H:%M:%S")).total_seconds()
    except Exception:
        return None


def verdict(it):
    """根据字段组合给出人话结论"""
    if it == "NOTFOUND":
        return "主题不存在 —— 请到 MQTT 控制台确认主题名是否正确"
    if isinstance(it, tuple):
        return "查询失败: %s" % it[1]

    online = bool(it.get("online"))
    pubo = bool(it.get("pubOnline"))
    a = age_sec(it.get("time"))

    if online:
        return "★ 设备【在线】 —— 已有订阅者。一切正常, 无需再改代码。"
    if pubo and a is not None and a < 90:
        return ("▲ 有设备在【发布】但没人【订阅】 → 控制台显示离线。\n"
                "      典型原因: 固件是旧版(没有 SUBSCRIBE), 或订阅那一步失败了。")
    if pubo:
        return ("· 曾经有发布者, 但已 %.0f 秒没有新数据 → 设备可能已停机或掉线。"
                % (a if a is not None else 0))
    return ("✗ 云端完全没有活动 → 设备【根本没连上】。\n"
            "      去查 AT / WiFi / TCP / MQTT 这四步里卡在哪一步(见 OLED 或串口日志)。")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--topic", default=DEFAULT_TOPIC)
    ap.add_argument("--interval", type=float, default=3.0)
    ap.add_argument("--once", action="store_true", help="只查一次")
    args = ap.parse_args()

    print("=" * 76)
    print(" 巴法云主题状态监控（云端示波器）")
    print(" topic = %s   uid = %s...   采样间隔 %.0fs" % (args.topic, args.uid[:8], args.interval))
    print(" 提示: 现在去复位/烧录开发板, 状态一旦变化这里会立刻提示。Ctrl+C 退出。")
    print("=" * 76)

    prev = None
    t0 = time.time()
    try:
        while True:
            it = fetch(args.uid, args.topic)
            if it == "NOTFOUND":
                print("\n!! 主题 %s 不在你的 MQTT 控制台里。" % args.topic)
                print("   " + verdict(it))
                return 2
            if isinstance(it, tuple):
                print("  [%4ds] 查询失败: %s" % (int(time.time() - t0), it[1]))
                time.sleep(args.interval)
                continue

            cur = (it.get("online"), it.get("pubOnline"), it.get("time"))
            tag = "基线" if prev is None else ("变化" if cur != prev else "   ")
            a = age_sec(it.get("time"))
            print("  [%4ds] %s  online=%-5s pubOnline=%-5s 最后数据=%s (%s前)"
                  % (int(time.time() - t0), tag, cur[0], cur[1], cur[2],
                     ("%.0fs" % a) if a is not None else "?"))

            if prev is not None and cur != prev:
                if cur[2] != prev[2]:
                    print("         >>> 检测到【新数据到达】—— 板子正在成功上报！")
                if cur[0] is True and prev[0] is not True:
                    print("         >>> online 翻成 True —— 【订阅成功】, 控制台图标会亮成在线。")
                if cur[0] is not True and prev[0] is True:
                    print("         >>> online 掉回 False —— 订阅者掉线了。")

            if prev is None:
                print()
                print("  当前判读: " + verdict(it))
                print("-" * 76)
            prev = cur

            if args.once:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n已停止。")
        if prev is not None:
            print("最后一次状态: online=%s pubOnline=%s time=%s" % prev)
    return 0


if __name__ == "__main__":
    sys.exit(main())
