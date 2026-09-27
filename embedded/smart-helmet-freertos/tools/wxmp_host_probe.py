#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wxmp_host_probe.py —— 判定"小程序到底该把哪个域名加进 request 白名单"

背景：
  巴法云官方【小程序】示例文档里写的是在 request 合法域名填 https://api.bemfa.com
  （注意是 api，不是 apis），而本项目固件工具链一直在用 https://apis.bemfa.com。
  微信白名单不认子域，两个主机必须分别添加，配错就整条请求失败。

本脚本对若干候选 (主机, 路径) 组合逐个发真实 HTTPS 请求，输出：
  域名解析 -> HTTP 状态 -> TLS 证书签发者 -> 返回体摘要
据此判断哪些组合是"真能用"的，避免在小程序里靠猜。

判定原则：
  只要返回了 HTTP 状态码（哪怕是 4xx/5xx），就说明【域名+TLS 链路是通的】，
  可用于白名单；返回 业务错误码 更是证明接口存在于该主机。
  只有 DNS 失败 / TLS 失败 / 连接超时才说明该主机不可加。

用法: python wxmp_host_probe.py
"""

import json
import socket
import ssl
import sys
import urllib.error
import urllib.request

UID = "d2f5cf227188d137f8171da8611a7614"
TOPIC = "temp001"

# (标签, 完整 URL, 说明)
TARGETS = [
    ("apis.bemfa.com /va/getmsg（本项目现用）",
     "https://apis.bemfa.com/va/getmsg?uid={uid}&topic={topic}&type=1&num=1",
     "当前小程序代码请求的就是这个"),

    ("api.bemfa.com /va/getmsg（官方小程序文档的域名）",
     "https://api.bemfa.com/va/getmsg?uid={uid}&topic={topic}&type=1&num=1",
     "同为 /va 路径, 看该主机是否也提供"),

    ("api.bemfa.com /api/device/v1/data/1/get/（官方 HTTP 文档路径）",
     "https://api.bemfa.com/api/device/v1/data/1/get/?uid={uid}&topic={topic}&type=1",
     "官方 HTTP API 文档里的老路径"),

    ("apis.bemfa.com /vb/api/v2/allTopic（主题列表+在线状态）",
     "https://apis.bemfa.com/vb/api/v2/allTopic?openID={uid}&type=1",
     "可拿到 online/pubOnline, 本模块默认不用"),

    ("api.bemfa.com /vb/api/v2/allTopic",
     "https://api.bemfa.com/vb/api/v2/allTopic?openID={uid}&type=1",
     "同上, 换主机"),
]

UA = ("Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) "
      "AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148 "
      "MicroMessenger/8.0.40(0x18002832) NetType/WIFI Language/zh_CN")


def peer_issuer(host):
    """取对端证书的签发者，确认 TLS 链路与证书来源（真机握手失败多半在这）"""
    try:
        ctx = ssl.create_default_context()
        with socket.create_connection((host, 443), timeout=10) as s:
            with ctx.wrap_socket(s, server_hostname=host) as ss:
                cert = ss.getpeercert()
        if not cert:
            return "(无证书信息)"
        issuer = dict(x[0] for x in cert.get("issuer", []))
        subject = dict(x[0] for x in cert.get("subject", []))
        return "CN=%s <- 签发 %s (%s)" % (
            subject.get("commonName", "?"),
            issuer.get("organizationName", issuer.get("commonName", "?")),
            cert.get("notAfter", "?"),
        )
    except Exception as e:
        return "TLS 失败: %s: %s" % (type(e).__name__, e)


def resolve(host):
    try:
        ips = sorted({ai[4][0] for ai in socket.getaddrinfo(host, 443, proto=socket.IPPROTO_TCP)})
        return ", ".join(ips)
    except Exception as e:
        return "DNS 失败: %s" % e


def probe(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        r = urllib.request.urlopen(req, timeout=15)
        return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:
        return None, "%s: %s" % (type(e).__name__, e)


def summarize(body, limit=170):
    if not body:
        return "(空)"
    try:
        j = json.loads(body)
    except Exception:
        return body[:limit].replace("\n", " ")
    if isinstance(j, dict):
        keep = {}
        for k in ("code", "msg", "message", "data"):
            if k in j:
                v = j[k]
                if k == "data":
                    v = "(有数据)" if v else v
                keep[k] = v
        return json.dumps(keep, ensure_ascii=False)[:limit]
    return body[:limit]


def main():
    hosts = ["apis.bemfa.com", "api.bemfa.com", "bemfa.com"]
    print("=" * 74)
    print("一、域名解析与 TLS 证书")
    print("=" * 74)
    for h in hosts:
        print("\n[%s]" % h)
        print("  DNS  : %s" % resolve(h))
        print("  证书 : %s" % peer_issuer(h))

    print("\n" + "=" * 74)
    print("二、接口可用性（能否作为白名单域名）")
    print("=" * 74)
    for label, tpl, note in TARGETS:
        url = tpl.format(uid=UID, topic=TOPIC)
        code, body = probe(url)
        print("\n▸ %s" % label)
        print("  %s" % note)
        print("  URL   : %s" % url)
        if code is None:
            print("  结果  : ✗ 链路不通 -> %s" % body[:120])
            print("  结论  : 该主机【不能】作为白名单域名（连 TLS/DNS 都过不去）")
        else:
            print("  HTTP  : %s" % code)
            print("  返回  : %s" % summarize(body))
            if code == 200:
                print("  结论  : ✓ 可用（HTTP 200）")
            else:
                print("  结论  : △ 域名链路通，可加白名单；此路径/参数不对（HTTP %s 是业务层拒绝）" % code)

    print("\n" + "=" * 74)
    print("三、给微信公众平台的填写建议")
    print("=" * 74)
    print("  request 合法域名：要填【小程序代码里实际请求的那个主机】")
    print("    - 若代码用 apis.bemfa.com -> 填 https://apis.bemfa.com")
    print("    - 若代码用 api.bemfa.com  -> 填 https://api.bemfa.com")
    print("    两个都填最保险（微信允许多个，用分号分隔）")
    print("  socket 合法域名（走 WSS 实时推送时才需要）：wss://bemfa.com")


if __name__ == "__main__":
    sys.exit(main())
