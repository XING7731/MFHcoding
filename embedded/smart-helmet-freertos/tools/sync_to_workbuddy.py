#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把工程源码镜像到 WorkBuddy 工作区副本, 并用 MD5 校验一致性。

【为什么需要它】
  这套工程实际有两份: 真正用来编译烧录的是桌面上的那份
  (C:\\Users\\27362\\Desktop\\New Folder\\smart-helmet-freertos),
  而 WorkBuddy 工作区里保存的是一份便于查阅/留档的副本。两份靠手工复制
  很容易漏文件 —— 上一版就出现过"本地改了、副本还是旧的", 事后对不上账。
  所以同步这件事必须可校验: 逐文件比 MD5, 只复制真正不同的, 最后再回读
  校验一遍。

只同步源码与文档(user/ tools/ docs/ 与根目录的 *.md), 不同步 Core/、
MDK-ARM/、Drives/ —— 那些是 CubeMX/Keil 生成物与编译中间件, 体积大且
可随时由源码重建。

用法:
    python tools/sync_to_workbuddy.py                    # 同步 + 校验
    python tools/sync_to_workbuddy.py --dry-run          # 只看差异, 不写
    python tools/sync_to_workbuddy.py --dst <目标目录>    # 指定副本位置
退出码: 0 全部一致 / 1 有文件无法同步 / 2 路径不存在
"""

import argparse
import hashlib
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)          # 工程根 = tools/ 的上一级

DEFAULT_DST = (r"C:\Users\27362\WorkBuddy\2026-09-11-08-34-18"
               r"\smart-helmet-freertos")

SYNC_DIRS = ["user", "tools", "docs"]
SYNC_ROOT_FILES = ["README.md"]


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def _skip(name):
    """跳过临时/生成物。

    `_` 开头的一律跳过 —— 这是本项目的临时约定(_probe.txt 等探针输出),
    其中就包括本脚本自己写的 _sync_report.txt。不跳过的话, 下一次同步会把
    上一轮的报告当成源码复制过去, 副本里就会出现一堆过期报告。
    """
    if name.startswith("_"):
        return True
    if name.endswith((".pyc", ".pyo")):
        return True
    return False


def collect():
    """返回 {相对路径: 绝对路径}。"""
    out = {}
    for d in SYNC_DIRS:
        base = os.path.join(SRC, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames if x != "__pycache__"]
            for name in filenames:
                if _skip(name):
                    continue
                p = os.path.join(dirpath, name)
                out[os.path.relpath(p, SRC)] = p
    for name in SYNC_ROOT_FILES:
        p = os.path.join(SRC, name)
        if os.path.isfile(p):
            out[name] = p
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dst", default=DEFAULT_DST)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    dst = args.dst
    lines = []

    def say(s):
        lines.append(s)
        print(s)

    if not os.path.isdir(dst):
        print("!! 目标目录不存在: %s" % dst)
        print("   用 --dst 指定正确位置, 或先创建它。")
        return 2

    files = collect()
    say("源   : %s" % SRC)
    say("目标 : %s" % dst)
    say("文件数(源) : %d" % len(files))
    if args.dry_run:
        say("模式 : dry-run(只看差异, 不写盘)")
    say("")

    copied, same, failed = [], [], []

    for rel in sorted(files):
        sp = files[rel]
        dp = os.path.join(dst, rel)
        try:
            smd5 = md5(sp)
        except OSError as e:
            failed.append("%s  (源读取失败: %s)" % (rel, e))
            continue

        if os.path.isfile(dp):
            try:
                dmd5 = md5(dp)
            except OSError:
                dmd5 = None
            if dmd5 == smd5:
                same.append(rel)
                continue

        if args.dry_run:
            copied.append(rel + "   [需要更新]")
            continue

        try:
            os.makedirs(os.path.dirname(dp), exist_ok=True)
            shutil.copy2(sp, dp)
            # 回读校验: 复制完必须再算一次, 只信结果不信动作
            if md5(dp) != smd5:
                failed.append("%s  (复制后 MD5 不一致)" % rel)
            else:
                copied.append(rel)
        except OSError as e:
            failed.append("%s  (%s)" % (rel, e))

    # 副本里多出来的源文件(只报告, 不删 —— 删除必须由人来决定)
    extra = []
    for d in SYNC_DIRS:
        base = os.path.join(dst, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames if x != "__pycache__"]
            for name in filenames:
                p = os.path.join(dirpath, name)
                rel = os.path.relpath(p, dst)
                if rel not in files:
                    extra.append(rel)

    say("---- 结果 ----")
    say("已同步(内容有变) : %d" % len(copied))
    for r in copied:
        say("    + %s" % r)
    say("本来就一致       : %d" % len(same))
    if failed:
        say("!! 失败          : %d" % len(failed))
        for r in failed:
            say("    ! %s" % r)
    if extra:
        say("副本多出(未删除, 请人工确认) : %d" % len(extra))
        for r in extra:
            say("    ? %s" % r)

    say("")
    if failed:
        say("结论 : 有文件未能同步, 见上")
    elif args.dry_run:
        say("结论 : dry-run 结束, 实际同步时会有 %d 个文件被更新" % len(copied))
    else:
        say("结论 : 两份已完全一致(共 %d 个文件逐字节校验通过)"
            % (len(same) + len(copied)))

    out = os.path.join(SRC, "tools", "_sync_report.txt")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("\n(报告已写入 tools/_sync_report.txt)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
