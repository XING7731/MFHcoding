#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wxmp_api_probe.py —— 给微信小程序选接口：确认巴法云"读最新消息"到底走哪个 URL

为什么需要这个脚本
    小程序只能发 HTTPS(wx.request), 且域名必须能配进"request 合法域名"。
    本脚本把巴法云几个候选读取接口逐个试一遍, 明确回答三件事:
        ① 该 URL 走 http 还是 https 都能通?
        ② 返回 JSON 的结构长什么样(字段路径), 小程序里怎么取 msg/time?
        ③ 拿到的 msg 能不能 json.loads 成固件的紧凑报文?

用法
    python wxmp_api_probe.py
"""

import json
import sys
import urllib.request
import urllib.error

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

UID = "d2f5cf227188d137f8171da8611a7614"
TOPIC = "temp001"

CANDIDATES = [
    ("allTopic(本次固件上报用的查询接口)",
     "https://apis.bemfa.com/vb/api/v2/allTopic?openID={uid}&type=1"),
    ("getmsg(官方获取最新一条消息接口)",
     "https://apis.bemfa.com/va/getmsg?uid={uid}&topic={topic}&type=1&num=1"),
    ("getmsg(v1 data get 路径)",
     "https://api.bemfa.com/api/device/v1/data/1/get/?uid={uid}&topic={topic}&type=1"),
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.status, r.read().decode("utf-8", "replace")


def main():
    print("=" * 78)
    print("  巴法云读取接口探测   uid=%s...  topic=%s" % (UID[:8], TOPIC))
    print("=" * 78)

    for name, tpl in CANDIDATES:
        url = tpl.format(uid=UID, topic=TOPIC)
        print("\n--- %s" % name)
        print("    URL: %s" % url)
        try:
            status, body = fetch(url)
        except urllib.error.HTTPError as e:
            print("    ✗ HTTP %s %s" % (e.code, e.reason))
            continue
        except Exception as e:
            print("    ✗ 失败: %r" % (e,))
            continue

        print("    HTTP %s, 响应 %d 字节" % (status, len(body)))
        try:
            j = json.loads(body)
        except Exception:
            print("    响应不是 JSON, 原文前 200 字符:")
            print("    %s" % body[:200])
            continue

        # 打印顶层结构, 便于确定取值路径
        print("    顶层键: %s" % list(j.keys()))
        print("    code = %s" % j.get("code"))
        print("    完整响应(截断 800): %s" % json.dumps(j, ensure_ascii=False)[:800])

        msg = _find_msg(j, TOPIC)
        if msg is None:
            print("    未在响应里找到 topic=%s 的记录, 原始响应:" % TOPIC)
            print("    %s" % json.dumps(j, ensure_ascii=False)[:400])
            continue

        print("    ✓ 取到 msg (共 %d 字符)" % len(msg))
        print("      原文: %s" % msg)
        try:
            d = json.loads(msg)
            print("      ✓ 可解析为 JSON, 键: %s" % list(d.keys()))
            print("      字段值: %s" % json.dumps(d, ensure_ascii=False))
        except Exception as e:
            print("      ✗ 不是合法 JSON: %r" % (e,))

    print("\n" + "=" * 78)


def _find_msg(j, topic):
    """在响应里挖出该 topic 的 msg —— 兼容两种常见结构"""
    data = j.get("data")
    # 结构 A: {"code":0,"data":{"data":[{topic,time,msg}, ...]}}
    if isinstance(data, dict):
        arr = data.get("data")
        if isinstance(arr, list):
            for it in arr:
                if isinstance(it, dict) and it.get("topic") == topic:
                    return it.get("msg")
            if arr and isinstance(arr[0], dict) and "msg" in arr[0]:
                return arr[0].get("msg")
        if "msg" in data:
            return data.get("msg")
    # 结构 B: {"code":0,"data":[{topic,msg}]}
    if isinstance(data, list):
        for it in data:
            if isinstance(it, dict) and it.get("topic") == topic:
                return it.get("msg")
        if data and isinstance(data[0], dict) and "msg" in data[0]:
            return data[0].get("msg")
    if "msg" in j:
        return j.get("msg")
    return None


if __name__ == "__main__":
    main()
