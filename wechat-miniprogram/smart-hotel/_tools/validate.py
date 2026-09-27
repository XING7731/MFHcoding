# -*- coding: utf-8 -*-
"""小程序工程静态校验：
1) 所有 JSON 可解析
2) app.json 里的页面文件齐全
3) usingComponents 路径可解析到 .js
4) wxml 中引用的本地图片资源存在
5) wxml 中出现的自定义组件标签均已注册
6) wxml 中绑定的事件处理函数在对应 js 中存在
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BUILTIN_TAGS = {
    'view', 'text', 'image', 'button', 'input', 'scroll-view', 'swiper',
    'swiper-item', 'block', 'canvas', 'form', 'label', 'navigator', 'picker',
    'checkbox', 'radio', 'switch', 'slider', 'textarea', 'video', 'audio',
    'map', 'web-view', 'cover-view', 'cover-image', 'movable-area',
    'movable-view', 'open-data', 'ad', 'camera', 'live-player', 'live-pusher',
    'official-account', 'progress', 'rich-text', 'slot', 'wxs', 'template',
    'picker-view', 'picker-view-column', 'functional-page-navigator',
    'page-meta', 'navigation-bar', 'root-portal', 'match-media',
    'keyboard-accessory', 'page-container', 'share-element', 'grid-view',
    'list-view', 'sticky-header', 'sticky-section', 'snapshot', 'voip-room',
    'channel-live', 'channel-video', 'double-tap-gesture-handler',
    'force-press-gesture-handler', 'horizontal-drag-gesture-handler',
    'long-press-gesture-handler', 'pan-gesture-handler',
    'scale-gesture-handler', 'tap-gesture-handler', 'vertical-drag-gesture-handler',
    'draggable-sheet', 'nested-scroll-body', 'nested-scroll-header', 'span',
    'icon', 'editor', 'audio', 'iframe', 'include', 'import', 'recycle-view',
}

errors = []
warnings = []

# 第三方组件库（vant dist 等）不做事件/资源校验，避免误报
THIRD_PARTY_DIRS = ('dist', 'node_modules', 'ec-canvas', 'miniprogram_npm')


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def is_third_party(path):
    return rel(path).split('/')[0] in THIRD_PARTY_DIRS


def walk_files(exts, skip_dirs=()):
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in skip_dirs and not d.startswith('_')]
        for name in files:
            if name.lower().endswith(exts):
                yield os.path.join(base, name)


def check_json():
    all_json = {}
    for path in walk_files(('.json',)):
        try:
            with open(path, encoding='utf-8') as fh:
                all_json[path] = json.load(fh)
        except Exception as exc:  # noqa: BLE001
            errors.append('[JSON] 解析失败 %s -> %s' % (rel(path), exc))
    return all_json


def resolve_component(owner_json_path, ref):
    """把 usingComponents 的引用解析成绝对路径（不含扩展名）"""
    if ref.startswith('/'):
        return os.path.normpath(os.path.join(ROOT, ref.lstrip('/')))
    base = os.path.dirname(owner_json_path)
    return os.path.normpath(os.path.join(base, ref))


def check_pages(all_json):
    app = all_json.get(os.path.join(ROOT, 'app.json'))
    if not app:
        errors.append('[app] 缺少 app.json')
        return []
    pages = app.get('pages') or []
    if not pages:
        errors.append('[app] pages 为空')
    for page in pages:
        for ext in ('.js', '.wxml', '.json'):
            target = os.path.join(ROOT, page + ext)
            if not os.path.isfile(target):
                errors.append('[page] 缺少文件 %s' % rel(target))
    return pages


def collect_components(all_json):
    """返回 {json文件路径: {标签名: 绝对路径}}"""
    table = {}
    for path, data in all_json.items():
        refs = data.get('usingComponents') if isinstance(data, dict) else None
        if not refs:
            continue
        table[path] = {}
        for tag, ref in refs.items():
            target = resolve_component(path, ref)
            if not os.path.isfile(target + '.js'):
                errors.append(
                    '[component] %s 中 %s -> %s 不存在' % (rel(path), tag, rel(target + '.js'))
                )
            table[path][tag] = target
    return table


def global_components(all_json):
    app = all_json.get(os.path.join(ROOT, 'app.json')) or {}
    refs = app.get('usingComponents') or {}
    return set(refs.keys())


IMG_RE = re.compile(r'src\s*=\s*"(/[^"{}]+)"')
TAG_RE = re.compile(r'<([a-zA-Z][a-zA-Z0-9-]*)')
BIND_RE = re.compile(r'\b(?:bind|catch)(?::)?([a-zA-Z]+)\s*=\s*"([^"{}]+)"')


def check_wxml(all_json, globals_):
    for path in walk_files(('.wxml',)):
        if is_third_party(path):
            continue
        with open(path, encoding='utf-8') as fh:
            text = fh.read()

        # 1) 本地图片资源
        for ref in IMG_RE.findall(text):
            target = os.path.join(ROOT, ref.lstrip('/'))
            if not os.path.isfile(target):
                errors.append('[asset] %s 引用 %s 不存在' % (rel(path), ref))

        # 2) 自定义组件标签是否已注册
        owner_json = path[: -len('.wxml')] + '.json'
        declared = set()
        if os.path.isfile(owner_json):
            data = all_json.get(owner_json) or {}
            declared = set((data.get('usingComponents') or {}).keys())
        for tag in set(TAG_RE.findall(text)):
            if tag in BUILTIN_TAGS or tag in globals_ or tag in declared:
                continue
            if '-' not in tag:
                continue
            errors.append('[wxml] %s 使用了未注册的组件 <%s>' % (rel(path), tag))

        # 3) 事件绑定函数是否存在
        js_path = path[: -len('.wxml')] + '.js'
        if os.path.isfile(js_path):
            with open(js_path, encoding='utf-8') as fh:
                js_text = fh.read()
            for _, handler in BIND_RE.findall(text):
                handler = handler.strip()
                if not handler or '.' in handler:
                    continue
                if not re.search(r'\b%s\s*\(' % re.escape(handler), js_text):
                    errors.append(
                        '[event] %s 绑定 %s，但 %s 中未定义'
                        % (rel(path), handler, rel(js_path))
                    )


def check_wxss_vars():
    """确认 app.wxss 中定义的 CSS 变量被 pages 正常使用（仅提示）"""
    app_wxss = os.path.join(ROOT, 'app.wxss')
    if not os.path.isfile(app_wxss):
        errors.append('[style] 缺少 app.wxss')
        return
    with open(app_wxss, encoding='utf-8') as fh:
        text = fh.read()
    defined = set(re.findall(r'(--[a-z0-9-]+)\s*:', text))
    used = set()
    for path in walk_files(('.wxss',)):
        if path == app_wxss or is_third_party(path):
            continue
        with open(path, encoding='utf-8') as fh:
            used |= set(re.findall(r'var\((--[a-z0-9-]+)\)', fh.read()))
    missing = used - defined
    if missing:
        warnings.append('[style] 未定义的 CSS 变量: %s' % ', '.join(sorted(missing)))


def check_js_syntax(node_exe):
    """用 node --check 对自有 JS（CommonJS）做语法检查"""
    import subprocess

    targets = []
    for path in walk_files(('.js',)):
        if is_third_party(path):
            continue
        targets.append(path)
    for path in targets:
        proc = subprocess.run(
            [node_exe, '--check', path],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or '').strip().splitlines()
            errors.append(
                '[js] %s 语法错误: %s' % (rel(path), detail[-1] if detail else '?')
            )
    return len(targets)


def main():
    all_json = check_json()
    pages = check_pages(all_json)
    collect_components(all_json)
    globals_ = global_components(all_json)
    check_wxml(all_json, globals_)
    check_wxss_vars()

    node_exe = sys.argv[1] if len(sys.argv) > 1 else ''
    checked = 0
    if node_exe and os.path.isfile(node_exe):
        checked = check_js_syntax(node_exe)

    print('=' * 64)
    print('页面 %d 个，自有 JS 语法检查 %d 个' % (len(pages), checked))
    if warnings:
        print('警告 %d 项:' % len(warnings))
        for item in warnings:
            print('  - ' + item)
    if errors:
        print('错误 %d 项:' % len(errors))
        for item in errors:
            print('  x ' + item)
    else:
        print('校验通过：未发现错误')
    print('=' * 64)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
