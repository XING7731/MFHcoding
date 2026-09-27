# -*- coding: utf-8 -*-
"""把 dist/dist/* 扁平化到 dist/*，把 img/img/*.png 归位到 utils/img/*.png。"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(ROOT)


def count_files(path):
    n = 0
    for _, _, files in os.walk(path):
        n += len(files)
    return n


def flatten(outer, inner, report):
    """把 outer/inner 下的所有内容移动到 outer/ 下。"""
    src = os.path.join(ROOT, outer, inner)
    dst = os.path.join(ROOT, outer)
    if not os.path.isdir(src):
        report.append("[skip] %s/%s 不存在" % (outer, inner))
        return
    before = count_files(dst)
    moved = 0
    for name in os.listdir(src):
        s = os.path.join(src, name)
        t = os.path.join(dst, name)
        if os.path.exists(t):
            report.append("[warn] 目标已存在，跳过: %s" % t)
            continue
        shutil.move(s, t)
        moved += 1
    after = count_files(dst)
    if os.path.isdir(src) and not os.listdir(src):
        os.rmdir(src)
        report.append("[ok] 已删除空目录 %s/%s" % (outer, inner))
    else:
        report.append("[warn] %s/%s 非空，未删除: %s" %
                      (outer, inner, os.listdir(src)))
    report.append("[ok] %s: 移动 %d 项, 文件数 %d -> %d" %
                  (outer, moved, before, after))


def move_tree(src_rel, dst_rel, report):
    """把 src_rel 下的内容整体移动到 dst_rel。"""
    src = os.path.join(ROOT, src_rel)
    dst = os.path.join(ROOT, dst_rel)
    if not os.path.isdir(src):
        report.append("[skip] %s 不存在" % src_rel)
        return
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in os.listdir(src):
        s = os.path.join(src, name)
        t = os.path.join(dst, name)
        if os.path.exists(t):
            report.append("[warn] 目标已存在，跳过: %s" % t)
            continue
        shutil.move(s, t)
        n += 1
    if not os.listdir(src):
        os.rmdir(src)
    report.append("[ok] %s -> %s : 移动 %d 项" % (src_rel, dst_rel, n))


def main():
    report = []
    flatten("dist", "dist", report)
    move_tree("img/img", "utils/img", report)
    if os.path.isdir(os.path.join(ROOT, "img")) and not os.listdir(os.path.join(ROOT, "img")):
        os.rmdir(os.path.join(ROOT, "img"))
        report.append("[ok] 已删除空目录 img/")

    # 清理微信小程序默认模板的 logs 示例页
    logs = os.path.join(ROOT, "pages", "logs")
    if os.path.isdir(logs):
        shutil.rmtree(logs)
        report.append("[ok] 已移除模板示例页 pages/logs")

    report.append("=== 最终结构 ===")
    for name in sorted(os.listdir(ROOT)):
        p = os.path.join(ROOT, name)
        if os.path.isdir(p):
            report.append("  %s/  (%d files)" % (name, count_files(p)))
        else:
            report.append("  %s" % name)

    report.append("utils/img: " + ", ".join(
        sorted(os.listdir(os.path.join(ROOT, "utils", "img")))))

    sys.stdout.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
