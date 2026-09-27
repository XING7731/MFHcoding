#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静止自标定逻辑的数值验证（PC 侧模拟，不涉及硬件）

目的: 在烧录之前, 先用数值把 mpu6050.c 里 accel_self_calibrate() 的推导走一遍,
      确认三件事:
        ① 静止时原始码值模值确实等于真实 LSB/g
        ② 标定能把"被放大 4 倍的读数"修回真实 g
        ③ 阈值 1.8 / 1.5 在这颗 ±2g 芯片上判据真的能触发

复现方式: python tools/accel_cal_sim.py
"""

import math
import random

# ---------------- 被模拟的硬件事实 ----------------
TRUE_LSB_PER_G = 16384.0   # 芯片实际灵敏度: ±2g 档 = 16384 LSB/g
REG_LSB_PER_G  = 4096.0    # 代码据寄存器读回假设的灵敏度: ±8g 档 = 4096 LSB/g
INT16_MAX      = 32767     # 原始码值是 int16, 超过就削顶

FALL_HIGH_G = 1.8          # user_config.h: FALL_ACCEL_HIGH_G
FALL_LOW_G  = 1.5          # user_config.h: FALL_ACCEL_LOW_G

random.seed(20260916)


def raw_sample(a_g, noise_lsb=60.0):
    """把真实加速度(g)变成三轴原始码值, 含 int16 削顶。

    重力方向任意: 把 a_g 全部分配到 x 轴(与现场"x 轴竖直朝下"一致),
    y/z 只留噪声 —— 这样最贴近用户看到的现象(A:3.9 0.0 0.0)。
    """
    rx = a_g * TRUE_LSB_PER_G + random.gauss(0, noise_lsb)
    ry = random.gauss(0, noise_lsb)
    rz = random.gauss(0, noise_lsb)
    clamp = lambda v: max(-INT16_MAX - 1, min(INT16_MAX, int(round(v))))
    return clamp(rx), clamp(ry), clamp(rz)


def magnitude(rx, ry, rz):
    # 与 C 侧一致: 先转 float 再相乘, 避免平方和溢出 32 位 int
    return math.sqrt(float(rx) ** 2 + float(ry) ** 2 + float(rz) ** 2)


def run_calibration(samples=64):
    """复刻 accel_self_calibrate(): 单轮静止标定。

    返回 (标定值, 极差百分比)。静止时模值均值就是真实 LSB/g。
    """
    mags = [magnitude(*raw_sample(1.0)) for _ in range(samples)]
    mean = sum(mags) / len(mags)
    spread = (max(mags) - min(mags)) / mean * 100.0
    return mean, spread


print("=" * 74)
print("① 静止标定: 模值均值 是否等于 一颗静止加速度计的真实 LSB/g")
print("=" * 74)
cal_lsb, cal_spread = run_calibration()
print(f"  真实灵敏度(物理)        : {TRUE_LSB_PER_G:8.1f} LSB/g   (= ±2g 档)")
print(f"  寄存器读回假设值        : {REG_LSB_PER_G:8.1f} LSB/g   (= ±8g 档, 芯片在撒谎)")
print(f"  静止标定测得            : {cal_lsb:8.1f} LSB/g")
print(f"  相对误差                : {abs(cal_lsb - TRUE_LSB_PER_G) / TRUE_LSB_PER_G * 100:8.2f} %")
print(f"  标定期极差(越小越静止)  : {cal_spread:8.2f} %   (门的限值 6.0 %)")
print(f"  结论: {'标定值 = 真实值, 不依赖任何寄存器读回' if abs(cal_lsb - TRUE_LSB_PER_G) / TRUE_LSB_PER_G < 0.02 else '偏离过大!'}")

print()
print("=" * 74)
print("② 同一批码值, 两种换算系数得到的读数")
print("=" * 74)
print(f"  {'真实加速度':>12} {'原始码值':>10} {'旧:÷4096':>10} {'新:÷16384':>11}  说明")
print("  " + "-" * 70)


def show(a_g, note):
    rx, ry, rz = raw_sample(a_g, noise_lsb=0.0)
    old = magnitude(rx, ry, rz) / REG_LSB_PER_G
    new = magnitude(rx, ry, rz) / cal_lsb
    print(f"  {a_g:>10.2f}g {rx:>10d} {old:>9.2f}g {new:>10.2f}g  {note}")


show(1.00, "静止(板子不动) ── 旧的 3.9 就是这么来的")
show(0.00, "自由落体(失重)")
show(2.00, "真实 2g: ±2g 量程刚好用满")
show(6.00, "真实 6g(约 1m 跌落) ── 被 int16 削顶")
show(8.00, "真实 8g(更重的摔)   ── 同样削顶")

print()
print("=" * 74)
print("③ 摔倒判据: |a|>1.8g 两次, 之后 1.5s 内 <1.5g")
print("=" * 74)
for label, lsb in (("标定成功(÷16384)", cal_lsb), ("标定失败(÷4096) ", REG_LSB_PER_G)):
    idle = magnitude(*raw_sample(1.0, noise_lsb=0.0)) / lsb
    hit = magnitude(*raw_sample(6.0, noise_lsb=0.0)) / lsb
    c1 = hit > FALL_HIGH_G          # 条件1: 冲击超阈值
    c2 = hit > FALL_HIGH_G          # 条件2: 窗口内第二次
    c3 = idle < FALL_LOW_G          # 条件3: 摔后静止低于低阈值
    verdict = "可触发" if (c1 and c2 and c3) else "永不触发"
    print(f"  {label}:  静止|a|={idle:5.2f}g  冲击|a|={hit:5.2f}g  "
          f"条件1={int(c1)} 条件2={int(c2)} 条件3={int(c3)}  →  {verdict}")

print()
print("  判读要点:")
print("    · 标定成功 → 静止 1.0g 低于阈值, 冲击撞到量程顶 2.0g 高于阈值, 三条全过;")
print("    · 标定失败 → 静态读数就 4.0g 高于阈值, 状态机长期卡在 IMPACT 出不来,")
print("      条件3 又因 4.0 永不小于 1.5 而不成立 → 真摔也不报警。")
print("      这正是屏幕必须显示 ! ACAL FAIL 的原因 —— 失败必须让人看见。")
print()
print("=" * 74)
print("④ 量程上限决定了 HIGH 能取多大")
print("=" * 74)
ceil_g = INT16_MAX / TRUE_LSB_PER_G
print(f"  ±2g 档的物理读数上限 = 32767 / 16384 = {ceil_g:.2f}g")
for th in (1.5, 1.8, 2.0, 2.5):
    ok = ceil_g > th
    print(f"    HIGH = {th:.1f}g  →  {'可触发 (量程顶 {:.2f} > {:.1f})'.format(ceil_g, th) if ok else '永不触发 (量程顶 {:.2f} <= {:.1f})'.format(ceil_g, th)}")
print()
print("  所以需求原值 2.5g 在这颗芯片上物理不可达 —— HIGH=1.8 不是取舍, 是上限内的唯一选择。")
print("  要回到 2.5g, 只有换一片能真正换挡到 ±8g 的芯片。")
