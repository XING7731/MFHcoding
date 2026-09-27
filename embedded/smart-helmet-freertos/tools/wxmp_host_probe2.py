#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wxmp_host_probe2.py —— 在 api.bemfa.com 上找出"真能取到数据"的接口路径

为什么必须做这一步：
  巴法云官方【小程序】文档让人在 request 合法域名里填 https://api.bemfa.com，
  而官方 HTTP API 文档给的是 /api/device/v1/data/1/get/ 这种老路径。
  上一轮实测该路径返回 {"code":"40012"}（topic error），说明路径存在但参数不对。
  如果用户真的按官方文档只加了 api.bemfa.com，那我们的代码就必须能在该主机
  上取到数，否则配得再对也没用。

本脚本对 api.bemfa.com / apis.bemfa.com 两主机 × 多路径 × 多参数组合做穷举，
把 HTTP 200 且 code==0 的组合挑出来，作为小程序可用的"主机+路径"清单。

用法: python wxmp_host_probe2.py
"""

import json
import sys
import urllib.error
import urllib.request

UID = "d2f5cf227188d137f8171da8611a7614"
TOPIC = "temp001"

UA = ("Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) "
      "AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148 "
      "MicroMessenger/8.0.40(0x18002832) NetType/WIFI Language/zh_CN")

HOSTS = ["api.bemfa.com", "apis.bemfa.com"]

# 路径模板：{host} 会被替换
PATHS = [
    "/va/getmsg",
    "/api/device/v1/data/1/get/",
    "/api/device/v1/data/1/get",
    "/api/device/v2/data/1/get/",
    "/vb/api/v2/allTopic",
    "/api/v2/allTopic",
]

# 参数组合：都是巴法云文档里出现过的写法
PARAM_SETS = [
    ("uid+topic+type1", "uid={uid}&topic={topic}&type=1"),
    ("uid+topic+type1+num1", "uid={uid}&topic={topic}&type=1&num=1"),
    ("openID+topic+type1", "openID={uid}&topic={topic}&type=1"),
    ("uid+topic", "uid={uid}&topic={topic}"),
    ("uid+topic+type2", "uid={uid}&topic={topic}&type=2"),
    ("openID+type1(主题列表)", "openID={uid}&type=1"),
    ("uid+type1(主题列表)", "uid={uid}&type=1"),
]


def probe(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        r = urllib.request.urlopen(req, timeout=12)
        return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:
        return None, "%s: %s" % (type(e).__name__, e)


def code_of(body):
    try:
        j = json.loads(body)
    except Exception:
        return None, False
    if not isinstance(j, dict):
        return None, False
    c = j.get("code")
    has_data = bool(j.get("data"))
    return c, has_data


def main():
    winners = []

    for host in HOSTS:
        print("\n" + "=" * 72)
        print("主机: %s" % host)
        print("=" * 72)
        for path in PATHS:
            for label, tpl in PARAM_SETS:
                q = tpl.format(uid=UID, topic=TOPIC)
                url = "https://%s%s?%s" % (host, path, q)
                status, body = probe(url)

                if status is None:
                    mark, note = "✗", body[:70]
                elif status != 200:
                    mark, note = "·", "HTTP %s" % status
                else:
                    c, has_data = code_of(body)
                    if c == 0 and has_data:
                        mark = "★"
                        note = "code=0 且有数据"
                        winners.append((host, path, label, url))
                    elif c == 0:
                        mark = "○"
                        note = "code=0 但 data 为空"
                    else:
                        mark, note = "·", "code=%s" % (c,)

                # 只打印"非 404"的，404 是噪音
                if status != 404:
                    print("  %s %-42s %-24s %s" % (mark, path, label, note))

    print("\n" + "=" * 74)
    print("★ 可直接被小程序使用的主机+路径组合")
    print("=" * 74)
    if not winners:
        print("（无）—— 说明没有一组能取到数据，需要先查 topic 是否存在于该账号")
    for host, path, label, url in winners:
        print("\n  https://%s%s" % (host, path))
        print("    参数写法 : %s" % label)
        print("    完整 URL : %s" % url)

    hosts_used = sorted({w[0] for w in winners})
    print("\n" + "=" * 74)
    print("结论：request 合法域名应填 -> %s" % (
        "、".join("https://" + h for h in hosts_used) if hosts_used else "(待定)"))
    print("=" * 74)
    print("若两个主机都能用，建议【两个都填】（微信支持多条，填写框里换行即可）：")
    print("  https://apis.bemfa.com")
    print("  https://api.bemfa.com")
    print("这样无论代码用哪个主机都不会踩白名单不匹配的坑。")


if __name__ == "__main__":
    sys.exit(main())
