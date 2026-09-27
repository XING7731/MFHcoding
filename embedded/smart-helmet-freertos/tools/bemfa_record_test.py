#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_record_test.py —— 判定"转发了但没写进记录"是不是因为报文太长

要回答的问题
    实测: 用 MQTT 订阅 temp001 能收到开发板 ~250 字节的 JSON 报文(转发正常),
    可是巴法云 HTTP 接口的 msg/time 从来没有出现过它 —— 一直停在本机脚本发的
    那条 33 字节短报文上。而本机脚本那条【短】报文却能被正常记录。

    于是最可疑的变量是【报文长度】(或其中的 null / 特殊字符):
    若平台对 msg 记录有长度限制, 超长报文可能【转发照做、记录写入失败】,
    于是订阅者收得到、控制台却永远看不到 —— 与现象完全吻合。

本脚本怎么做
    用同一条 MQTT 连接, 依次发布【长度递增】的一串报文, 每发一条就查一次
    HTTP 接口, 看该条有没有出现在 msg 里, 从而找到"能被记录的"长度上限。

    ★ 会短暂顶掉板子(同私钥单连接), 这是必须付出的代价; 板子会自行重连。

用法
    python bemfa_record_test.py
    python bemfa_record_test.py --uid xxxx --topic temp001
"""

import argparse
import json
import socket
import sys
import time
import urllib.request
from datetime import datetime

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"
DEFAULT_TOPIC = "temp001"


def enc_str(s):
    b = s.encode("utf-8")
    return len(b).to_bytes(2, "big") + b


def enc_rem(n):
    out = bytearray()
    while True:
        d = n % 128
        n //= 128
        if n:
            d |= 0x80
        out.append(d)
        if not n:
            break
    return bytes(out)


def api_msg(uid, topic):
    """返回该 topic 的 (time, msg)；查询失败抛异常"""
    url = ("http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid)
    raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8", "replace")
    j = json.loads(raw)
    for it in ((j.get("data") or {}).get("data") or []):
        if it.get("topic") == topic:
            return it.get("time"), it.get("msg")
    return None, None


class Pub(object):
    def __init__(self, host, port, uid):
        self.host, self.port, self.uid = host, port, uid
        self.s = None

    def open(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect((self.host, self.port))
        vh = b"\x00\x04MQTT\x04\x02" + (60).to_bytes(2, "big")
        body = vh + enc_str(self.uid)
        self.s.sendall(b"\x10" + enc_rem(len(body)) + body)
        d = self.s.recv(64)
        if not d or (d[0] >> 4) != 2:
            return False
        rc = d[3] if len(d) > 3 else 0xFF
        print("  CONNACK rc=0x%02X %s" % (rc, "接受" if rc == 0 else "被拒绝"))
        return rc == 0

    def publish(self, topic, payload):
        pb = payload.encode("utf-8")
        body = enc_str(topic) + pb
        self.s.sendall(b"\x30" + enc_rem(len(body)) + body)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def board_like_json(seq, target_len):
    """造一条和固件同结构的报文; 不够长就靠填充字段补"""
    base = ('{"timestamp":"2026-09-11T10:49:24Z","seq":%d,"heart_rate":114,'
            '"spo2":null,"temp":26.3,"humidity":57.0,"gas_level":16,'
            '"light":1013,"status":"normal","pad":"%s"}')
    pad = ""
    s = base % (seq, pad)
    if len(s) < target_len:
        pad = "x" * (target_len - len(s))
        s = base % (seq, pad)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--topic", default=DEFAULT_TOPIC)
    ap.add_argument("--host", default="bemfa.com")
    ap.add_argument("--port", type=int, default=9501)
    ap.add_argument("--settle", type=float, default=3.0, help="每条发完后等多久再查接口")
    ap.add_argument("--lens", default="",
                    help="自定义长度阶梯, 逗号分隔, 如 160,180,200,220,240")
    ap.add_argument("--short-count", type=int, default=0,
                    help="连发 N 条【极短】报文(长度不变), 用来排除'只记录前几条'的限流假设")
    args = ap.parse_args()

    print("=" * 78)
    print("  报文长度 -> 是否被写入云端记录  (topic=%s)" % args.topic)
    print("=" * 78)

    t0 = datetime.now()
    try:
        tt, mm = api_msg(args.uid, args.topic)
        print("  测试前记录: time=%s  msg=%s" % (tt, str(mm)[:70]))
    except Exception as e:
        print("  查询失败: %s" % e)
        return 2

    p = Pub(args.host, args.port, args.uid)
    try:
        if not p.open():
            print("  连接失败")
            return 2
    except Exception as e:
        print("  连接失败: %s" % e)
        return 2

    # 长度阶梯: 每档带一个唯一标记, 便于精确判断是哪一条被记录
    cases = []
    if args.short_count:
        # 全是短报文(长度几乎不变), 只变 seq —— 若后面的仍旧不被记录,
        # 就证明原因不是长度, 而是限流(短时间内只记录前几条)。
        for i in range(args.short_count):
            cases.append(("S%d-短" % i, '{"seq":%d,"t":"s"}' % (9200 + i)))
    elif args.lens:
        for i, ln in enumerate(args.lens.split(",")):
            ln = int(ln.strip())
            cases.append(("%dB" % ln, board_like_json(9100 + i, ln)))
    else:
        cases.append(("A-短(33B)", '{"seq":9001,"t":"A"}'))
        cases.append(("B-中(~120B)", board_like_json(9002, 0)))          # 自然长度
        cases.append(("C-长(~250B)", board_like_json(9003, 250)))
        cases.append(("D-长(~400B)", board_like_json(9004, 400)))
        cases.append(("E-长(~800B)", board_like_json(9005, 800)))

    results = []
    print("\n  依次发布并回查:")

    for name, payload in cases:
        seq = None
        i = payload.find('"seq":')
        if i >= 0:
            k = i + 6
            while k < len(payload) and payload[k].isdigit():
                k += 1
            seq = payload[i + 6:k]
        marker = '"seq":%s' % seq if seq else None

        p.publish(args.topic, payload)
        time.sleep(args.settle)

        tt, mm = None, None
        try:
            tt, mm = api_msg(args.uid, args.topic)
        except Exception as e:
            print("   [%s] 回查失败: %s" % (name, e))

        hit = (mm is not None and marker is not None and marker in str(mm))
        # 若接口返回的 msg 被截断, 也标出"部分命中"
        partial = (not hit) and (mm is not None and str(seq) in str(mm)) if seq else False
        flag = "记录✓" if hit else ("疑似部分✓" if partial else "记录✗")
        print("   [%-12s] %4d 字节  ->  %s   (云端 msg 长度=%s)"
              % (name, len(payload.encode("utf-8")), flag,
                 len(str(mm)) if mm is not None else "?"))
        if mm is not None:
            print("                  msg=%s" % str(mm)[:100])
        results.append((name, len(payload.encode("utf-8")), hit, partial))

    p.close()

    print("\n" + "=" * 78)
    print("  结论")
    print("=" * 78)
    ok = [r for r in results if r[2]]
    bad = [r for r in results if not r[2]]
    if not bad:
        print("  所有长度都能被记录 → 长度不是原因。")
        print("  那就要回到'板子发的报文本身有别的问题'去查(内容/编码)。")
    elif ok and bad:
        print("  短报文被记录、长报文没被记录:")
        for n, l, _, _ in ok:
            print("     ✓ %-12s %4dB" % (n, l))
        for n, l, _, _ in bad:
            print("     ✗ %-12s %4dB" % (n, l))
        print("  ⇒ 平台对该主题的记录【有长度上限】: 超过阈值的报文被丢弃,")
        print("    但转发仍照做, 所以订阅者收得到、控制台永远看不到。")
        print("    对策: 把上报报文压到阈值以内(精简字段/缩短 key/去掉 null)。")
    else:
        print("  连最短的报文都没被记录 → 与长度无关, 是别的原因。")
    print("\n  测试用时 %.0fs" % (datetime.now() - t0).total_seconds())
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
