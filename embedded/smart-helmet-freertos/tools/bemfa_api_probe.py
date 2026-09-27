#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_api_probe.py —— 把巴法云该私钥下的主题记录【原始】拉出来看

为什么要这个
    "板子确有发布、订阅者也能收到, 可控制台/接口就是不见新数据" —— 这种
    矛盾有一个很容易被忽略的解释:
        ★ 巴法云同一个主题名, MQTT 型 和 TCP 型 是【两条相互独立的记录】。
    本项目 temp001 最早是给旧工程(bemfa.com:8344 TCP)建的, 所以它的记录
    可能是 TCP 型; 而 MQTT 发布写进去的是另一条记录。一直只查 type=1,
    就可能永远看不到设备实际在写的那一条。

本脚本不猜, 直接把 type=1 / type=2 / 不带 type 三种查询的原始 JSON 打出来,
并把每条记录的 topic / online / pubOnline / time / msg 平铺成表。

全程 HTTP 只读, 不占用 MQTT 连接, 不会顶掉板子。

用法
    python bemfa_api_probe.py
    python bemfa_api_probe.py --uid xxxx
"""

import argparse
import json
import sys
import urllib.request

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"


def fetch(uid, type_param):
    """type_param 为 None 表示不带 type 参数"""
    url = "http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s" % uid
    if type_param is not None:
        url += "&type=%s" % type_param
    req = urllib.request.Request(url, headers={"User-Agent": "bemfa-probe/1.0"})
    raw = urllib.request.urlopen(req, timeout=12).read().decode("utf-8", "replace")
    return url, raw


def flatten(raw):
    try:
        j = json.loads(raw)
    except Exception:
        return None, []
    d = (j.get("data") or {})
    items = d.get("data") or []
    return j, items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    args = ap.parse_args()

    print("=" * 78)
    print("  巴法云主题记录探针  uid=%s..." % args.uid[:8])
    print("  目的: 分清设备实际写入的是哪一条记录 (MQTT / TCP / 其它)")
    print("=" * 78)

    for tp in (1, 2, None):
        label = "type=%s" % tp if tp is not None else "不带 type"
        print("\n" + "-" * 78)
        print("  【%s】" % label)
        print("-" * 78)
        try:
            url, raw = fetch(args.uid, tp)
        except Exception as e:
            print("  请求失败: %s" % e)
            continue

        print("  URL: %s" % url)
        j, items = flatten(raw)
        if j is None:
            print("  返回不是合法 JSON, 原文:")
            print("  %s" % raw[:600])
            continue
        print("  code=%s  message=%s  共 %d 条记录"
              % (j.get("code"), j.get("message"), len(items)))
        if not items:
            print("  (空)")
            print("  原始返回: %s" % raw[:400])
            continue

        print("  %-16s %-9s %-10s %-20s %s"
              % ("topic", "online", "pubOnline", "time", "msg"))
        print("  " + "-" * 74)
        for it in items:
            print("  %-16s %-9s %-10s %-20s %s"
                  % (it.get("topic"), it.get("online"), it.get("pubOnline"),
                     it.get("time"), str(it.get("msg"))[:80]))

    print("\n" + "=" * 78)
    print("  判读")
    print("=" * 78)
    print("  * 若同一个 topic 在 type=1 和 type=2 下各出现一次, 且")
    print("    【其中一条的 time/msg 在跟着设备更新】—— 那条就是设备实际写的记录,")
    print("    此前一直盯着另一条, 所以看起来'没有数据'。")
    print("  * 若两边都没有设备数据, 则记录确实没被写入,")
    print("    下一步用 bemfa_tap.py 确认设备当下是否在实时发布。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
