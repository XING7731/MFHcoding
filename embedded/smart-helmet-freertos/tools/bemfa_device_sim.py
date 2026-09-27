#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_device_sim.py —— 在电脑上完整复刻"安全帽固件"的联网行为

用途
    本脚本做的事, 和 STM32 固件里 net_connect() + WifiTask 做的事**完全一致**:
        ① TCP 连 bemfa.com:9501
        ② MQTT CONNECT (clientId = 你的私钥, CleanSession=1)
        ③ SUBSCRIBE 主题  (== 固件新增的第 5 步, 决定控制台显示在线/离线)
        ④ 周期 PUBLISH  (默认 5s 一次, 模拟设备的 30s 上报)
    每一步之后都去查巴法云设备接口, 把 online / pubOnline / msg 打出来,
    于是能亲眼看到"控制台在线图标是被哪一步点亮的"。

为什么有用
    它把"固件问题"和"云端配置问题"彻底切开:
      · 本脚本跑通 → 说明云端配置、私钥、主题全对;
        此时板子还离线, 就一定是板子没按这套逻辑执行(没烧录/订阅失败/没连上)。
      · 本脚本也跑不通 → 说明问题在云端配置或网络, 与 STM32 代码无关。

⚠ 同一私钥只允许一个 MQTT 连接
    运行前请把开发板断电(或至少确认它没在连), 否则两边会互相顶下线,
    观察到的 online 状态会不断跳变, 无法判读。

用法
    python bemfa_device_sim.py                        # 用默认私钥/主题, 跑 6 轮
    python bemfa_device_sim.py --topic helmet001      # 换主题
    python bemfa_device_sim.py --rounds 12 --period 5 # 跑 1 分钟
    python bemfa_device_sim.py --no-subscribe         # 对照: 只发布不订阅(复现"离线")
"""

import argparse
import json
import socket
import sys
import time
import urllib.request

DEFAULT_UID = "d2f5cf227188d137f8171da8611a7614"
DEFAULT_TOPIC = "temp001"
BROKER = "bemfa.com"
PORT = 9501


# ----------------------------------------------------------------------------
# 云端状态查询
# ----------------------------------------------------------------------------
def query_online(uid, topic):
    """查巴法云主题状态, 返回 (online, pubOnline, msg, time) —— 失败返回 None"""
    url = ("http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid)
    try:
        raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8")
        j = json.loads(raw)
        for it in (j.get("data", {}) or {}).get("data", []) or []:
            if it.get("topic") == topic:
                return (it.get("online"), it.get("pubOnline"),
                        it.get("msg"), it.get("time"))
    except Exception as e:
        print("    [API] 查询失败: %s" % e)
    return None


def show_state(uid, topic, tag):
    st = query_online(uid, topic)
    if st is None:
        print("  %-22s | 查询失败" % tag)
        return None
    online, pubo, msg, t = st
    print("  %-22s | online=%-5s pubOnline=%-5s | msg=%s"
          % (tag, online, pubo, (msg or "")[:44]))
    return st


# ----------------------------------------------------------------------------
# 极简 MQTT 报文构造(与固件 mqtt_client.c 的字节布局一致)
# ----------------------------------------------------------------------------
def enc_len(n):
    out = bytearray()
    while True:
        d = n % 128
        n //= 128
        if n > 0:
            d |= 0x80
        out.append(d)
        if n == 0:
            break
    return bytes(out)


def pkt_connect(client_id, keepalive=30):
    vh = b"\x00\x04MQTT\x04\x02" + keepalive.to_bytes(2, "big")
    pl = len(client_id).to_bytes(2, "big") + client_id.encode()
    body = vh + pl
    return b"\x10" + enc_len(len(body)) + body


def pkt_subscribe(topic, mid=1, qos=0):
    body = mid.to_bytes(2, "big") + len(topic).to_bytes(2, "big") \
           + topic.encode() + bytes([qos])
    return b"\x82" + enc_len(len(body)) + body


def pkt_publish(topic, payload):
    body = len(topic).to_bytes(2, "big") + topic.encode() + payload
    return b"\x30" + enc_len(len(body)) + body


def pkt_pingreq():
    return b"\xc0\x00"


# ----------------------------------------------------------------------------
# 主流程
# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--topic", default=DEFAULT_TOPIC)
    ap.add_argument("--rounds", type=int, default=6, help="发布轮数")
    ap.add_argument("--period", type=float, default=5.0, help="每轮间隔秒")
    ap.add_argument("--no-subscribe", action="store_true",
                    help="对照实验: 跳过 SUBSCRIBE, 复现'只有发布所以显示离线'")
    args = ap.parse_args()

    uid, topic = args.uid, args.topic

    print("=" * 74)
    print(" 巴法云设备模拟器 —— 复刻安全帽固件的完整联网流程")
    print(" broker=%s:%d  uid=%s  topic=%s" % (BROKER, PORT, uid[:8] + "...", topic))
    print(" 订阅=%s  轮数=%d  间隔=%.0fs"
          % ("关闭(对照实验)" if args.no_subscribe else "开启",
             args.rounds, args.period))
    print("=" * 74)
    print()
    print("【第 0 步】运行前的云端状态(此时板子与脚本都没连)")
    show_state(uid, topic, "基线")
    print()

    # ---- ① TCP ----
    print("【第 1 步】TCP 连接 %s:%d …" % (BROKER, PORT))
    try:
        s = socket.create_connection((BROKER, PORT), timeout=15)
    except Exception as e:
        print("  ✗ TCP 连接失败: %s" % e)
        print("  → 电脑都连不上, 说明是网络/运营商问题, 与 STM32 代码无关。")
        return 2
    s.settimeout(6)
    print("  ✓ TCP 已建立 (本机端口 %d)" % s.getsockname()[1])
    print()

    try:
        # ---- ② CONNECT ----
        print("【第 2 步】MQTT CONNECT (clientId=私钥, CleanSession=1) …")
        s.sendall(pkt_connect(uid))
        r = s.recv(64)
        print("  收到: %s" % r.hex(" "))
        if len(r) >= 4 and r[0] == 0x20:
            rc = r[3]
            print("  %s CONNACK 返回码 = 0x%02X (%s)"
                  % ("✓" if rc == 0 else "✗", rc,
                     "accepted" if rc == 0 else
                     {1: "协议版本不支持", 2: "ClientID 被拒",
                      3: "服务不可用", 4: "账号密码错", 5: "未授权"}.get(rc, "未知")))
            if rc != 0:
                print("  → 巴法云明确拒绝连接, 与硬件无关。检查私钥是否正确。")
                return 3
        else:
            print("  ✗ 没收到 CONNACK")
            return 3
        print()

        # ---- ③ SUBSCRIBE ----
        if not args.no_subscribe:
            print("【第 3 步】SUBSCRIBE %s (QoS0) —— 这一步点亮控制台的'在线' …" % topic)
            s.sendall(pkt_subscribe(topic))
            r = s.recv(64)
            print("  收到: %s" % r.hex(" "))
            if len(r) >= 3 and r[0] == 0x90 and r[2] == 0x00:
                print("  ✓ SUBACK 返回码 0x00 —— 订阅成功, 控制台应立刻显示【在线】")
            else:
                print("  ✗ SUBACK 异常: 0x80=主题不存在/无权限, 0x00 之外都不是成功")
            print()
            st = show_state(uid, topic, "订阅后立即查")
            if st and st[0] is True:
                print("  ★ 结论: online 已翻成 True —— 订阅就是'在线'的开关。")
            print()
        else:
            print("【第 3 步】已跳过 SUBSCRIBE (对照实验)")
            print("  → 预期: 后面数据能发上去(pubOnline=true), 但 online 永远是 False。")
            print()

        # ---- ④ 周期 PUBLISH ----
        print("【第 4 步】周期 PUBLISH 模拟数据(%d 轮, 每 %.0fs 一次) …"
              % (args.rounds, args.period))
        payload = ('{"msg":"device-sim","temp":26.5,"humi":58,'
                   '"hr":72,"note":"PC 模拟, 非真实板子"}')
        for i in range(1, args.rounds + 1):
            s.sendall(pkt_publish(topic, payload.encode()))
            if i % 2 == 1:
                s.sendall(pkt_pingreq())          # 顺带验证心跳
            st = show_state(uid, topic, "第 %d 轮发布后" % i)
            if st:
                print("  %-22s | msg 内容 = %s"
                      % ("", (st[2] or "(空)")[:56]))
                print("  %-22s | 云端 time = %s" % ("", st[3]))
            if i < args.rounds:
                time.sleep(args.period)
        print()

        # ---- ⑤ 断开 ----
        print("【第 5 步】断开 TCP, 观察状态回落 …")
        s.sendall(b"\xe0\x00")     # DISCONNECT
        time.sleep(0.5)
        s.close()
    finally:
        try:
            s.close()
        except Exception:
            pass

    time.sleep(3)
    show_state(uid, topic, "断开后查")
    print()
    print("=" * 74)
    print(" 判读")
    print("   · 订阅后 online=True 且各轮 pubOnline=True → 这套逻辑完全正确。")
    print("     板子若仍离线, 就是板子没执行到这里: 没烧录新固件 / 订阅失败 / 没连上。")
    print("   · 若 -no-subscribe 对照下 online 始终 False → 证实'在线'由订阅决定。")
    print("=" * 74)
    return 0


if __name__ == "__main__":
    sys.exit(main())
