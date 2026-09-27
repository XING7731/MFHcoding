#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_online_probe.py —— 查明「巴法云控制台为什么显示离线」

背景
----
设备能连、能上报, 但控制台的设备图标一直是灰的(离线)。此时数据可能其实
已经到云端了 —— 因为「控制台离线」和「上报失败」是两件完全不同的事。

巴法云设备接口 (https://cloud.bemfa.com/docs/src/api_device.html) 对每个主题
返回两个独立的在线标志:

    online      该主题「有没有订阅者在线」  <-- 控制台显示在线/离线看的是这个
    pubOnline   该主题「发布者是否在线」

本脚本做的就是: 先在未订阅状态读一次基线, 然后连上去 SUBSCRIBE 该主题并
保持连接, 期间每隔几秒读一次接口 —— 于是能直接看到 online 是否因为
「有了订阅者」而翻成 true。

结论导向
--------
* 订阅后 online 变 true   -> 控制台的「在线」= 有设备订阅该主题。
                              固件只发布不订阅, 所以永远显示离线。修法: 固件
                              连上后发一条 SUBSCRIBE, 保持订阅即可。
* 订阅后 online 仍 false  -> online 另有判据(例如必须用 TCP 私有协议接入),
                              需要继续查。

用法
----
  python bemfa_online_probe.py --uid <私钥> --topic temp001 --hold 36

注意: 同一私钥只允许一个 MQTT 连接。若开发板此刻正跑着并连着同一私钥,
本脚本会把它顶下去(它随后会自动重连)。测完 online 若不翻, 先把开发板
断电再测一次, 以排除「被开发板顶下线」的干扰。
"""

import argparse
import json
import socket
import sys
import time
import urllib.request

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bemfa_check import MqttConn          # 复用同一套最小 MQTT 实现


def fetch_topic(uid, topic):
    """读设备接口, 返回该主题的信息 dict; 失败返回 None"""
    url = ("http://apis.bemfa.com/vb/api/v2/allTopic"
           "?openID=%s&type=1" % uid)
    raw = urllib.request.urlopen(url, timeout=15).read().decode("utf-8")
    j = json.loads(raw)
    if j.get("code") != 0:
        print("  接口返回异常: %s" % raw[:200])
        return None
    for item in j["data"]["data"]:
        if item.get("topic") == topic:
            return item
    return None


def show(tag, uid, topic):
    try:
        it = fetch_topic(uid, topic)
    except Exception as e:
        print("  %-26s 查询失败: %s" % (tag, e))
        return None
    if it is None:
        print("  %-26s 该主题不存在于 MQTT 控制台" % tag)
        return None
    print("  %-26s online=%-5s pubOnline=%-5s  time=%s  msg=%s"
          % (tag, it.get("online"), it.get("pubOnline"),
             it.get("time", ""), str(it.get("msg", ""))[:38]))
    return it


def main():
    ap = argparse.ArgumentParser(description="探测巴法云 online 状态的判定依据")
    ap.add_argument("--uid", required=True, help="私钥")
    ap.add_argument("--topic", default="temp001", help="主题")
    ap.add_argument("--host", default="bemfa.com")
    ap.add_argument("--port", type=int, default=9501)
    ap.add_argument("--hold", type=int, default=36,
                    help="订阅后保持连接的秒数, 期间每 6 秒查一次")
    args = ap.parse_args()

    print("=" * 70)
    print("  巴法云 online 状态判定探测")
    print("  私钥: %s...   主题: %s" % (args.uid[:8], args.topic))
    print("=" * 70)

    print("\n[1] 基线: 本机尚未连接、未订阅")
    base = show("before subscribe", args.uid, args.topic)
    base_online = base.get("online") if base else None
    base_pub = base.get("pubOnline") if base else None

    if base_pub:
        print("\n  ! pubOnline 已经是 true —— 说明此刻有别的连接(多半是你的开发板)")
        print("    正在往该主题发布数据, 也就是说: 你的上报其实是成功的。")

    print("\n[2] 连接 MQTT 并 SUBSCRIBE 主题, 然后保持连接")
    try:
        s = socket.create_connection((args.host, args.port), timeout=10)
    except Exception as e:
        print("  TCP 连接失败: %s" % e)
        return 2
    c = MqttConn(s)
    rc = c.connect(args.uid)
    print("  CONNACK = %s" % ("None(超时)" if rc is None else "0x%02X" % rc))
    if rc != 0:
        print("  连接未成功, 无法继续(私钥被拒 / 被别的设备占用)")
        return 3

    sub = c.subscribe(args.topic)
    print("  SUBACK  = %s" % ("None(超时)" if sub is None else "0x%02X" % sub))

    flipped = False
    for i in range(max(1, args.hold // 6)):
        time.sleep(6)
        it = show("subscribe held +%2ds" % ((i + 1) * 6), args.uid, args.topic)
        if it and it.get("online") is True:
            flipped = True
            if not base_online:
                print("           ^^ online 由 false 变为 true —— 判定依据已确认")

    s.close()
    time.sleep(2)
    print("\n[3] 断开后再查一次")
    time.sleep(2)
    after = show("after disconnect", args.uid, args.topic)

    print("\n" + "=" * 70)
    print("  结论")
    print("=" * 70)
    print("  基线 online        : %s" % base_online)
    print("  订阅期间是否翻 true : %s" % flipped)
    if after is not None:
        print("  断开后 online      : %s" % after.get("online"))
    print()
    if flipped:
        print("  => 控制台的「在线」= 该主题上有订阅者。")
        print("     固件只 PUBLISH 从不 SUBSCRIBE, 所以永远显示离线。")
        print("     修法: 固件 MQTT 连上后补一条 SUBSCRIBE <主题>, 并维持长连接。")
    elif base_online is False and base_pub is True:
        print("  => 抑制订阅也没有让 online 翻 true。请把开发板断电后再跑一次本脚本,")
        print("     排除「开发板占着连接把我顶下线」的干扰。")
    else:
        print("  => 未观察到明确翻转, 请结合上面每行的数值人工判断。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
