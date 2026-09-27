#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_cloud_monitor.py —— 只看云端, 不碰连接: 板子到底有没有把数据发上去

为什么单独做这个脚本
    bemfa_watch.py 只盯一个主题。但"板子显示 OK 却没有数据"还有一种容易被
    忽略的可能: 板子发到了【另一个主题】(比如后来新建的 helmet001)。
    本脚本先把该私钥下的【所有主题】列出来, 再持续采样每个主题的
    time/msg/online/pubOnline —— 任何一个主题有动静都能立刻看出是哪个。

    全程只用 HTTP 只读接口 (apis.bemfa.com/vb/api/v2/allTopic),
    【不会占用 MQTT 连接, 不会把板子顶下线】—— 这一点和 MQTT 客户端脚本
    正相反。所以排查时应该优先用它。

!! 关于 msg / time 这两个字段的重要更正(2026-09-14 实测) !!
    本脚本原先把"msg/time 不更新"直接判成"板子没把数据送达云端" —— 这是错的。
    实测(见 docs/17): 开发板每 ~30s 正常发布到 temp001, 用 MQTT 订阅上去能连续
    收到它 seq 递增的报文, 可本接口的 msg/time 却始终停在最后一次【由本脚本
    自己发的】调试报文上。也就是说:
        ★ msg / time 不能用来判断"设备有没有在发"。
    它们只反映这个 HTTP 接口这一侧看到的记录, 对本项目这种设备上报并不跟随。

    另外 online / pubOnline 有约 1~2 分钟的滞后:
    实测已断开连接后, 这两个字段仍然显示 True。所以它们也不能用来做实时判断
    (早前把 pubOnline=False 当成"板子没在发"的证据, 就是被这个滞后误导的)。

    要判断"板子到底有没有在发", 唯一可靠的两种办法:
      ① 用 bemfa_tap.py 订阅上去听 —— 能连续收到 seq 递增的报文 = 在发;
      ② 看板子 OLED 第 8 行的 e(回环确认)是否与 p 同步增长。

用法
    python bemfa_cloud_monitor.py                     # 列出全部主题, 然后盯 120s
    python bemfa_cloud_monitor.py --seconds 300        # 盯 5 分钟
    python bemfa_cloud_monitor.py --interval 5
"""

import argparse
import json
import sys
import time
import urllib.request
from datetime import datetime

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"


def fetch_all(uid):
    url = "http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid
    raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8")
    j = json.loads(raw)
    return (j.get("data", {}) or {}).get("data", []) or []


def age(tstr):
    try:
        return (datetime.now() - datetime.strptime(tstr, "%Y-%m-%d %H:%M:%S")).total_seconds()
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--interval", type=float, default=4.0)
    args = ap.parse_args()

    print("巴法云云端监控（HTTP 只读, 不占用 MQTT 连接）")
    print("uid=%s...  时长=%.0fs  采样=%.0fs" % (args.uid[:8], args.seconds, args.interval))
    print("=" * 78)

    try:
        items = fetch_all(args.uid)
    except Exception as e:
        print("查询失败: %s" % e)
        return 2

    print("\n【该私钥下的全部主题】")
    print("  %-16s %-9s %-10s %-20s %s" % ("topic", "online", "pubOnline", "time", "msg"))
    print("  " + "-" * 74)
    for it in items:
        print("  %-16s %-9s %-10s %-20s %s"
              % (it.get("topic"), it.get("online"), it.get("pubOnline"),
                 it.get("time"), str(it.get("msg"))[:90]))

    if not items:
        print("  (没有主题)")

    # ---- 持续采样 ----
    print("\n【持续采样 %.0f 秒】只有变化时才打印" % args.seconds)
    prev = {}
    for it in items:
        prev[it.get("topic")] = (it.get("time"), it.get("msg"), it.get("online"), it.get("pubOnline"))

    t0 = time.time()
    changed = 0
    while time.time() - t0 < args.seconds:
        time.sleep(args.interval)
        try:
            items = fetch_all(args.uid)
        except Exception as e:
            print("  [%3ds] 查询失败: %s" % (int(time.time() - t0), e))
            continue

        for it in items:
            k = it.get("topic")
            cur = (it.get("time"), it.get("msg"), it.get("online"), it.get("pubOnline"))
            if prev.get(k) != cur:
                print("  [%3ds] ★ %s 发生变化" % (int(time.time() - t0), k))
                print("        time  : %s -> %s" % (prev.get(k, ("?",))[0], cur[0]))
                print("        msg   : %s" % str(cur[1])[:150])
                print("        online: %s -> %s   pubOnline: %s -> %s"
                      % (prev.get(k, (None,) * 4)[2], cur[2],
                         prev.get(k, (None,) * 4)[3], cur[3]))
                prev[k] = cur
                changed += 1

    print("\n" + "=" * 78)
    print("采样结束: 共检测到 %d 次变化" % changed)
    if changed == 0:
        print("  → 本接口的 msg/time 没有变化。")
        print("     【注意】这【不能】证明板子没上报 —— 实测板子正常上报时这两个")
        print("     字段同样不动(详见 docs/17)。要判断板子在不在发, 请改用:")
        print("       python bemfa_tap.py            # 订阅上去听, 看 seq 是否递增")
        print("     或看板子 OLED 第 8 行的 e(回环确认)有没有和 p 一起涨。")
    else:
        print("  → 有变化。看上面是哪个 topic / 哪条 msg, 就能判断是谁发的。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
