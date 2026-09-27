#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
esp8266_sim.py —— 用电脑当一个"假的 ESP01S 模块"

思路
----
ESP01S 对 STM32 来说只是一根串口: STM32 发 AT 命令, 模块回应答。
所以你完全可以让**电脑**来扮演这个角色 —— 电脑收到 AT 命令, 按 ESP-AT
的格式回话, 并且在 AT+CIPSTART 时**真的**去连 bemfa.com:9501, 把 Broker
回来的字节用 +IPD 格式转发给 STM32。

于是可以一刀切开问题的归属:

    电脑模拟 ESP  ->  STM32 能连上巴法云(控制台能看到数据)
                    => 固件没错, 问题在 ESP01S 模块 / 供电 / 接线

    电脑模拟 ESP  ->  STM32 仍然连不上
                    => 固件逻辑或 AT 时序有问题, 与模块无关

接线(关键, 务必拔掉 ESP01S)
---------------------------
    STM32 PA9  (USART1_TX)  ---->  USB-TTL 的 RXD
    STM32 PA10 (USART1_RX)  <----  USB-TTL 的 TXD
    STM32 GND               <----> USB-TTL 的 GND

    * ESP01S 模块必须拔下来/断开供电, 否则两个设备会同时驱动同一根信号线
    * USB-TTL 的电平跳线选 3.3V
    * 波特率 115200

跑之前先断电(重要)
------------------
PC 会代替 ESP 去连巴法云。同一个私钥只允许一个连接, 所以:
    - 拔掉 ESP01S(反正要拔)
    - 给别的用同一私钥的设备断电(含旧工程 stm32-dht11-cloud 的板子)
否则双方互相踢下线, 现象就是"断了又连"。

用法
----
    pip install pyserial

    # 列出电脑上的串口
    python esp8266_sim.py --list

    # bridge 模式(默认): 真的把数据转发到巴法云, STM32 会完整跑通
    python esp8266_sim.py --port COM5

    # fake 模式: 不联网, 本地合成 CONNACK/PINGRESP, 用来离线测固件逻辑
    python esp8266_sim.py --port COM5 --mode fake

    # 模拟"热点连不上", 用来验证固件在 CWJAP 失败时的行为
    python esp8266_sim.py --port COM5 --fail-cwjap

    # 模拟"临时断网", 用来验证固件重连退避与自愈
    python esp8266_sim.py --port COM5 --drop-after 20

可用的演示开关
--------------
    --fail-cwjap         AT+CWJAP 一律回 FAIL
    --fail-cipstart      AT+CIPSTART 一律回 CONNECT FAIL / ERROR
    --fake-dns-fail      CIPSTART 按域名失败, 逼固件走 IP 直连
    --drop-after N       每发完 N 条 MQTT 报文就掐断一次 TCP(测试自愈)
    --ssid / --pwd       要"接受"的热点名和密码, 不匹配就回 FAIL
"""

import argparse
import re
import socket
import sys
import threading
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("缺少 pyserial。请先安装:\n    pip install pyserial")
    sys.exit(1)


class EspSim:
    def __init__(self, a):
        self.a = a
        self.lock = threading.Lock()
        self.ser = None
        self.sock = None
        self.sock_th = None
        self.joined = None          # 当前"已连"的热点名
        self.tcp_open = False
        self.buf = bytearray()
        self.state = "cmd"
        self.need = 0
        self.running = True
        self.n_rx_pkts = 0
        self.n_tx_pkts = 0
        self.rx_bytes = 0

    # ------------------------------------------------------------------
    def log(self, tag, text):
        print("[%s] %s" % (tag, text), flush=True)

    def ser_write(self, data: bytes):
        with self.lock:
            try:
                self.ser.write(data)
                self.ser.flush()
            except Exception as e:
                print("!! 串口写入失败: %s" % e, flush=True)

    def resp(self, text: str):
        """回一条 AT 应答(自动补 CRLF)"""
        self.log("ESP->MCU", repr(text))
        self.ser_write(text.encode("utf-8") + b"\r\n")

    def resp_bytes(self, data: bytes, desc: str):
        """回一段原始字节(如 +IPD 数据)"""
        self.log("ESP->MCU", "%s  <%d bytes>" % (desc, len(data)))
        self.ser_write(data)

    # ------------------------------------------------------------------
    def run(self):
        self.ser = serial.Serial(self.a.port, self.a.baud, timeout=0.05)
        print("=" * 68)
        print("  假 ESP01S 已启动")
        print("  串口     : %s @ %d" % (self.a.port, self.a.baud))
        print("  模式     : %s" % self.a.mode)
        if self.a.mode == "bridge":
            print("  转发目标 : %s:%d  (真的连过去)" % (self.a.host, self.a.port))
        else:
            print("  转发目标 : 无, 本地合成 MQTT 应答")
        print("  接受热点 : %s" % self.a.ssid)
        print("=" * 68)
        print("等 STM32 发 AT 命令… (Ctrl+C 退出)\n")

        # 上电横幅: 让固件的"模块重启检测"认为是首次启动
        time.sleep(0.2)
        self.ser_write(b"\r\nready\r\n")

        try:
            while self.running:
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if chunk:
                    self.feed(chunk)
        except KeyboardInterrupt:
            print("\n退出中…")
        finally:
            self.running = False
            self.close_sock()
            if self.ser and self.ser.is_open:
                self.ser.close()

    # ------------------------------------------------------------------
    def feed(self, data: bytes):
        self.buf += data
        while self.running:
            if self.state == "cmd":
                i = self.buf.find(b"\n")
                if i < 0:
                    return
                line = bytes(self.buf[:i])
                del self.buf[:i + 1]
                text = line.rstrip(b"\r").decode("utf-8", "replace").strip()
                if text:
                    self.log("MCU->ESP", text)
                    self.handle_line(text)
                if self.state != "data":
                    continue
            # state == 'data': 收 CIPSEND 的正文
            if len(self.buf) < self.need:
                return
            payload = bytes(self.buf[:self.need])
            del self.buf[:self.need]
            self.state = "cmd"
            self.handle_payload(payload)

    # ------------------------------------------------------------------
    def handle_line(self, u: str):
        U = u.upper()

        # ---- 基础 ----
        if U == "AT":
            return self.resp("OK")
        if U in ("ATE0", "ATE1"):
            return self.resp("OK")
        if U == "AT+RST":
            self.resp("OK")
            self.close_sock()
            self.joined = None
            time.sleep(0.3)
            return self.resp("ready")
        if U == "AT+GMR":
            return self.resp("AT version:1.7.4.0\r\nSDK version:3.0.4\r\nOK")

        # ---- WiFi ----
        if U == "AT+CWMODE=1":
            return self.resp("OK")
        if U.startswith("AT+CWMODE"):
            return self.resp("OK")
        if U == "AT+CWAUTOCONN=0":
            return self.resp("OK")
        if U == "AT+SLEEP=0":
            return self.resp("OK")
        if U == "AT+CWJAP?":
            if self.joined:
                return self.resp('+CWJAP:"%s","aa:bb:cc:11:22:33",6,-52\r\n\r\nOK'
                                 % self.joined)
            return self.resp("No AP\r\n\r\nOK")
        if U.startswith("AT+CWJAP="):
            return self.cmd_join(u)
        if U == "AT+CWQAP":
            self.joined = None
            return self.resp("OK")

        # ---- TCP ----
        if U == "AT+CIPMUX=0":
            return self.resp("OK")
        if U == "AT+CIPCLOSE":
            return self.cmd_close()
        if U.startswith("AT+CIPSTART="):
            return self.cmd_cipstart(u)
        if U.startswith("AT+CIPSEND="):
            return self.cmd_cipsend(u)

        # ---- 诊断类 ----
        if U == "AT+CIFSR":
            return self.resp('+CIFSR:STAIP,"192.168.43.150"\r\n'
                             '+CIFSR:STAMAC,"5c:cf:7f:ab:cd:ef"\r\n\r\nOK')
        if U == "AT+CIPSTATUS":
            if self.tcp_open:
                return self.resp('STATUS:3\r\n'
                                 '+CIPSTATUS:0,"TCP","119.91.109.180",9501,0\r\n\r\nOK')
            return self.resp("STATUS:5\r\n\r\nOK")
        if U.startswith("AT+CIPDOMAIN="):
            return self.cmd_cipdomain(u)

        # ---- 未知命令 ----
        self.log("!!", "未实现的命令, 回 ERROR(提醒: 驱动新增了 AT 指令?)")
        return self.resp("ERROR")

    # ------------------------------------------------------------------
    def cmd_join(self, u: str):
        m = re.search(r'AT\+CWJAP\s*=\s*"([^"]*)"\s*,\s*"([^"]*)"', u, re.I)
        if not m:
            return self.resp("ERROR")
        ssid, pwd = m.group(1), m.group(2)

        if self.a.fail_cwjap:
            self.log("!!", "--fail-cwjap 已开启, 模拟连不上热点")
            time.sleep(3.0)
            return self.resp("+CWJAP:3\r\n\r\nFAIL")

        if ssid != self.a.ssid or pwd != self.a.pwd:
            self.log("!!", "热点名或密码与配置不符: 收到 [%s]/[%s]" % (ssid, pwd))
            time.sleep(3.0)
            return self.resp("+CWJAP:2\r\n\r\nFAIL")

        self.log("  ", "模拟连接热点 %s …" % ssid)
        time.sleep(1.5)                       # 模拟真实的连接耗时
        self.ser_write(b"WIFI CONNECTED\r\n")
        time.sleep(0.8)
        self.ser_write(b"WIFI GOT IP\r\n")
        self.joined = ssid
        return self.resp("OK")

    # ------------------------------------------------------------------
    def cmd_cipstart(self, u: str):
        m = re.match(r'AT\+CIPSTART\s*=\s*"[^"]*"\s*,\s*"([^"]*)"\s*,\s*(\d+)',
                     u, re.I)
        if not m:
            return self.resp("ERROR")
        host, port = m.group(1), int(m.group(2))

        if self.a.fail_cipstart:
            self.log("!!", "--fail-cipstart 已开启")
            return self.resp("CONNECT FAIL\r\nERROR")

        is_ip = bool(re.match(r'^\d+\.\d+\.\d+\.\d+$', host))
        if self.a.fake_dns_fail and not is_ip:
            self.log("!!", "--fake-dns-fail: 模拟域名解析失败")
            return self.resp("DNS Fail\r\nERROR")

        if self.a.mode == "fake":
            self.tcp_open = True
            return self.resp("CONNECT\r\n\r\nOK")

        # bridge: 真的连过去
        self.log("  ", "正在连接 %s:%d …" % (host, port))
        try:
            s = socket.create_connection((host, port), timeout=10)
            s.settimeout(0.5)
        except Exception as e:
            self.log("!!", "TCP 连接失败: %s" % e)
            return self.resp("CONNECT FAIL\r\nERROR")

        self.close_sock()
        self.sock = s
        self.tcp_open = True
        self.sock_th = threading.Thread(target=self.sock_loop, daemon=True)
        self.sock_th.start()
        self.log("  ", "已连上 %s:%d" % (host, port))
        return self.resp("CONNECT\r\n\r\nOK")

    def cmd_close(self):
        if not self.tcp_open:
            return self.resp("OK")
        self.close_sock()
        return self.resp("CLOSED\r\n\r\nOK")

    def cmd_cipdomain(self, u: str):
        m = re.search(r'AT\+CIPDOMAIN\s*=\s*"([^"]*)"', u, re.I)
        if not m:
            return self.resp("ERROR")
        host = m.group(1)
        try:
            ip = socket.gethostbyname(host)
        except Exception:
            return self.resp("DNS Fail")
        return self.resp("+CIPDOMAIN:%s\r\n\r\nOK" % ip)

    # ------------------------------------------------------------------
    def cmd_cipsend(self, u: str):
        m = re.search(r'AT\+CIPSEND\s*=\s*(\d+)', u, re.I)
        if not m:
            return self.resp("ERROR")
        n = int(m.group(1))
        if not self.tcp_open:
            return self.resp("ERROR")
        self.need = n
        self.state = "data"
        # ESP-AT 的标准应答: 先 OK, 再给数据提示符 '>'
        self.log("ESP->MCU", "OK / '>' (等 %d 字节正文)" % n)
        self.ser_write(b"\r\nOK\r\n> ")

    # ------------------------------------------------------------------
    def handle_payload(self, payload: bytes):
        self.n_tx_pkts += 1
        kind = payload[0] >> 4 if payload else -1
        names = {1: "CONNECT", 3: "PUBLISH", 8: "SUBSCRIBE", 12: "PINGREQ",
                 14: "DISCONNECT"}
        self.log("MCU->ESP", "%s 报文 %d 字节 [%s]"
                 % (names.get(kind, "type%d" % kind), len(payload),
                    payload[:min(len(payload), 48)].hex(" ")))

        if self.a.mode == "bridge":
            if not self.sock:
                return self.resp("SEND FAIL")
            try:
                self.sock.sendall(payload)
            except Exception as e:
                self.log("!!", "转发到云端失败: %s" % e)
                return self.resp("SEND FAIL")
            self.log("ESP->云", "已转发 %d 字节" % len(payload))
        else:
            self.fake_respond(payload)

        self.resp("Recv %d bytes\r\n\r\nSEND OK" % len(payload))

        if self.a.drop_after and self.n_tx_pkts % self.a.drop_after == 0:
            self.log("!!", "--drop-after: 故意掐断 TCP, 测试固件自愈")
            self.close_sock()
            self.ser_write(b"\r\nCLOSED\r\n")

    # ------------------------------------------------------------------
    def fake_respond(self, payload: bytes):
        """离线合成 Broker 的应答, 让固件在完全没网时也能跑完全流程"""
        t = payload[0] >> 4
        if t == 1:                                    # CONNECT -> CONNACK
            self.ser_write(b"\r\n+IPD,4:" + bytes([0x20, 0x02, 0x00, 0x00]))
            self.log("ESP->MCU", "+IPD CONNACK rc=0x00 (离线合成)")
        elif t == 3:                                  # PUBLISH -> PUBACK(QoS1)
            qos = (payload[0] >> 1) & 0x03
            if qos == 1:
                self.ser_write(b"\r\n+IPD,4:" + bytes([0x40, 0x02, 0x00, 0x01]))
        elif t == 12:                                 # PINGREQ -> PINGRESP
            self.ser_write(b"\r\n+IPD,2:" + bytes([0xD0, 0x00]))
            self.log("ESP->MCU", "+IPD PINGRESP (离线合成)")
        elif t == 8:                                  # SUBSCRIBE -> SUBACK
            self.ser_write(b"\r\n+IPD,5:" + bytes([0x90, 0x03, 0x00, 0x01, 0x00]))
        # DISCONNECT(14) / 其它: 无需应答

    # ------------------------------------------------------------------
    def sock_loop(self):
        s = self.sock
        while self.running and self.sock is s:
            try:
                data = s.recv(1024)
            except socket.timeout:
                continue
            except Exception:
                break
            if not data:
                break
            self.n_rx_pkts += 1
            self.rx_bytes += len(data)
            self.log("云->ESP", "收到 %d 字节 [%s]"
                     % (len(data), data[:min(len(data), 48)].hex(" ")))
            # ESP-AT 用 +IPD 把下行数据交给 MCU
            self.ser_write(b"\r\n+IPD,%d:" % len(data) + data)
        if self.running:
            self.log("!!", "云端连接已断开")
            self.ser_write(b"\r\nCLOSED\r\n")
        self.tcp_open = False

    def close_sock(self):
        s, self.sock = self.sock, None
        self.tcp_open = False
        if s:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            try:
                s.close()
            except Exception:
                pass


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="用电脑模拟 ESP01S 模块")
    ap.add_argument("--list", action="store_true", help="列出可用串口后退出")
    ap.add_argument("--port", help="串口号, 如 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", choices=["bridge", "fake"], default="bridge",
                    help="bridge=真转发到巴法云(默认); fake=离线合成应答")
    ap.add_argument("--host", default="bemfa.com", help="bridge 模式的转发目标")
    ap.add_argument("--tcp-port", type=int, default=9501)
    ap.add_argument("--ssid", default="0001", help="要'接受'的热点名")
    ap.add_argument("--pwd", default="zxc145236", help="要'接受'的热点密码")
    ap.add_argument("--fail-cwjap", action="store_true")
    ap.add_argument("--fail-cipstart", action="store_true")
    ap.add_argument("--fake-dns-fail", action="store_true")
    ap.add_argument("--drop-after", type=int, default=0)
    a = ap.parse_args()

    if a.list:
        ports = list(list_ports.comports())
        if not ports:
            print("没有找到任何串口。USB-TTL 插好了吗? 驱动装了吗?")
        for p in ports:
            print("%-8s %s" % (p.device, p.description))
        return 0

    if not a.port:
        print("请用 --port 指定串口, 如 --port COM5")
        print("不知道是哪个的话, 先跑:  python esp8266_sim.py --list")
        return 2

    EspSim(a).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
