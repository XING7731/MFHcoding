#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_compact_payload_check.py —— 验证"换个长度就能被记录"这一结论

背景
    实测(2026-09-14)在 temp001 上做长度阶梯:
        190B/191B 能被写进云端记录, 192B 起就不再写入(只转发, 不记录)。
    因此固件上报报文必须压到 191B 以内。固件已改为紧凑键名。

本脚本做什么
    用 Python 造出【和固件紧凑报文一模一样的字符串】(典型值 + 最坏值),
    真的通过 MQTT 发一次, 再回查云端记录, 确认它确实被记录了。
    这样在板子烧录之前, 就能先证明"改短之后控制台一定看得到"。

    ★ 会短暂顶掉板子(同私钥单连接), 板子会自行重连。
    ★ 不想打扰板子时用 --dry: 只按 utf-8 算长度并判超限, 不发、不连。

用法
    python bemfa_compact_payload_check.py           # 真发真查
    python bemfa_compact_payload_check.py --dry     # 只算长度, 不碰设备
"""

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
LIMIT = 191          # 云端记录上限(实测: 192 起不写入)


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
    url = "http://apis.bemfa.com/vb/api/v2/allTopic?openID=%s&type=1" % uid
    raw = urllib.request.urlopen(url, timeout=10).read().decode("utf-8", "replace")
    j = json.loads(raw)
    for it in ((j.get("data") or {}).get("data") or []):
        if it.get("topic") == topic:
            return it.get("time"), it.get("msg")
    return None, None


# ---------------------------------------------------------------------------
# 固件 build_data_json() 在 NET_JSON_COMPACT=1 下的实际格式。
#
# 【这个字符串必须与固件逐字段一致】否则本脚本"验证"的是另一个报文,
# 长度会偏小, 于是出现"脚本说没超、烧录后控制台却没数据"这种最难查的现象。
#
# 2026-09-22 修正两处偏离:
#   ① 补上 sc/ps/cv —— 它们是 docs/34(门限在线配置) 加的, 本脚本一直漏着,
#      所以此前算出的长度比固件真实值小了约 25B; 而 cv 又恰好是"长度已经
#      很贴上限"的那一段, 漏掉它会给出虚假的安全感。
#   ② 按 docs/37: 去掉 ts/tsv, 加入 d/a/b(PPG 三个原始量)。
#   ③ 按 docs/39: 光照功能整体删除, 去掉 lux 字段。
# ---------------------------------------------------------------------------
FMT = ('{"seq":%d,"hr":%s,"spo2":%s,"sc":%d,"ps":%d,"d":%d,"a":%d,"b":%d,'
       '"cv":%d,"t":%s,"h":%s,"gas":%d,"ax":%s,"ay":%s,"az":%s,'
       '"st":"%s","up":%d}')


def make(seq, hr="null", spo2="null", sc=0, ps=0, d=0, a=0, b=0, cv=38972,
         t="null", h="null", gas=0,
         ax="0.0", ay="0.0", az="0.0", st="normal", up=0):
    return FMT % (seq, hr, spo2, sc, ps, d, a, b, cv,
                  t, h, gas, ax, ay, az, st, up)


CASES = [
    ("典型: 测出心率那一次", make(7001, hr="78", spo2="97", sc=0, ps=3,
                                  d=58234, a=312, b=9, cv=38972,
                                  t="26.3", h="57.0", gas=16,
                                  ax="-0.1", ay="0.0", az="9.8",
                                  st="normal", up=1234567)),
    ("最坏: 所有字段最大位数", make(65535, hr="199", spo2="199", sc=1, ps=3,
                                    d=262143, a=65535, b=40, cv=65535,
                                    t="-100.5", h="100.0", gas=999,
                                    ax="-100.5", ay="-100.5", az="-100.5",
                                    st="fall_detected", up=4294967295)),
    ("没测出: hr=null, d/a/b 有值", make(7003, sc=0, ps=3,
                                         d=128000, a=0, b=0, cv=38972,
                                         t="26.3", h="57.0", gas=16,
                                         ax="-0.1", ay="0.0", az="9.8",
                                         st="normal", up=1234567)),
    ("无数据: null 兜底", make(7004, st="unknown", up=999999)),
]


class Pub(object):
    def __init__(self, host, port, uid):
        self.host, self.port, self.uid = host, port, uid
        self.s = None

    def open(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect((self.host, self.port))
        body = b"\x00\x04MQTT\x04\x02" + (60).to_bytes(2, "big") + enc_str(self.uid)
        self.s.sendall(b"\x10" + enc_rem(len(body)) + body)
        d = self.s.recv(64)
        rc = d[3] if len(d) > 3 else 0xFF
        print("  CONNACK rc=0x%02X %s" % (rc, "接受" if rc == 0 else "被拒绝"))
        return rc == 0

    def publish(self, topic, payload):
        self.s.sendall(b"\x30" + enc_rem(len(enc_str(topic) + payload.encode("utf-8")))
                       + enc_str(topic) + payload.encode("utf-8"))

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def dry_run():
    """只算长度: 不连 MQTT、不发报文, 因此不会顶掉正在跑的板子。"""
    print("=" * 78)
    print("  [--dry] 只算长度, 不连接、不发送")
    print("  云端记录上限 = %d 字节" % LIMIT)
    print("=" * 78)
    bad = 0
    worst = 0
    for name, payload in CASES:
        n = len(payload.encode("utf-8"))
        worst = max(worst, n)
        over = n > LIMIT
        if over:
            bad += 1
        print("    %-26s %3d 字节   余量 %3d  %s"
              % (name, n, LIMIT - n, "★ 超限!" if over else "OK"))
    print("")
    print("  最大一条 = %d 字节 (余量 %d)" % (worst, LIMIT - worst))
    if bad == 0:
        print("  ⇒ 全部在 %d 以内, 烧录后控制台能记录。" % LIMIT)
    else:
        print("  ⇒ 有 %d 条超限, 必须先压缩字段。" % bad)
    print("")
    print("  注: 本模式无法证明'云端真的记录了', 只证明长度够短;")
    print("      要去掉 --dry 实跑一次才能验证端到端。")
    return 0


def main():
    if "--dry" in sys.argv:
        return dry_run()

    print("=" * 78)
    print("  紧凑报文 -> 云端记录 验证   topic=%s" % DEFAULT_TOPIC)
    print("  云端记录上限实测为 %d 字节(超过就不写入)" % LIMIT)
    print("=" * 78)

    p = Pub("bemfa.com", 9501, DEFAULT_UID)
    try:
        if not p.open():
            return 2
    except Exception as e:
        print("  连接失败: %s" % e)
        return 2

    ok_cnt = 0
    for name, payload in CASES:
        n = len(payload.encode("utf-8"))
        p.publish(DEFAULT_TOPIC, payload)
        time.sleep(3.0)
        try:
            tt, mm = api_msg(DEFAULT_UID, DEFAULT_TOPIC)
        except Exception as e:
            print("  回查失败: %s" % e)
            continue
        hit = (mm is not None) and (str(mm) == payload)
        if hit:
            ok_cnt += 1
        print("\n  [%s]" % name)
        print("    报文长度 : %d 字节   (%s)" % (n, "在 %d 以内" % LIMIT if n <= LIMIT else "超过 %d!" % LIMIT))
        print("    云端记录 : %s" % ("✓ 就是这条, 已被记录" if hit else "✗ 没被记录"))
        print("    云端 msg : %s" % str(mm)[:80])
        print("    云端 time: %s" % tt)

    p.close()

    print("\n" + "=" * 78)
    print("  结论")
    print("=" * 78)
    if ok_cnt == len(CASES):
        print("  全部报文都被云端记录 ⇒ 紧凑报文长度安全。")
        print("  烧录新 hex 后, 板子的数据会正常出现在巴法云控制台上。")
    else:
        print("  有报文没被记录(%d/%d) —— 需要进一步压缩字段。" % (ok_cnt, len(CASES)))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
