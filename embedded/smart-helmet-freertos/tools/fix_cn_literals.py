#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fix_cn_literals.py —— 把"代码中的字符串字面量"里的非 ASCII 字符转成 \\xNN 转义

【为什么需要这个脚本】
    Keil MDK 的 ARMCC (AC5) 按"系统 ANSI 代码页"解析源文件。在中文 Windows 上
    就是 GBK。而本项目源码是 UTF-8, 于是一旦某个字符串字面量里含中文:

        return "摔倒报警";

    armcc 会把 E6 91 94 E5 80 92 E6 8A A5 E8 AD A6 这些字节按 GBK 两两配对,
    配对错位时恰好把结尾的双引号 0x22 当作某个双字节字符的第二字节"吃掉", 于是报:

        error:  #8: missing closing quote

    报错信息里的中文还会显示成 "鎽斿€掓姤璀?" —— 这是 UTF-8 字节被当 GBK 解读的
    典型乱码, 可作为该问题的识别特征。

    实测结论: 加 --multibyte_chars 无效; 换 AC6(armclang) 可以, 但本项目用 AC5。

【本脚本的做法】
    只改"代码中的字符串字面量", 把它里面的非 ASCII 字节逐字节写成 \\xNN:

        return "摔倒报警";
            ↓
        return "\xe6\x91\x94\xe5\x80\x92\xe6\x8a\xa5\xe8\xad\xa6";

    这样字面量在源码层面变成纯 ASCII, 任何编译器 / 任何代码页都不会再误判,
    而运行期产生的字节流仍是原来的 UTF-8 序列, 语义完全不变。

    脚本采用状态机逐字节扫描, 明确区分:
        代码 / 行注释 / 块注释 / 字符串字面量 / 字符常量
    注释里的中文【不会】被改动 —— 注释中的中文不影响 armcc 编译,
    且保留原文更利于阅读。

    C 语言细节: \\xNN 会贪婪地吞掉后续十六进制数字。例如 "\\x94" 后面紧跟字符 'E'
    会被解析成 "\\x94E"。脚本在这种情况下自动插入 "" 断开, 写成 "\\x94" "E"
    (C 会拼接相邻的字面量, 结果完全不变)。

【用法】
    python fix_cn_literals.py            # 只扫描并报告, 不修改
    python fix_cn_literals.py --apply    # 实际写入修改
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC_DIRS = [
    os.path.join(ROOT, '..', 'user', 'Src'),
    os.path.join(ROOT, '..', 'user', 'Inc'),
]
HEXDIGITS = b'0123456789abcdefABCDEF'

CODE, LINE_CMT, BLOCK_CMT, STRING, CHAR = range(5)


def escape_bytes(data: bytes) -> bytes:
    """把非 ASCII 字节写成 \\xNN, 并处理 \\xNN 贪婪吃位问题。"""
    out = bytearray()
    for i, b in enumerate(data):
        if b < 0x80:
            out.append(b)
            continue
        out += b'\\x%02x' % b
        if i + 1 < len(data) and data[i + 1] in HEXDIGITS:
            out += b'""'          # 断开, 防止 \xNN 继续吃位
    return bytes(out)


def process(path: str, apply: bool) -> int:
    with open(path, 'rb') as f:
        src = f.read()

    out = bytearray()
    hits = []
    i, n = 0, len(src)
    state = CODE
    lit_start = 0          # 当前字面量"内容"的起始下标

    while i < n:
        c = src[i]
        nxt = src[i + 1:i + 2]

        if state == CODE:
            if c == 0x2F and nxt == b'/':
                out.append(c); i += 1; state = LINE_CMT; continue
            if c == 0x2F and nxt == b'*':
                out += b'/*'; i += 2; state = BLOCK_CMT; continue
            if c == 0x22:
                i += 1; lit_start = i; state = STRING; continue
            if c == 0x27:
                i += 1; lit_start = i; state = CHAR; continue
            out.append(c); i += 1; continue

        if state == LINE_CMT:
            out.append(c)
            if c == 0x0A:
                state = CODE
            i += 1; continue

        if state == BLOCK_CMT:
            if c == 0x2A and nxt == b'/':
                out += b'*/'; i += 2; state = CODE; continue
            out.append(c); i += 1; continue

        if state in (STRING, CHAR):
            q = 0x22 if state == STRING else 0x27
            if c == 0x5C and i + 1 < n:        # 转义序列: 属于字面量内容, 只推进下标
                i += 2; continue
            if c == q:
                body = src[lit_start:i]
                if state == STRING and any(b >= 0x80 for b in body):
                    ln = src.count(b'\n', 0, lit_start) + 1
                    hits.append((ln, body))
                    out += b'"' + escape_bytes(body) + b'"'
                else:
                    out += src[lit_start - 1:i + 1]     # 原样保留(含两侧引号)
                i += 1; state = CODE; continue
            i += 1; continue

    # 理论上不会走到这里(源文件不会以未闭合字面量结束)
    if state != CODE:
        raise SystemExit(f'{path}: 状态机异常结束(state={state}), 源文件可能不完整')

    if not hits:
        return 0

    rel = os.path.relpath(path, os.path.join(ROOT, '..'))
    print(f'\n{rel}')
    for ln, body in hits:
        print(f'  第 {ln} 行: {body.decode("utf-8", "replace")}')

    if apply:
        with open(path, 'wb') as f:
            f.write(bytes(out))
    return len(hits)


def main() -> None:
    apply = '--apply' in sys.argv
    total, files = 0, 0

    for d in SRC_DIRS:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(('.c', '.h')):
                k = process(os.path.join(d, name), apply)
                if k:
                    total += k
                    files += 1

    print()
    if total == 0:
        print('未发现"代码中的字符串字面量含非 ASCII 字符", 无需修改。')
    else:
        verb = '已修改' if apply else '待修改(未写入, 加 --apply 生效)'
        print(f'共 {files} 个文件 / {total} 处字面量 {verb}。')


if __name__ == '__main__':
    main()
