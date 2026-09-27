#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""命令行无头编译 Keil MDK 工程, 并把结果摘要写成一份 UTF-8 文本。

【为什么要单独写这个脚本】
  这个环境里 PowerShell 工具不回显 stdout, Bash 的 PATH 又是坏的(ls/head 都
  找不到)。而 Keil 的编译结果只能从它自己写的 log 里拿。所以"跑一次编译"这
  件事必须由一个脚本自己把摘要落盘, 再用读文件的方式取回来 —— 否则只能看到
  一个没有内容的 "exit code 0", 无法确认到底有没有 Error/Warning。

用法:
    python tools/build_keil.py                 # 增量编译, 摘要写到 MDK-ARM/build_report.txt
    python tools/build_keil.py out.txt         # 指定摘要文件
    python tools/build_keil.py out.txt -b      # -b 全量重建(等价 Keil 的 Rebuild)

退出码: 直接沿用 UV4 的退出码(0 无错无警 / 1 有警告 / 2 有错误 / 其它=启动失败),
        方便在脚本里判断。
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Keil 可能装在这些地方, 按顺序找
UV4_CANDIDATES = [
    r"C:\apps\keil5\UV4\UV4.exe",
    r"C:\Keil_v5\UV4\UV4.exe",
    r"C:\Program Files\Keil_v5\UV4\UV4.exe",
    r"C:\Program Files (x86)\Keil_v5\UV4\UV4.exe",
    r"D:\Keil_v5\UV4\UV4.exe",
]


def find_uv4():
    for p in UV4_CANDIDATES:
        if os.path.isfile(p):
            return p
    return None


def find_project():
    for d in ("MDK-ARM", ""):
        base = os.path.join(ROOT, d) if d else ROOT
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if name.endswith(".uvprojx"):
                return os.path.join(base, name)
    return None


def main():
    args = [a for a in sys.argv[1:]]
    build_flag = "-r"                   # -r = Rebuild(全量), -b = Build(增量)
    if "-b" in args:
        args.remove("-b")
        build_flag = "-b"

    report = args[0] if args else os.path.join(ROOT, "MDK-ARM", "build_report.txt")
    log = os.path.splitext(report)[0] + ".uv4.log"

    lines = []
    def say(s):
        lines.append(s)

    uv4 = find_uv4()
    proj = find_project()
    say("UV4   : %s" % (uv4 or "!! 没找到 UV4.exe, 请改 UV4_CANDIDATES"))
    say("工程  : %s" % (proj or "!! 没找到 .uvprojx"))
    if not uv4 or not proj:
        _write(report, lines)
        return 10

    if os.path.exists(log):
        os.remove(log)

    say("模式  : %s" % ("Rebuild(全量)" if build_flag == "-r" else "Build(增量)"))
    # -j0: 不弹窗; -o: 把编译输出写到 log
    cmd = [uv4, build_flag, proj, "-j0", "-o", log]
    say("命令  : " + " ".join('"%s"' % c if " " in c else c for c in cmd))
    say("")

    try:
        proc = subprocess.run(cmd, cwd=os.path.dirname(proj), timeout=900,
                              capture_output=True)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        say("!! 编译超时(900s)")
        _write(report, lines)
        return 11
    except OSError as e:
        say("!! 无法启动 UV4: %s" % e)
        _write(report, lines)
        return 12

    say("UV4 退出码 = %d  (0=无错无警 / 1=有警告 / 2=有错误 / 3=致命)" % rc)
    say("")

    text = ""
    if os.path.exists(log):
        with open(log, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()

    if not text.strip():
        say("!! UV4 没有写出 log —— 通常是工程打不开或许可证问题;")
        say("   先手动用 Keil 打开一次工程确认能编译, 再回来跑本脚本。")
        _write(report, lines)
        return rc if rc else 13

    # ---- 抽取结论行 ----
    err = re.search(r"(\d+)\s+Error\(s\)", text)
    warn = re.search(r"(\d+)\s+Warning\(s\)", text)
    size = re.search(r"Program Size:\s*(.*)", text)
    hexline = re.search(r'creating hex file from "([^"]+)"', text) or \
              re.search(r"FromELF: creating hex file\.\.\.", text)

    say("Error   = %s" % (err.group(1) if err else "?"))
    say("Warning = %s" % (warn.group(1) if warn else "?"))
    if size:
        say("尺寸    : %s" % size.group(1).strip())

    if err and err.group(1) != "0":
        say("")
        say("---- 报错行(含上下文) ----")
        for i, ln in enumerate(text.splitlines()):
            if re.search(r"\berror\b|\bError\b:", ln):
                say("  " + ln.strip())
    if warn and warn.group(1) != "0":
        say("")
        say("---- 警告行 ----")
        for ln in text.splitlines():
            if "warning" in ln.lower():
                say("  " + ln.strip())

    ok = bool(err and err.group(1) == "0" and warn and warn.group(1) == "0")
    say("")
    say("结论    : %s" % ("0 Error / 0 Warning  ✔" if ok else "见上 ↑"))

    # hex 时间戳(确认产物是本次的)
    # 注意: Keil 会把 hex 放在 MDK-ARM/<工程名>/ 子目录下, 不是和 uvprojx 同级,
    #       所以要递归找, 不能只看 os.path.dirname(proj)。
    hexp = None
    newest = -1.0
    for dirpath, _dirnames, filenames in os.walk(os.path.dirname(proj)):
        for name in filenames:
            if name.lower().endswith(".hex"):
                p = os.path.join(dirpath, name)
                m = os.path.getmtime(p)
                if m > newest:
                    newest, hexp = m, p
    if hexp:
        import time
        say("HEX     : %s  %d 字节  %s"
            % (os.path.relpath(hexp, ROOT), os.path.getsize(hexp),
               time.strftime("%Y-%m-%d %H:%M:%S",
                             time.localtime(newest))))

    _write(report, lines)
    return rc


def _write(path, lines):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
