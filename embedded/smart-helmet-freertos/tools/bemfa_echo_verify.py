#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_echo_verify.py —— 验证「固件新增的"回环确认"判据」到底成不成立

背景（为什么要专门验这一件事）
    板子屏幕显示 "NET: OK", pub 计数一直涨, 但巴法云控制台上没有新数据。
    这有两种完全不同的可能:
        (A) 数据其实上去了, 只是控制台没刷新 / 看错了地方;
        (B) AT+CIPSEND 拿到 "SEND OK" 是【假成功】—— 字节只到了 ESP01S 的
            本地 TCP 栈, 根本没到巴法云。
    光看 pub 计数分不出 A 和 B, 因为 pub 只反映 AT 层。

    固件因此改成"发完一条就等它回环回来": 报文里带自增 seq, 发完在接收缓冲里
    找 "seq":N, —— 找得到就说明这串字节确实经过了 Broker 又回推给了订阅者。
    【这个判据成立的前提是: 巴法云会把设备自己发的消息回推给同一个连接。】
    本脚本就是来确认这个前提的。前提不成立的话, 固件屏幕上会一直显示
    "NO ECHO", 那是误报, 不是板子的问题。

本脚本做四件事
    ① HTTP 查云端当前状态(msg / time / online / pubOnline) —— 只读, 不影响连接
    ② MQTT 连上(ClientID=私钥) + SUBSCRIBE 主题
    ③ PUBLISH 一条带唯一 seq 的报文, 看本连接能不能收到【自己发的】这条
    ④ 再 HTTP 查一次云端状态, 看 msg/time 有没有跟着变

!! 会顶掉板子 !!
    巴法云同一私钥只允许一条 MQTT 连接。本脚本一连上, 正在跑同一私钥的
    开发板就会被踢下线。这是故意为之的验证动作, 不是故障。

用法
    python bemfa_echo_verify.py
    python bemfa_echo_verify.py --topic temp001 --uid <私钥>
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

PKT_CONNACK = 2
PKT_PUBLISH = 3
PKT_SUBACK = 9
PKT_PINGRESP = 13


def rule(title):
    print("\n" + "=" * 72)
    print("  " + title)
    print("=" * 72)


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


class Mqtt:
    def __init__(self, sock):
        self.s = sock
        self.buf = b""

    def _need(self, n, timeout):
        end = time.time() + timeout
        while len(self.buf) < n:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("timeout")
            self.s.settimeout(left)
            chunk = self.s.recv(4096)
            if not chunk:
                raise ConnectionError("peer closed")
            self.buf += chunk

    def read_packet(self, timeout=5.0):
        try:
            self._need(1, timeout)
        except (TimeoutError, ConnectionError):
            return None, b""
        hdr = self.buf[0]
        self.buf = self.buf[1:]
        mult, rl = 1, 0
        try:
            while True:
                self._need(1, timeout)
                b = self.buf[0]
                self.buf = self.buf[1:]
                rl += (b & 0x7F) * mult
                mult *= 128
                if not (b & 0x80):
                    break
            self._need(rl, timeout)
        except (TimeoutError, ConnectionError):
            return None, b""
        body = self.buf[:rl]
        self.buf = self.buf[rl:]
        return hdr, body

    def connect(self, client_id, keepalive=30):
        vh = b"\x00\x04MQTT\x04\x02" + keepalive.to_bytes(2, "big")
        body = vh + enc_str(client_id)
        self.s.sendall(b"\x10" + enc_rem(len(body)) + body)
        hdr, body = self.read_packet(10.0)
        if hdr is None or (hdr >> 4) != PKT_CONNACK:
            return None
        return body[1] if len(body) >= 2 else 0xFF

    def subscribe(self, topic, msgid=1, qos=0):
        body = msgid.to_bytes(2, "big") + enc_str(topic) + bytes([qos])
        self.s.sendall(b"\x82" + enc_rem(len(body)) + body)
        hdr, body = self.read_packet(8.0)
        if hdr is None or (hdr >> 4) != PKT_SUBACK:
            return None
        return body[-1] if body else 0xFF

    def publish(self, topic, payload):
        body = enc_str(topic) + payload
        self.s.sendall(b"\x30" + enc_rem(len(body)) + body)

    def drain(self, seconds):
        out = []
        end = time.time() + seconds
        while True:
            left = end - time.time()
            if left <= 0:
                break
            hdr, body = self.read_packet(min(left, 1.0))
            if hdr is None:
                continue
            t = hdr >> 4
            if t == PKT_PUBLISH and len(body) >= 2:
                tl = int.from_bytes(body[:2], "big")
                out.append((body[2:2 + tl].decode("utf-8", "replace"),
                            body[2 + tl:]))
        return out


def cloud_state(uid, topic):
    """HTTP 只读查询: 拿到该主题的 msg/time/online/pubOnline"""
    url = "http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid
    try:
        raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8")
        j = json.loads(raw)
    except Exception as e:
        return ("ERR", str(e))
    for it in (j.get("data", {}) or {}).get("data", []) or []:
        if it.get("topic") == topic:
            return it
    return "NOTFOUND"


def show_state(tag, it):
    if it == "NOTFOUND":
        print("  [%s] 主题不存在于该私钥的 MQTT 控制台" % tag)
        return None
    if isinstance(it, tuple):
        print("  [%s] 查询失败: %s" % (tag, it[1]))
        return None
    a = None
    try:
        a = (datetime.now() - datetime.strptime(it.get("time"), "%Y-%m-%d %H:%M:%S")).total_seconds()
    except Exception:
        pass
    print("  [%s] online=%-5s pubOnline=%-5s time=%s (%s前)"
          % (tag, it.get("online"), it.get("pubOnline"), it.get("time"),
             ("%.0fs" % a) if a is not None else "?"))
    print("  [%s] 云端保存的 msg = %s" % (tag, str(it.get("msg"))[:110]))
    return it


def main():
    ap = argparse.ArgumentParser(description="验证巴法云是否会回推设备自己发的消息")
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--topic", default=DEFAULT_TOPIC)
    ap.add_argument("--host", default="bemfa.com")
    ap.add_argument("--port", type=int, default=9501)
    ap.add_argument("--wait", type=float, default=6.0, help="发布后等待回环的秒数")
    args = ap.parse_args()

    seq = int(time.time()) % 100000
    marker = '{"seq":%d,"source":"echo_verify"}' % seq
    needle = '"seq":%d,' % seq

    print("巴法云「回环确认」判据验证")
    print("  topic = %s   uid = %s..." % (args.topic, args.uid[:8]))
    print("  本次发布的报文: %s" % marker)
    print("  要查找的特征串: %s" % needle)
    print("\n  !! 本脚本会以同一私钥连上云端, 把正在跑的开发板顶下线(正常现象) !!")

    # ---------- ① 连接前的云端状态 ----------
    rule("① 连接前: 云端当前状态(HTTP 只读, 不影响任何连接)")
    before = show_state("前", cloud_state(args.uid, args.topic))
    if before is None:
        print("\n  结论: 主题查不到, 先确认主题建在 MQTT 控制台里。")
        return 2

    # ---------- ② TCP + CONNECT ----------
    rule("② TCP + MQTT CONNECT")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((args.host, args.port))
        print("  TCP 已连接 %s:%d" % (args.host, args.port))
    except Exception as e:
        print("  TCP 连接失败: %s" % e)
        return 2

    c = Mqtt(s)
    rc = c.connect(args.uid)
    if rc is None:
        print("  没收到 CONNACK")
        return 3
    print("  CONNACK rc=0x%02X %s" % (rc, "接受" if rc == 0 else "被拒绝"))
    if rc != 0:
        return 3

    # ---------- ③ SUBSCRIBE ----------
    rule("③ SUBSCRIBE 主题")
    sub_rc = c.subscribe(args.topic)
    print("  SUBACK = %s" % ("0x%02X" % sub_rc if sub_rc is not None else "无应答"))
    if sub_rc is None or sub_rc > 2:
        print("  订阅失败 → 主题名不对或未在 MQTT 控制台创建")
        return 3
    print("  订阅成功 → 云端现在应显示 online=true")

    # ---------- ④ 发布 + 等回环 ----------
    rule("④ 发布一条报文, 看能不能收到【自己发的】这条")
    c.publish(args.topic, marker.encode("utf-8"))
    print("  已发布, 等待 %.0f 秒..." % args.wait)
    got = c.drain(args.wait)

    echo_ok = False
    for t, p in got:
        txt = p.decode("utf-8", "replace")
        mark = ""
        if needle in txt:
            echo_ok = True
            mark = "   <<< 这就是自己刚发的那条"
        print("  收到 topic=%s payload=%s%s" % (t, txt[:130], mark))

    if not got:
        print("  这段时间内没有收到任何下行报文")

    print()
    if echo_ok:
        print("  ★ 回环成立: 巴法云确实会把设备自己发的消息回推给同一个连接。")
        print("    → 固件的『回环确认』判据有效: 屏幕上 e 计数会涨, 涨了就说明真的到了云端;")
        print("      若 e 一直为 0 而 p 在涨, 那就是货真价实的『假成功』。")
    else:
        print("  ✗ 没有收到自己发的回环。")
        print("    → 固件的『回环确认』判据在此账号/主题上不成立, 屏幕会误报 NO ECHO。")
        print("      此时应把 user_config.h 的 NET_ECHO_CONFIRM_ENABLE 置 0,")
        print("      改用『HTTP 查云端 msg/time 是否在变』作为唯一判据。")

    # ---------- ⑤ 连接后的云端状态 ----------
    rule("⑤ 连接后: 云端状态是否跟着变(这是最直接的『数据到没到』证据)")
    time.sleep(1.5)
    after = show_state("后", cloud_state(args.uid, args.topic))

    if before and after and not isinstance(after, tuple):
        if after.get("time") != before.get("time"):
            print("\n  ★ time 从 %s 变成 %s —— 云端已收到并保存了新数据。"
                  % (before.get("time"), after.get("time")))
        else:
            print("\n  · time 没有变化(可能云端有延迟, 稍等几秒再查一次)")
        if after.get("msg") != before.get("msg"):
            print("  ★ msg 已更新 —— 报文内容确实被云端保存了。")

    rule("判读表: 以后遇到『板子显示 OK 但云端没数据』该怎么看")
    print("  p 涨 + e 涨            → 数据真的到了云端, 去控制台看就行")
    print("  p 涨 + e 恒为 0        → 假成功。头号原因是同一私钥被电脑上的调试")
    print("                         客户端顶掉了(巴法云同 UID 只允许一条连接)。")
    print("                         固件会在连续 %d 次后自动断开重连自愈。")
    print("  e 恒为 0 但本脚本也说『回环不成立』 → 关掉 NET_ECHO_CONFIRM_ENABLE")
    print("                         改用本脚本第 ⑤ 步这种 HTTP 查询来判断。")

    s.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
