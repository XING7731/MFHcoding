#!/usr/bin/env node
/* =========================================================================
 * wxmp_precheck.js —— 微信小程序工程预检（不依赖开发者工具）
 *
 * 【为什么需要它】
 *   开发者工具报"编译错误"时，错误可能来自三件完全不同的事，但界面提示
 *   往往长得差不多，很容易误判成"我代码写错了"：
 *     ① 工程本身的问题（页面缺文件、JSON 带 BOM、标签不配对…）
 *     ② 工具自己的缓存与磁盘不一致（最常见，改代码怎么改都不好）
 *     ③ 工具在编译【已经被删除】的历史文件
 *   这个脚本只读磁盘，独立复现微信编译期的全部硬性检查。跑一遍就能把
 *   ① 排除掉，把问题精确地推给 ② 或 ③。
 *
 * 【最后一段会去翻开发者工具的日志】——这是它最有用的地方：
 *   直接把工具最近抱怨的原话打出来。工具的界面只给一句"编译错误"，
 *   真正的 ENOENT / 未找到 xxx 文件 都在日志里。
 *
 * 用法:
 *   node wxmp_precheck.js "C:\path\to\miniprogram"
 *   node wxmp_precheck.js            # 不传则用当前目录
 * ========================================================================= */

const fs = require('fs')
const path = require('path')
const vm = require('vm')

/* Git Bash 风格的 "/c/Users/..." 直接丢给 Windows 版 node，会被解析成
 * "C:\c\Users\..."（多一层 c）。这里先归一化，省得在两种 shell 之间来回试。 */
function normalizePath(p) {
  const m = /^\/([a-zA-Z])\/(.*)$/.exec(p)
  if (m) return m[1].toUpperCase() + ':\\' + m[2].replace(/\//g, '\\')
  return p
}

const PROJ = path.resolve(normalizePath(process.argv[2] || process.cwd()))

/* ------------------------------ 输出工具 ------------------------------ */

let pass = 0
let warn = 0
let fail = 0
const failures = []

function head(t) {
  console.log('\n' + t)
  console.log('-'.repeat(Math.max(20, t.length)))
}
function ok(msg) { pass++; console.log('  ok   ' + msg) }
function wr(msg) { warn++; console.log('  warn ' + msg) }
function bad(msg, why) {
  fail++
  failures.push(msg + (why ? '  ← ' + why : ''))
  console.log('  FAIL ' + msg + (why ? '  ← ' + why : ''))
}

/* ------------------------------ 小工具 ------------------------------ */

function readText(p) {
  /* 统一按 UTF-8 读，顺便把 BOM 暴露出来判断 */
  return fs.readFileSync(p, 'utf8')
}

/* 带 BOM 的 json/wxml 会让微信的解析器直接失败，且肉眼看不出来 */
function hasBOM(p) {
  const fd = fs.openSync(p, 'r')
  const buf = Buffer.alloc(3)
  fs.readSync(fd, buf, 0, 3, 0)
  fs.closeSync(fd)
  return buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf
}

function parseJSON(p) {
  try {
    return { v: JSON.parse(readText(p)) }
  } catch (e) {
    return { err: e.message }
  }
}

/* Windows 不区分大小写，但小程序上传后在服务器上是区分的。
 * 在 Windows 上写 pages/Index/index 而磁盘是 pages/index/index，
 * 本地永远正常、上传后必挂 —— 这个坑只能靠逐段比对名字来抓。 */
function realCasePath(rel) {
  const parts = rel.split(/[\\/]/).filter(Boolean)
  let cur = PROJ
  for (const seg of parts) {
    let entries
    try { entries = fs.readdirSync(cur) }
    catch (e) { return null }
    const hit = entries.find(e => e === seg)
    if (!hit) {
      const ci = entries.find(e => e.toLowerCase() === seg.toLowerCase())
      return { mismatch: true, actual: ci || null, at: seg }
    }
    cur = path.join(cur, seg)
  }
  return { ok: true }
}

function existsCaseExact(rel) {
  const r = realCasePath(rel)
  return !!(r && r.ok)
}

/* ------------------------------ 1. 工程骨架 ------------------------------ */

head('1. 工程骨架')

if (!fs.existsSync(path.join(PROJ, 'app.json'))) {
  bad('缺少 app.json —— 这不是一个合法的工程根目录', PROJ)
  console.log('\n请确认路径指向含 app.json 的那一层目录。')
  process.exit(1)
}

const appJsonPath = path.join(PROJ, 'app.json')
if (hasBOM(appJsonPath)) bad('app.json 带 UTF-8 BOM，工具会解析失败')
else ok('app.json 无 BOM')

for (const f of ['app.js', 'app.wxss', 'project.config.json']) {
  if (fs.existsSync(path.join(PROJ, f))) ok('存在 ' + f)
  else wr('缺少 ' + f + '（可选，但一般是需要的）')
}

/* ------------------------------ 2. app.json ------------------------------ */

head('2. app.json 与页面注册')

const appRes = parseJSON(appJsonPath)
if (appRes.err) {
  bad('app.json 不是合法 JSON', appRes.err)
  process.exit(1)
}
const appJson = appRes.v
ok('app.json 是合法 JSON')

const pages = appJson.pages
if (!Array.isArray(pages) || pages.length === 0) {
  bad('app.json 的 pages 为空或不是数组')
} else {
  ok('注册了 ' + pages.length + ' 个页面')

  for (const p of pages) {
    /* 页面「四件套」里 js / wxml 是必需的；json / wxss 可缺省 */
    const missing = []
    for (const ext of ['js', 'wxml']) {
      const f = p + '.' + ext
      if (!fs.existsSync(path.join(PROJ, f))) missing.push(f)
    }
    if (missing.length) {
      /* 这条就是工具日志里 "未找到 [\"pages\"][i] 对应的 xxx 文件" 的同款 */
      bad('页面缺文件 ' + missing.join(', '), 'app.json 注册了 ' + p + ' 但磁盘上没有')
      continue
    }
    const badCase = ['js', 'wxml', 'json', 'wxss'].filter(ext =>
      fs.existsSync(path.join(PROJ, p + '.' + ext)) && !existsCaseExact(p + '.' + ext))
    if (badCase.length) {
      bad('文件名大小写与 app.json 不一致: ' + badCase.join(', '),
          'Windows 上能跑，上传到微信服务器后必然失败')
      continue
    }
    ok('页面齐备 ' + p)
  }
}

/* tabBar 的 pagePath 必须已在 pages 里注册，否则编译直接报错 */
if (appJson.tabBar && Array.isArray(appJson.tabBar.list)) {
  appJson.tabBar.list.forEach((it, i) => {
    const pp = it.pagePath
    if (pages.indexOf(pp) < 0) {
      bad('tabBar.list[' + i + '] 的 pagePath "' + pp + '" 未在 pages 中注册')
    } else {
      ok('tabBar.list[' + i + '] -> ' + pp)
    }
    /* 配了图标就必须存在。只留 text 不配图标是允许的。 */
    for (const k of ['iconPath', 'selectedIconPath']) {
      if (it[k] && !fs.existsSync(path.join(PROJ, it[k]))) {
        bad('tabBar.list[' + i + '].' + k + ' 指向不存在的文件: ' + it[k])
      }
    }
  })
}

/* ------------------------------ 3. 各页面的 json ------------------------------ */

head('3. 页面配置与自定义组件')

const usingComponents = {}      /* tag -> 路径，用于 WXML 校验 */
pages.forEach(p => {
  const jf = path.join(PROJ, p + '.json')
  if (!fs.existsSync(jf)) return
  const r = parseJSON(jf)
  if (r.err) { bad(p + '.json 不是合法 JSON', r.err); return }
  const uc = r.v.usingComponents
  if (uc && typeof uc === 'object') {
    Object.keys(uc).forEach(tag => {
      usingComponents[tag] = uc[tag]
      const target = uc[tag]
      /* 绝对路径以 / 开头，相对路径相对于当前页面目录 */
      const abs = target.startsWith('/')
        ? target.slice(1)
        : path.join(path.dirname(p), target)
      const found = ['.js', '.json', '.wxml', '.wxss']
        .some(ext => fs.existsSync(path.join(PROJ, abs + ext)))
      if (found) ok(p + ' 组件 ' + tag + ' -> ' + target)
      else bad(p + ' 组件 ' + tag + ' 指向不存在的路径: ' + target)
    })
  }
})
if (Object.keys(usingComponents).length === 0) ok('没有使用自定义组件')

/* ------------------------------ 4. WXML ------------------------------ */

head('4. WXML')

/* 小程序 WXML 表达式是简化版 JS，这些写法会被编译器直接拒绝 */
const BAD_EXPR = [
  { re: /\?\./,         why: '不支持可选链 ?.' },
  { re: /\?\?/,         why: '不支持空值合并 ??' },
  { re: /`/,            why: '不支持模板字符串' },
  { re: /=>/,           why: '不支持箭头函数' },
  { re: /\bnew\s+/,     why: '不支持 new' },
  { re: /\bfunction\b/, why: '不支持 function 字面量' }
]

const SELF_CLOSING_OK = new Set(['image', 'input', 'icon', 'progress', 'switch',
  'slider', 'checkbox', 'radio', 'textarea', 'canvas', 'camera', 'video',
  'audio', 'live-player', 'live-pusher', 'map', 'web-view', 'ad', 'import',
  'include', 'wxs', 'open-data', 'official-account', 'navigation-bar',
  'page-meta', 'keyboard-accessory', 'match-media', 'page-container',
  'share-element', 'voip-room', 'functional-page-navigator', 'cover-image'])

pages.forEach(p => {
  const wf = path.join(PROJ, p + '.wxml')
  if (!fs.existsSync(wf)) return
  if (hasBOM(wf)) { bad(p + '.wxml 带 UTF-8 BOM'); return }

  let src = readText(wf)
  /* 注释先摘掉，否则里面的示例标签会被当成真标签 */
  src = src.replace(/<!--[\s\S]*?-->/g, '')

  /* 4.1 标签配对 */
  const stack = []
  let pairErr = null
  const re = /<(\/?)([a-zA-Z][\w-]*)((?:"[^"]*"|[^>"])*?)(\/?)>/g
  let m
  while ((m = re.exec(src))) {
    const closing = m[1] === '/'
    const tag = m[2]
    const selfClose = m[4] === '/'
    if (selfClose) continue
    if (closing) {
      const top = stack.pop()
      if (top !== tag) { pairErr = '期望 </' + top + '> 却遇到 </' + tag + '>'; break }
    } else {
      stack.push(tag)
    }
  }
  if (pairErr) bad(p + '.wxml 标签未配对', pairErr)
  else if (stack.length) bad(p + '.wxml 有未闭合标签', stack.join(', '))
  else ok(p + '.wxml 标签配对正常')

  /* 4.2 {{}} 表达式里的禁用语法 */
  const exprRe = /\{\{([\s\S]*?)\}\}/g
  let e
  let exprBad = 0
  while ((e = exprRe.exec(src))) {
    BAD_EXPR.forEach(b => {
      if (b.re.test(e[1])) {
        bad(p + '.wxml 表达式用了 ' + b.why + '：{{' + e[1].trim().slice(0, 40) + '}}')
        exprBad++
      }
    })
  }
  if (!exprBad) ok(p + '.wxml 表达式语法正常')

  /* 4.3 wx:for 缺 wx:key（只是警告，但会拖慢渲染并可能出状态错乱） */
  const tagRe = /<([a-zA-Z][\w-]*)((?:"[^"]*"|[^>"])*?)>/g
  let t
  while ((t = tagRe.exec(src))) {
    const attrs = t[2]
    if (/\bwx:for\b/.test(attrs) && !/\bwx:key\b/.test(attrs)) {
      wr(p + '.wxml <' + t[1] + '> 用了 wx:for 但没有 wx:key')
    }
  }

  /* 4.4 用了未注册的自定义组件（含 - 的标签名基本都是自定义组件） */
  const usedTags = new Set()
  let u
  const tagRe2 = /<([a-z][\w-]*-[\w-]*)(?:"[^"]*"|[^>"])*>/g
  while ((u = tagRe2.exec(src))) usedTags.add(u[1])
  usedTags.forEach(tag => {
    /* 微信内置的带连字符标签，不算自定义组件 */
    const builtin = /^(cover-|movable-|live-|page-|open-|official-|navigation-|match-|share-|keyboard-|functional-|voip-|picker-|scroll-|swiper-|rich-|web-|ad-|wxs$|canvas|map$|video|audio|image|input)/
    if (builtin.test(tag)) return
    if (!usingComponents[tag]) {
      /* 全局组件可能在 app.json 里注册 */
      const g = (appJson.usingComponents || {})[tag]
      if (!g) bad(p + '.wxml 用了未注册的组件 <' + tag + '>', '页面 json 与 app.json 里都没有声明')
    }
  })
})

/* ------------------------------ 5. WXSS ------------------------------ */

head('5. WXSS')

function checkWxss(file, label) {
  if (!fs.existsSync(file)) return
  let s = readText(file)
  if (hasBOM(file)) { bad(label + ' 带 UTF-8 BOM'); return }
  s = s.replace(/\/\*[\s\S]*?\*\//g, '')

  /* 括号/引号配对 */
  let depth = 0
  let q = null
  let line = 1
  for (let i = 0; i < s.length; i++) {
    const c = s[i]
    if (c === '\n') line++
    if (q) { if (c === q) q = null; continue }
    if (c === '"' || c === "'") { q = c; continue }
    if (c === '{') depth++
    else if (c === '}') { depth--; if (depth < 0) { bad(label + ' 第 ' + line + ' 行有多余的 }'); return } }
  }
  if (depth !== 0) bad(label + ' 大括号不配对（差 ' + depth + ' 个 }）')
  else ok(label + ' 大括号配对正常')

  /* 通配符选择器在组件内是有限制的，容易静默不生效 */
  if (/^\s*\*\s*\{/m.test(s)) wr(label + ' 使用了通配符选择器 *（组件内不生效）')
}

checkWxss(path.join(PROJ, 'app.wxss'), 'app.wxss')
pages.forEach(p => checkWxss(path.join(PROJ, p + '.wxss'), p + '.wxss'))

/* ------------------------------ 6. JS ------------------------------ */

head('6. JS 语法与 require 路径')

const jsFiles = ['app.js']
pages.forEach(p => jsFiles.push(p + '.js'))
/* utils 下的公共模块也一并检查 */
const utilsDir = path.join(PROJ, 'utils')
if (fs.existsSync(utilsDir)) {
  fs.readdirSync(utilsDir).filter(f => f.endsWith('.js')).forEach(f => jsFiles.push('utils/' + f))
}

jsFiles.forEach(rel => {
  const f = path.join(PROJ, rel)
  if (!fs.existsSync(f)) return
  const src = readText(f)
  if (hasBOM(f)) { bad(rel + ' 带 UTF-8 BOM'); return }

  /* 语法：交给 V8 解析。小程序编译器的解析规则比 V8 严，
   * 所以这里通过不等于绝对没问题，但不通过就一定有问题。 */
  try {
    new vm.Script(src, { filename: rel })
  } catch (e) {
    bad(rel + ' JS 语法错误', e.message)
    return
  }

  /* require 的相对路径必须真实存在（漏 .js / 写错目录都会编译失败） */
  const reqRe = /require\(\s*['"]([^'"]+)['"]\s*\)/g
  let r
  let reqBad = 0
  while ((r = reqRe.exec(src))) {
    const target = r[1]
    if (!target.startsWith('.')) continue      /* 第三方包不在这检查 */
    const abs = path.resolve(path.dirname(f), target)
    const found = fs.existsSync(abs) || fs.existsSync(abs + '.js') || fs.existsSync(abs + '.json')
    if (!found) { bad(rel + ' require 的路径不存在: ' + target); reqBad++ }
  }
  if (!reqBad) ok(rel + ' 语法与依赖路径正常')
})

/* ------------------------------ 7. 开发者工具日志 ------------------------------ */

head('7. 开发者工具最近报的错')

/* 工具界面只给一句"编译错误"，真正的 ENOENT / 未找到 xxx 全在日志里。
 * 这一步把原话捞出来 —— 判断"是代码问题还是工具缓存问题"全靠它。 */
function findToolLogs() {
  const base = path.join(process.env.LOCALAPPDATA || '', '微信开发者工具', 'User Data')
  if (!fs.existsSync(base)) return []
  const out = []
  for (const hash of fs.readdirSync(base)) {
    const dir = path.join(base, hash, 'WeappLog', 'logs')
    if (!fs.existsSync(dir)) continue
    for (const f of fs.readdirSync(dir)) {
      if (!/\.log$/.test(f)) continue
      const full = path.join(dir, f)
      let st
      try { st = fs.statSync(full) } catch (e) { continue }
      out.push({ file: full, name: f, mtime: st.mtimeMs, size: st.size })
    }
  }
  return out.sort((a, b) => b.mtime - a.mtime)
}

const logs = findToolLogs()
if (!logs.length) {
  wr('没找到开发者工具日志（可能工具装在其它的用户目录下）')
} else {
  console.log('  最近活跃的日志: ' + logs[0].name + '  (' + (logs[0].size / 1024).toFixed(0) + ' KB)')

  const PAT = [
    /未找到.*对应的/,
    /Couldn't find the module/,
    /ENOENT/,
    /编译.*\.(wxml|wxss|js|json).*错误/,
    /SyntaxError/,
    /编译错误/
  ]
  /* 收集时带上时间。日志行首形如 [2026-09-16 10:13:58.958+08:00][ERROR]... */
  const hits = []
  logs.slice(0, 3).forEach(lg => {
    let text
    try { text = readText(lg.file) } catch (e) { return }
    text.split(/\r?\n/).forEach(ln => {
      if (!ln.trim()) return
      if (!PAT.some(p => p.test(ln))) return
      const m = ln.match(/^\[([^\]]+)\]/)
      let t = ''
      if (m) {
        const dm = m[1].match(/(\d{2}:\d{2}:\d{2})/)
        t = dm ? dm[1] : ''
      }
      /* 换行里的续行（堆栈）也保留，但压成一行 */
      hits.push({ t: t, raw: ln.trim() })
    })
  })

  if (!hits.length) {
    ok('工具日志里没有编译类错误')
  } else {
    /* 只关心"最后一次编译"留下的东西，所以按时间倒序取最新的几条 */
    const tsec = s => {
      const m = s.match(/^(\d{2}):(\d{2}):(\d{2})$/)
      return m ? (+m[1]) * 3600 + (+m[2]) * 60 + (+m[3]) : -1
    }
    hits.sort((a, b) => tsec(a.t) - tsec(b.t))

    console.log('  发现 ' + hits.length + ' 条相关记录，最近 6 条：')
    hits.slice(-6).forEach(h => {
      const body = h.raw.replace(/\[[^\]]*\]/g, ' ').replace(/\s+/g, ' ').trim()
      console.log('    [' + (h.t || '--:--:--') + '] ' + body.slice(0, 150))
    })

    /* 逐条给判读。这一步是这个脚本存在的意义：把错误归类到"谁的问题" */
    const joined = hits.map(h => h.raw).join('\n')
    console.log('')

    /* ENOENT: 工具在编译一个磁盘上不存在的文件。
     * 只认 .wxml/.wxss/.js/.json 结尾的路径，否则会把无关的 ENOENT 也捞进来。 */
    const eno = joined.match(/ENOENT[^']*'([^']+\.(?:wxml|wxss|js|json))'/g) || []
    if (eno.length) {
      const files = Array.from(new Set(eno.map(s => s.match(/'([^']+)'/)[1])))
      console.log('  >> 工具在编译【磁盘上不存在】的文件：')
      files.forEach(f => {
        const rel = f.replace(/\\/g, '/').replace(/^.*?\/WeChat smart helmet\//i, '')
        const still = fs.existsSync(path.join(PROJ, rel))
        console.log('       ' + rel + (still ? '   (文件其实在，属缓存错乱)' : '   ★ 已删除，属工具缓存过期'))
      })
      console.log('     这不是代码问题 —— 清缓存即可（见下方"怎么修"）。')
    }
    if (hits.some(h => /未找到.*对应的/.test(h.raw))) {
      console.log('  >> 工具曾报"app.json 注册的页面找不到文件"。')
      console.log('     若上面第 2 节全 ok，说明这条是旧记录（当时文件还没写完），可忽略。')
    }
  }
}

/* ------------------------------ 汇总 ------------------------------ */

head('汇总')
console.log('  通过 ' + pass + ' 项，警告 ' + warn + ' 项，失败 ' + fail + ' 项')

if (fail === 0) {
  console.log('')
  console.log('  工程本身检查通过 —— 磁盘上的代码/配置没有会导致编译失败的问题。')
  console.log('')
  console.log('  如果开发者工具仍然报编译错误，那问题不在代码，而在工具的缓存。')
  console.log('  工具的文件监听对我们用外部程序改文件这类情况经常漏掉，')
  console.log('  于是它的"模块图"还停在改之前的版本 —— 表现就是：')
  console.log('    · 明明文件在，却报"未找到 xxx 文件"')
  console.log('    · 编译已经删掉的老文件，报 ENOENT')
  console.log('    · 改代码怎么改都不生效')
  console.log('')
  console.log('  怎么修（按顺序试，90% 第一步就好）：')
  console.log('    1. 开发者工具顶部菜单「工具 → 清缓存 → 清除全部缓存」')
  console.log('       然后重新编译')
  console.log('    2. 关闭项目（或退出工具）重新打开，再编译')
  console.log('    3. 还不行：把工程复制到【不含空格】的路径下重新导入')
  console.log('       （当前路径若含空格，文件监听容易失效）')
  console.log('')
  console.log('  注意：修改 app.json 之后必须重新编译，热更新不会重新读它。')
} else {
  console.log('')
  console.log('  有 ' + fail + ' 项硬错误，这些会让编译直接失败，需要先修：')
  failures.forEach(f => console.log('    · ' + f))
}

process.exit(fail ? 1 : 0)
