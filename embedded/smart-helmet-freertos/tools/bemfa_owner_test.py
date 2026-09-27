#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_owner_test.py —— 「云端那个 online=True 的订阅者, 到底是不是我的板子?」

为什么要做这个测试
    巴法云同一私钥【只允许一条 MQTT 连接】。当板子屏幕显示 "NET 有数"
    却查不到新数据时, 只有两种可能, 而它们必须靠下面的实验才能分开:

      A) 板子确实连着 (它就是那个订阅者), 只是 PUBLISH 报文没能到达 Broker
         → 问题在发送路径;
      B) 板子压根没连上, 云端那个 online=True 是【别的客户端】占着唯一连接,
         板子每次都被顶下线, 而模块不会主动报 CLOSED
         → 问题在"谁占着连接"。

    两者的区别只有一个观测点: 【把板子断电, 看 online 会不会跟着掉】。

本脚本全程只做 HTTP 只读查询 (apis.bemfa.com/vb/api/v2/allTopic),
【不建立任何 MQTT 连接】—— 所以不会顶掉板子, 也不会干扰现场状态。
排查阶段应该只用这类脚本, 不要用 MQTTX / mqtt.fx / 浏览器控制台。

用法
    python tools\\bemfa_owner_test.py
    然后按屏幕提示操作板子电源, 三个阶段共约 2 分钟, 结束会自动给结论。

判读
    online 在「断电」阶段变 False、在「上电」阶段变回 True
        → 板子就是那个订阅者, 连接是活的 ⇒ 问题在发送路径(A)
    online 三个阶段一动不动
        → 那个订阅者是别的客户端 ⇒ 板子被顶下线(B), 先清掉占用者
"""

import argparse
import json
import sys
import time
import urllib.request

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"

# (持续秒数, 提示语)
PHASES = (
    (45, "阶段 1 / 基线  —— 请【保持板子通电】, 什么都不要动"),
    (60, "阶段 2 / 断电  —— 请现在把板子【彻底断电】(拔电池 / 拔 USB / 关电源开关)"),
    (60, "阶段 3 / 上电  —— 请现在把板子【重新上电】并等它跑起来"),
)


def fetch_all(uid):
    url = "http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid
    raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8")
    j = json.loads(raw)
    return (j.get("data", {}) or {}).get("data", []) or []


def snap(uid):
    """返回 {topic: (online, pubOnline, time)}"""
    out = {}
    for it in fetch_all(uid):
        out[it.get("topic")] = (it.get("online"), it.get("pubOnline"), it.get("time"))
    return out


def uniq(seq):
    r = []
    for v in seq:
        if v not in r:
            r.append(v)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--interval", type=float, default=4.0)
    args = ap.parse_args()

    print("巴法云「连接归属」判定 (HTTP 只读, 不占用 MQTT 连接)")
    print("uid=%s...   采样间隔 %.0fs   总时长约 %ds"
          % (args.uid[:8], args.interval, sum(p[0] for p in PHASES)))
    print("=" * 78)

    try:
        topics = list(snap(args.uid).keys())
    except Exception as e:
        print("查询失败: %s" % e)
        return 2
    if not topics:
        print("该私钥下没有任何主题, 先到巴法云 MQTT 控制台建一个。")
        return 2

    print("该私钥下的主题: %s" % ", ".join(topics))
    print()

    phase_snaps = []
    for dur, banner in PHASES:
        print("-" * 78)
        print(">>> " + banner)
        print("-" * 78)
        t0 = time.time()
        seen = []
        while time.time() - t0 < dur:
            try:
                s = snap(args.uid)
            except Exception as e:
                print("  [%3ds] 查询失败: %s" % (int(time.time() - t0), e))
                time.sleep(args.interval)
                continue
            line = "  ".join("%s: online=%-5s pubOnline=%-5s" % (t, s[t][0], s[t][1])
                             for t in topics)
            print("  [%3ds] %s" % (int(time.time() - t0), line))
            seen.append(s)
            time.sleep(args.interval)
        phase_snaps.append(seen)
        print()

    # ---------------- 判读 ----------------
    print("=" * 78)
    print("【判读】")
    verdict = []
    for t in topics:
        base = uniq([s[t][0] for s in phase_snaps[0]])
        off = uniq([s[t][0] for s in phase_snaps[1]])
        back = uniq([s[t][0] for s in phase_snaps[2]])
        pub = uniq([s[t][1] for ph in phase_snaps for s in ph])

        print("  topic = %s" % t)
        print("      通电基线 : online = %s" % base)
        print("      断电期间 : online = %s" % off)
        print("      上电之后 : online = %s" % back)
        print("      pubOnline(全程) = %s" % pub)

        base_on = (True in base)
        off_has_false = (False in off)
        back_on = (True in back)

        if base_on and off_has_false:
            msg = ("板子【就是】那个订阅者 —— 断电后 online 掉了, 上电%s。\n"
                   "      ⇒ 连接是活的, 问题出在【发送路径】: PUBLISH 报文没能到达 Broker。\n"
                   "      下一步看屏幕第 8 行是 'p=x NO ECHO' 还是 'OK p=x e=y'。"
                   % ("又回来了" if back_on else "没回来(需再等一会儿/检查板子是否重启)"))
        elif base_on and not off_has_false:
            msg = ("板子【不是】那个订阅者 —— 断电了 online 依然是 True。\n"
                   "      ⇒ 有一个【别的客户端】正占着这唯一一条连接, 板子每次都被顶下线,\n"
                   "        而 ESP01S 不会主动报 CLOSED, 所以板子自己完全不知道。\n"
                   "      下一步: 关掉浏览器里的巴法云 MQTT 控制台页面 / MQTTX / 旧板子,\n"
                   "              再让板子断电 10 秒后重启。")
        elif not base_on and not back_on:
            msg = ("三个阶段 online 全是 False —— 没有任何订阅者在线上。\n"
                   "      ⇒ 板子从来没连上过(卡在建链阶段), 同时也确认没有别的客户端占用。\n"
                   "      下一步看屏幕第 8 行: 'AT no reply' / 'WiFi fail' / 'TCP fail' / 'MQTT rc=xx'。")
        else:
            msg = "状态在三个阶段里都在变, 说明板子在反复重连。请看上面哪一段掉的。"
        print("      → %s" % msg)
        verdict.append(msg)

    print()
    print("=" * 78)
    print("把上面「→」那几行连同 OLED 第 8 行的原文一起发给我, 就能定位了。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
