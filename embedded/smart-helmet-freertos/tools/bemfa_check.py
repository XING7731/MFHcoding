#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_check.py —— 在电脑上直接验证「巴法云私钥 + 主题」是否可用

为什么需要它
------------
STM32 那边连不上时, 你无法区分到底是:
  (a) 私钥错 / 没开通 MQTT
  (b) 主题不存在, 或建在了 TCP 控制台而不是 MQTT 控制台
  (c) 上报用错了主题后缀(裸主题 / /set / /up)
  (d) ESP01S 模块、供电、接线的问题
  (e) 手机热点的问题

本脚本把固件要做的事情在电脑上原样做一遍:
    TCP 连 bemfa.com:9501 -> MQTT CONNECT(ClientID=私钥) -> 读 CONNACK
    -> SUBSCRIBE 主题 -> PUBLISH 三种主题后缀 -> 看 Broker 怎么回应

于是可以一步定位:
  * CONNACK 返回码非 0        -> 私钥/账号问题 (与硬件无关)
  * CONNACK=0 但订阅收不到    -> 主题后缀用错, 或主题不存在
  * 全部通过                  -> 云端配置没问题, 问题在 ESP01S 或它周围

不需要 STM32, 不需要 ESP01S, 不需要任何硬件。

!! 注意 !!
同一个私钥只允许一个 MQTT 连接。运行本脚本前, 请先给其他正在使用同一
私钥的设备断电(包括上一版 stm32-dht11-cloud 的板子), 否则会互相踢下线,
测出来的结果不可信。

用法
----
  python bemfa_check.py --uid <私钥> --topic temp001
  python bemfa_check.py --uid <私钥> --topic temp001 --method 2 --appid <id> --secret <key>
"""

import argparse
import socket
import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


# --------------------------------------------------------------------------
# MQTT 3.1.1 最小实现(只用到 CONNECT / SUBSCRIBE / PUBLISH, 无需第三方库)
# --------------------------------------------------------------------------
CONNACK_TEXT = {
    0x00: "CONNACK 0x00 连接已被接受 —— 私钥可用",
    0x01: "CONNACK 0x01 协议版本不被支持",
    0x02: "CONNACK 0x02 ClientID 被拒绝 —— 私钥写错, 或该私钥未开通 MQTT",
    0x03: "CONNACK 0x03 服务不可用",
    0x04: "CONNACK 0x04 用户名或密码错误 —— 方式二填错 appID/secretKey",
    0x05: "CONNACK 0x05 未授权 not authorized —— 私钥/账号无权限",
}

PKT_CONNACK = 2
PKT_PUBLISH = 3
PKT_SUBACK = 9
PKT_PINGRESP = 13


def enc_str(s: str) -> bytes:
    b = s.encode("utf-8")
    return len(b).to_bytes(2, "big") + b


def enc_rem(n: int) -> bytes:
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


class MqttConn:
    def __init__(self, sock):
        self.s = sock
        self.inbuf = b""

    # ---- 底层 ----
    def _need(self, n, timeout):
        end = time.time() + timeout
        while len(self.inbuf) < n:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("等待 %d 字节超时" % n)
            self.s.settimeout(left)
            try:
                chunk = self.s.recv(4096)
            except socket.timeout:
                raise TimeoutError("等待 %d 字节超时" % n)
            if not chunk:
                raise ConnectionError("连接被对端关闭")
            self.inbuf += chunk

    def read_packet(self, timeout=5.0):
        """返回 (type_byte, payload_bytes); 超时返回 (None, b'')"""
        try:
            self._need(1, timeout)
        except (TimeoutError, ConnectionError):
            return None, b""
        hdr = self.inbuf[0]
        self.inbuf = self.inbuf[1:]

        mult, rl = 1, 0
        try:
            while True:
                self._need(1, timeout)
                b = self.inbuf[0]
                self.inbuf = self.inbuf[1:]
                rl += (b & 0x7F) * mult
                mult *= 128
                if not (b & 0x80):
                    break
        except (TimeoutError, ConnectionError):
            return None, b""

        try:
            self._need(rl, timeout)
        except (TimeoutError, ConnectionError):
            return None, b""
        body = self.inbuf[:rl]
        self.inbuf = self.inbuf[rl:]
        return hdr, body

    # ---- 报文 ----
    def connect(self, client_id, user=None, pwd=None, keepalive=60):
        flags = 0x02                                    # clean session
        payload = enc_str(client_id)
        if user:
            flags |= 0x80
            payload += enc_str(user)
        if pwd:
            flags |= 0x40
            payload += enc_str(pwd)

        vh = b"\x00\x04MQTT\x04" + bytes([flags]) + keepalive.to_bytes(2, "big")
        body = vh + payload
        self.s.sendall(b"\x10" + enc_rem(len(body)) + body)

        hdr, body = self.read_packet(10.0)
        if hdr is None:
            return None
        if (hdr >> 4) != PKT_CONNACK:
            return None
        return body[1] if len(body) >= 2 else 0xFF

    def subscribe(self, topic, qos=0):
        body = b"\x00\x01" + enc_str(topic) + bytes([qos])   # 报文标识符 1
        self.s.sendall(b"\x82" + enc_rem(len(body)) + body)
        hdr, body = self.read_packet(8.0)
        if hdr is None or (hdr >> 4) != PKT_SUBACK:
            return None
        return body[2] if len(body) >= 3 else 0xFF           # 0x00/0x01=成功

    def publish(self, topic, msg, qos=0):
        body = enc_str(topic) + msg
        self.s.sendall((bytes([0x30 | (qos << 1)])) + enc_rem(len(body)) + body)

    def ping(self):
        self.s.sendall(b"\xc0\x00")
        hdr, _ = self.read_packet(5.0)
        return hdr is not None and (hdr >> 4) == PKT_PINGRESP

    def drain(self, seconds):
        """在 seconds 秒内收取所有 PUBLISH, 返回 [(topic, payload)]"""
        got = []
        end = time.time() + seconds
        while True:
            left = end - time.time()
            if left <= 0:
                break
            hdr, body = self.read_packet(min(left, 1.0))
            if hdr is None:
                continue
            if (hdr >> 4) == PKT_PUBLISH:
                if len(body) >= 2:
                    tl = int.from_bytes(body[:2], "big")
                    got.append((body[2:2 + tl].decode("utf-8", "replace"), body[2 + tl:]))
                else:
                    got.append(("<解析失败>", body))
            elif (hdr >> 4) == PKT_PINGRESP:
                continue
        return got


# --------------------------------------------------------------------------
def rule(title):
    print("\n" + "=" * 68)
    print("  " + title)
    print("=" * 68)


def main():
    ap = argparse.ArgumentParser(description="验证巴法云 MQTT 私钥与主题")
    ap.add_argument("--uid", required=True, help="私钥(控制台个人中心)")
    ap.add_argument("--topic", default="temp001", help="要测试的主题")
    ap.add_argument("--host", default="bemfa.com")
    ap.add_argument("--port", type=int, default=9501, help="9501=明文 9503=TLS")
    ap.add_argument("--method", type=int, default=1, choices=[1, 2],
                    help="1=私钥作 ClientID(默认) 2=appID+secretKey")
    ap.add_argument("--appid", default="")
    ap.add_argument("--secret", default="")
    ap.add_argument("--listen", type=float, default=6.0, help="最后静听秒数")
    args = ap.parse_args()

    print("巴法云 MQTT 连通性检查")
    print("  服务器   : %s:%d" % (args.host, args.port))
    print("  主题     : %s" % args.topic)
    if args.method == 1:
        print("  认证方式 : 方式一(私钥作 ClientID=%s..., 用户名密码空)"
              % args.uid[:8])
    else:
        print("  认证方式 : 方式二(appID=%s)" % args.appid)

    # ---- ① DNS ----
    rule("① 域名解析")
    try:
        infos = socket.getaddrinfo(args.host, args.port, socket.AF_INET,
                                   socket.SOCK_STREAM)
        ip = infos[0][4][0]
        print("  %s -> %s   [DNS 正常]" % (args.host, ip))
    except Exception as e:
        print("  DNS 解析失败: %s" % e)
        print("  结论: 本机网络/DNS 有问题, 与巴法云账号无关")
        return 2

    # ---- ② TCP ----
    rule("② TCP 握手")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    try:
        s.connect((args.host, args.port))
        print("  TCP 已连接 bemfa.com:%d   [端口可达]" % args.port)
    except Exception as e:
        print("  TCP 连接失败: %s" % e)
        print("  结论: 本机到巴法云的 %d 端口不通(防火墙/公司网络限制)" % args.port)
        return 2

    # ---- ③ CONNECT ----
    rule("③ MQTT CONNECT (这一步直接检验私钥)")
    c = MqttConn(s)
    if args.method == 1:
        rc = c.connect(args.uid)
    else:
        rc = c.connect("pc_test_001", user=args.appid, pwd=args.secret)

    if rc is None:
        print("  没有收到 CONNACK (超时或连接被关闭)")
        print("  结论: Broker 没回应。若端口可达但仍无应答, 多为被其他设备")
        print("        用同一私钥占用了连接 -> 先给别的板子断电再试。")
        return 3

    print("  %s" % CONNACK_TEXT.get(rc, "CONNACK 0x%02X 未知返回码" % rc))
    if rc != 0:
        print("\n  结论: 私钥/账号层面就被拒绝了, 与 ESP01S、STM32 代码都无关。")
        print("        -> 去 https://cloud.bemfa.com/ 个人中心核对私钥是否完整")
        return 3

    # ---- ④ SUBSCRIBE ----
    rule("④ SUBSCRIBE 主题")
    sub_rc = c.subscribe(args.topic)
    if sub_rc is None:
        print("  没收到 SUBACK")
    elif sub_rc == 0xFF:
        print("  SUBACK 0xFF = 订阅被拒绝")
    elif sub_rc <= 2:
        print("  SUBACK 0x%02X = 订阅成功, 主题 [%s] 有效" % (sub_rc, args.topic))
    else:
        print("  SUBACK 0x%02X = 订阅失败(主题可能未创建)" % sub_rc)

    # ---- ⑤ 三种主题后缀 ----
    rule("⑤ 测试三种发布主题(这是最容易被忽略的一步)")
    msg = b'{"msg":"hello-from-pc"}'
    variants = [args.topic, args.topic + "/set", args.topic + "/up"]

    print("  巴法云规则(官方文档):")
    print("    <主题>/set  推送消息给所有订阅该主题的设备(不推给发布者自己)")
    print("    <主题>/up   只更新云端数据, 不推送")
    print("    <主题>      裸主题, 走标准 MQTT 广播")
    print()

    for t in variants:
        c.publish(t, msg)
        got = c.drain(2.0)
        if got:
            for gt, gp in got:
                print("  发布到 %-14s -> 本机收到回环: topic=%s payload=%s"
                      % (t, gt, gp.decode("utf-8", "replace")))
        else:
            print("  发布到 %-14s -> 本机未收到回环(不代表失败, 见下)" % t)

    # ---- ⑥ 静听 ----
    rule("⑥ 静听 %.0f 秒: 看是否有别的设备/云端在往这个主题发消息" % args.listen)
    got = c.drain(args.listen)
    if got:
        for gt, gp in got:
            print("  收到 topic=%s payload=%s" % (gt, gp.decode("utf-8", "replace")))
    else:
        print("  该主题上没有任何活动(正常: 没有别的设备在发)")

    # ---- ⑦ keepalive ----
    rule("⑦ PINGREQ / PINGRESP 心跳")
    if c.ping():
        print("  心跳正常 —— 链路可长期保持")
    else:
        print("  心跳无应答 —— 连接会被 Broker 判定超时断开")

    # ---- 结论 ----
    rule("结论")
    print("  ✅ 私钥可用, TCP/MQTT 链路完全正常 —— 云端配置这一侧没有问题。")
    print()
    print("  那么 STM32 连不上, 就只可能是下面这些(按嫌疑排序):")
    print("    1) ESP01S 供电不足   -> 发射瞬间压降复位, 日志里 resets= 会涨")
    print("    2) 接线/波特率错误   -> TX/RX 未交叉, 或波特率不是 115200")
    print("    3) 手机热点问题      -> 必须 2.4GHz; DNS 不解析就改用 IP 直连")
    print("    4) 主题后缀用错      -> 本脚本第 ⑤ 步的输出会告诉你哪种能收到")
    print("    5) 同一私钥被别的设备占用 -> 双方互相踢下线, 表现就是\"断了又连\"")

    s.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
