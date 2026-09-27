#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bemfa_tap.py —— 在主题上"听"一段时间, 看板子到底有没有在【实时上报】

要回答的问题
    板子屏幕显示 "NET: OK p=26 e=26" 且两个数都在涨, 但控制台上看不到新数据。
    有两种可能, 必须分开:
        (A) 板子真的在往巴法云发 —— 那么只要有人在主题上听, 就会【连续】收到
            它的报文, 而且 seq 会一条比一条大(每 30s 一条);
        (B) 板子根本没发上去 —— 那么即使听到, 也只是巴法云为该主题【保留
            的那一条历史报文】(retained), seq 不会变。

    所以判据很干脆: 听 80 秒, 看能不能收到【seq 递增的多条】报文。

    ★ 只收到 1 条、且 seq 始终不变  → 是保留的历史报文, 板子没在实时上报
    ★ 收到 2 条以上、seq 递增      → 板子在实时上报, 控制台看不到是别的原因

!! 关于"控制台看不到数据"的真正原因(2026-09-14 实测) !!
    巴法云该主题的记录字段只有 191 字节: 191 字节能写入, 192 字节起就不再写入。
    超过上限的报文【转发照做、记录不写】—— 订阅者(本脚本)收得完整, 设备侧回环
    确认也成功, 唯独控制台/HTTP 接口的 msg 永远停在旧值上。
    所以本脚本现在会把每条报文的字节数打出来, 超过 191 就当场提醒。
    固件已改为紧凑报文(约 150 字节), 见 docs/18。

!! 会顶掉板子 !!
    巴法云同一私钥只允许一条 MQTT 连接, 本脚本一连上就把板子踢下线。
    这是"听"这个动作无法避免的代价(HTTP 接口听不到实时报文)。
    板子察觉到断线后会自己重连, 那时它会把本脚本踢回来 ——
    脚本会把这个"被抢回"的现象也打印出来, 它本身也是一条证据。

用法
    python bemfa_tap.py                 # 听 80 秒
    python bemfa_tap.py --seconds 180
"""

import argparse
import socket
import sys
import time
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


def stamp():
    return datetime.now().strftime("%H:%M:%S")


class Tap(object):
    def __init__(self, host, port, uid, topic):
        self.host, self.port, self.uid, self.topic = host, port, uid, topic
        self.s = None
        self.buf = b""

    def open(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect((self.host, self.port))
        self.buf = b""
        vh = b"\x00\x04MQTT\x04\x02" + (30).to_bytes(2, "big")
        body = vh + enc_str(self.uid)
        self.s.sendall(b"\x10" + enc_rem(len(body)) + body)
        hdr, body2 = self.read(10.0)
        if hdr is None or (hdr >> 4) != PKT_CONNACK:
            print("  [%s] 没收到 CONNACK" % stamp())
            return False
        rc = body2[1] if len(body2) >= 2 else 0xFF
        print("  [%s] CONNACK rc=0x%02X %s" % (stamp(), rc, "接受" if rc == 0 else "被拒绝"))
        if rc != 0:
            return False
        body3 = (1).to_bytes(2, "big") + enc_str(self.topic) + bytes([0])
        self.s.sendall(b"\x82" + enc_rem(len(body3)) + body3)
        hdr, body4 = self.read(8.0)
        if hdr is None or (hdr >> 4) != PKT_SUBACK:
            print("  [%s] 没收到 SUBACK" % stamp())
            return False
        print("  [%s] SUBACK=0x%02X  已订阅 %s" % (stamp(), body4[-1] if body4 else 0xFF, self.topic))
        return True

    def _fill(self, timeout):
        end = time.time() + timeout
        while len(self.buf) < 1:
            left = end - time.time()
            if left <= 0:
                return False
            self.s.settimeout(left)
            chunk = self.s.recv(4096)
            if not chunk:
                return False
            self.buf += chunk
        return True

    def read(self, timeout):
        try:
            if not self._fill(timeout):
                return None, b""
            hdr = self.buf[0]
            self.buf = self.buf[1:]
            mult, rl = 1, 0
            while True:
                if not self._fill(timeout):
                    return None, b""
                b = self.buf[0]
                self.buf = self.buf[1:]
                rl += (b & 0x7F) * mult
                mult *= 128
                if not (b & 0x80):
                    break
            while len(self.buf) < rl:
                if not self._fill(timeout):
                    return None, b""
            body = self.buf[:rl]
            self.buf = self.buf[rl:]
            return hdr, body
        except (socket.timeout, OSError):
            return None, b""

    def listen(self, seconds):
        """返回 (收到的报文列表, 是否被对面断开)"""
        seen = []
        end = time.time() + seconds
        while time.time() < end:
            hdr, body = self.read(min(1.0, max(0.05, end - time.time())))
            if hdr is None:
                # 判断是"超时无数据"还是"连接被关"
                try:
                    self.s.settimeout(0.01)
                    if self.s.recv(1) == b"":
                        return seen, True
                except socket.timeout:
                    pass
                except OSError:
                    return seen, True
                continue
            if (hdr >> 4) == PKT_PUBLISH and len(body) >= 2:
                tl = int.from_bytes(body[:2], "big")
                topic = body[2:2 + tl].decode("utf-8", "replace")
                payload = body[2 + tl:].decode("utf-8", "replace")
                seen.append((time.time(), topic, payload))
                nb = len(payload.encode("utf-8"))
                print("  [%s] 收到 topic=%s  长度=%d 字节%s"
                      % (stamp(), topic, nb,
                         "  ⚠ 超过 191 → 云端记录会写不进去!" if nb > 191 else ""))
                print("           %s" % payload[:200])
        return seen, False


def seq_of(payload):
    """从 payload 里抠出 "seq":N 的 N, 没有则返回 None"""
    i = payload.find('"seq":')
    if i < 0:
        return None
    j = i + 6
    k = j
    while k < len(payload) and payload[k].isdigit():
        k += 1
    if k == j:
        return None
    try:
        return int(payload[j:k])
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser(description="在主题上听一段时间, 判断板子是否在实时上报")
    ap.add_argument("--uid", default=DEFAULT_UID)
    ap.add_argument("--topic", default=DEFAULT_TOPIC)
    ap.add_argument("--host", default="bemfa.com")
    ap.add_argument("--port", type=int, default=9501)
    ap.add_argument("--seconds", type=float, default=80.0)
    args = ap.parse_args()

    print("=" * 74)
    print("  在 %s 上听 %.0f 秒" % (args.topic, args.seconds))
    print("  判据: 收到多条且 seq 递增 = 板子在实时上报;")
    print("        只收到 1 条且 seq 不变 = 那只是保留的历史报文, 板子没发上去")
    print("  !! 本脚本会踢掉板子, 这是[听]的必要代价 !!")
    print("=" * 74)

    t = Tap(args.host, args.port, args.uid, args.topic)
    try:
        if not t.open():
            print("\n  打不开连接, 直接结束")
            return 2
    except Exception as e:
        print("  连接失败: %s" % e)
        return 2

    print("\n  开始监听...\n")
    seen, dropped = t.listen(args.seconds)

    print("\n" + "=" * 74)
    print("  结果")
    print("=" * 74)
    if dropped:
        print("  ⚠ 连接在监听期间被服务器断开 —— 极可能是板子重连后把本脚本踢掉了。")
        print("    这本身就说明【板子是活的、在抢只用一条的连接】。")
    print("  共收到 %d 条报文" % len(seen))

    # 兼容两种报文: 紧凑键名(hr/ts/gas, 固件默认) 与旧完整键名(heart_rate/timestamp)
    board = [(ts, p) for ts, tp, p in seen
             if '"seq"' in p and ('"hr"' in p or '"heart_rate"' in p)]
    seqs = [s for s in (seq_of(p) for _, _, p in seen) if s is not None]
    lens = [len(p.encode("utf-8")) for _, _, p in seen]

    if not seen:
        print("\n  ✗ 一条都没收到。")
        print("    → 板子当前没有往这个主题发布(或没连上)。")
        print("      先确认板子通电、OLED 第 8 行是 'NET: OK p=x e=y' 且在涨。")
        return 0

    print("  其中板子格式(含 seq 与 hr/heart_rate)的报文: %d 条" % len(board))
    if seqs:
        print("  收到的 seq: %s" % ", ".join(str(s) for s in seqs))
    if lens:
        over = [l for l in lens if l > 191]
        print("  报文长度: 最大 %d 字节%s"
              % (max(lens),
                 "  ⚠ 有 %d 条超过 191 字节 —— 这些条云端【只转发不记录】" % len(over)
                 if over else "  (全部在 191 字节以内, 云端可记录)"))

    if len(board) >= 2 and len(set(seqs)) >= 2:
        print("\n  ★ 板子在【实时上报】: 收到了多条 seq 递增的报文。")
        print("    → 链路与账号都是通的。")
        if lens and max(lens) > 191:
            print("    !! 但报文超过 191 字节, 云端记录写不进去 ⇒ 控制台看不到数据。")
            print("       这是最典型的\"收得到但看不到\", 请用紧凑报文(见 docs/18)。")
        else:
            print("    报文长度也在上限内 —— 此时控制台的 msg/time 应当能看到设备数据;")
            print("    若仍看不到, 先确认你刷新的是同一个私钥下的同一个主题。")
    elif seen:
        print("\n  ✗ 只听到 1 条(或 seq 没有递增)。")
        print("    → 这更像巴法云为该主题保留的历史报文, 板子当前没有在实时上报。")
        print("      先确认板子通电并连上(OLED 第 8 行), 再重听一次。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(130)
