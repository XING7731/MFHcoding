#!/usr/bin/env node
/* =========================================================================
 * wxmp_thresh_test.js —— 阈值设置 / 在线下发（小程序侧）的静态验证
 *
 * 【这一轮要防的三类失效，以及各自靠什么断言挡住】
 *
 *  ① 两侧的"镜像常量"悄悄对不上。
 *     小程序里抄了一份固件的门限与量程（bemfa.js 的 THRESH / T_DEF / T_LIMIT），
 *     因为两端必须各自能独立工作。抄错不会有任何报错，只会表现成
 *     "设备在响，小程序说正常"。本脚本【直接读固件头文件】逐项核对。
 *     ⇒ 第 1、2 节。这一节立着的时候就已经抓到两个真错（accelHigh 口径 / 光照门限口径）。
 *       其中光照门限 2026-09-22 已随功能整体删除（docs/39），现由第 2 节反向断言盯住。
 *
 *  ② 指纹算错。这是最阴的一种：设备其实收到了也生效了，只是小程序显示
 *     "未同步"，人会不停地重发。算法再简单也可能在"字节序 / 有符号 / 取模"
 *     上分叉，所以不能靠两边各写一遍然后相信它们一样。
 *     ⇒ 第 3 节：拿 tools/alarm_cfg_sim.py 产出的向量文件逐条复算。
 *
 *  ③ 发出去的报文固件读不回来（×10 与整数混用把 gas=30 读成 300）。
 *     ⇒ 第 4 节：拿 Python 侧产出的【报文原文】逐字节对照。
 *        这样做的好处是不必在 JS 里重写一遍固件的扫描器 —— 自己重写一份
 *        解析器来验自己生成的报文，等于自证。字节相同 + Python 侧已验证
 *        "自己的报文能被自己的规则读回同一组值"，链条才是闭的。
 *
 * 【本脚本不验什么】真机上巴法云的推送是否真的到达、蜂鸣器响不响 ——
 *   那些只能在设备上验。这里只保证"发给设备的这条报文，和固件期望的一模一样"。
 *
 * 运行: node wxmp_thresh_test.js
 * ========================================================================= */

const fs = require('fs')
const path = require('path')

const HERE = __dirname
const PROJ = path.dirname(HERE)
const MP = 'C:/Users/27362/Desktop/WeChat smart helmet'

const bemfa = require(path.join(MP, 'utils', 'bemfa.js'))

let pass = 0, fail = 0

/* 【为什么脚本自己写结果文件】本机 PowerShell 管道的 stdout 不回显，
 * 凡是"要看一眼"的结果都必须由脚本自己落成 UTF-8 文件再读 —— 工程里所有
 * *_sim.py 都遵循这条，这个 js 也一样，落到 wxmp_thresh_test_result.txt。
 * 同时仍然打到 stdout，方便在终端里直接跑。 */
const OUT = []
function say(s) {
  OUT.push(s)
  try { console.log(s) } catch (e) { /* 打印失败不影响结论 */ }
}

function ok(cond, name, extra) {
  if (cond) { pass++; say('  ok   ' + name) }
  else { fail++; say('  FAIL ' + name + (extra !== undefined ? '   -> ' + extra : '')) }
}
function head(t) {
  say('')
  say(t)
  say('-'.repeat(Math.max(20, t.length)))
}
function sleep(ms) { return new Promise(r => setTimeout(r, ms)) }

/* ======================================================================
 * 0. 读固件头文件
 * ====================================================================== */

const HDR_CFG = fs.readFileSync(path.join(PROJ, 'user', 'Inc', 'user_config.h'), 'utf8')
const HDR_ALM = fs.readFileSync(path.join(PROJ, 'user', 'Inc', 'alarm_cfg.h'), 'utf8')

/* 取 #define NAME <整数>。带括号、负号、u/l 后缀、行尾注释都要认。
 * 【为什么不复用 Python 那条正则】那条只处理正数（它的用途是读默认值），
 * 而这里要读 CFG_TEMP_MIN_X10 = (-200) 这种负数边界。 */
function macroInt(text, name) {
  const re = new RegExp(
    '^#define\\s+' + name + '\\s+\\(?(-?\\d+)\\)?\\s*[uUlL]*\\s*(?:/\\*.*)?$', 'm')
  const m = re.exec(text)
  if (!m) throw new Error('头文件里找不到宏 ' + name)
  return Number(m[1])
}
function macroFloat(text, name) {
  const re = new RegExp(
    '^#define\\s+' + name + '\\s+\\(?(-?\\d+(?:\\.\\d+)?)[fF]?\\)?\\s*(?:/\\*.*)?$', 'm')
  const m = re.exec(text)
  if (!m) throw new Error('头文件里找不到宏 ' + name)
  return Number(m[1])
}

/* ======================================================================
 * 1. THRESH（展示/二次判读用的镜像常量）必须与固件一致
 * ====================================================================== */

head('1. bemfa.js 的 THRESH 与固件门限逐项核对')

/* [固件里的宏名, 该宏所在头文件, 换算成界面单位要除多少] */
const THRESH_MAP = [
  ['gas',       'GAS_ALARM_LEVEL',    HDR_CFG, 1],
  ['tempHigh',  'TEMP_HIGH_X10',      HDR_CFG, 10],
  ['tempLow',   'TEMP_LOW_X10',       HDR_CFG, 10],
  ['humiHigh',  'HUMI_HIGH_X10',      HDR_CFG, 10],
  ['humiLow',   'HUMI_LOW_X10',       HDR_CFG, 10],
  ['hrMin',     'HR_MIN_BPM',         HDR_CFG, 1],
  ['hrMax',     'HR_MAX_BPM',         HDR_CFG, 1],
  ['spo2Min',   'SPO2_MIN_PERCENT',   HDR_CFG, 1],
  ['accelHigh', 'FALL_ACCEL_HIGH_G',  HDR_CFG, 1]
]

THRESH_MAP.forEach(function (row) {
  const k = row[0], name = row[1], text = row[2], div = row[3]
  const raw = (name === 'FALL_ACCEL_HIGH_G')
    ? macroFloat(text, name)
    : macroInt(text, name)
  const want = raw / div
  const got = bemfa.THRESH[k]
  ok(got === want, 'THRESH.' + k + ' === ' + name + (div === 1 ? '' : ' / 10') +
                   '  (' + got + ' vs ' + want + ')', got + ' vs ' + want)
})

/* 【2026-09-22 光照功能已整体删除, 原来的 LIGHT_CAL_ENABLED 绊线随之取消】
 * 原来这里断言 LIGHT_CAL_ENABLED 仍为 0, 用来防止"标定被打开而 luxLow 没跟着改"。
 * 现在 THRESH 里不再有 luxLow/luxHigh, 固件里也不再有 LIGHT_* 宏, 所以:
 *   · 上表不再有 lux 两行;
 *   · 下面改为反向断言 —— 这几个宏【必须已经不存在】, 否则说明删除做了一半,
 *     而"删一半"会留下一个没人读的死宏, 下一轮又有人照着它把功能加回来。 */
const LIGHT_MACROS_GONE = [
  'LIGHT_CAL_ENABLED', 'LIGHT_DARK_LUX', 'LIGHT_BRIGHT_LUX',
  'LIGHT_DARK_RAW', 'LIGHT_BRIGHT_RAW',
  'LIGHT_CAL_RAW_1', 'LIGHT_CAL_LUX_1', 'LIGHT_CAL_RAW_2', 'LIGHT_CAL_LUX_2',
  'HW_FILL_LIGHT_PRESENT'
]
LIGHT_MACROS_GONE.forEach(function (name) {
  const found = new RegExp('^#define\\s+' + name + '\\s', 'm').test(HDR_CFG)
  ok(!found, '固件里已不存在光照宏 ' + name + '（docs/39 的删除必须彻底）', '仍能找到 ' + name)
})
ok(bemfa.THRESH.luxLow === undefined && bemfa.THRESH.luxHigh === undefined,
   'bemfa.THRESH 里已无 luxLow / luxHigh',
   'luxLow=' + bemfa.THRESH.luxLow + ' luxHigh=' + bemfa.THRESH.luxHigh)

/* ======================================================================
 * 2. T_DEF / T_LIMIT 与固件一致
 * ====================================================================== */

head('2. 出厂默认值与量程（T_DEF / T_LIMIT）与固件一致')

const DEF_MAP = {
  tHi:  ['TEMP_HIGH_X10', HDR_CFG, 10],
  tLo:  ['TEMP_LOW_X10',  HDR_CFG, 10],
  hHi:  ['HUMI_HIGH_X10', HDR_CFG, 10],
  hLo:  ['HUMI_LOW_X10',  HDR_CFG, 10],
  gas:  ['GAS_ALARM_LEVEL', HDR_CFG, 1],
  hrLo: ['HR_MIN_BPM', HDR_CFG, 1],
  hrHi: ['HR_MAX_BPM', HDR_CFG, 1],
  sp:   ['SPO2_MIN_PERCENT', HDR_CFG, 1]
}
Object.keys(DEF_MAP).forEach(function (k) {
  const m = DEF_MAP[k]
  const want = macroInt(m[1], m[0]) / m[2]
  ok(bemfa.T_DEF[k] === want,
     'T_DEF.' + k + ' === ' + m[0] + '  (' + bemfa.T_DEF[k] + ' vs ' + want + ')',
     bemfa.T_DEF[k] + ' vs ' + want)
})

/* buz 没有对应的宏 —— 固件的初值是写死在 AlarmCfg_Init 里的一个赋值语句。
 * 这里改去读源码文本，免得它被改动而小程序不知道。 */
const SRC_ALM = fs.readFileSync(path.join(PROJ, 'user', 'Src', 'alarm_cfg.c'), 'utf8')
ok(/c->buz_enable\s*=\s*1\s*;/.test(SRC_ALM),
   'T_DEF.buz === 1 对应固件 AlarmCfg_Init 里的 buz_enable = 1', '')
ok(bemfa.T_DEF.buz === 1, 'T_DEF.buz === 1', String(bemfa.T_DEF.buz))

/* 量程：T_LIMIT 是界面单位，固件是 ×10（温湿度）/ 整数（其余）。
 * 【为什么必须"完全相等"而不是"包含"】若 T_LIMIT 比固件松，小程序会放行一个
 * 必然被固件整条拒的值 —— 现象就是"点了没反应"；若比固件紧，用户就永远调不到
 * 固件其实接受的值。两个方向都是错的，所以要求逐项相等。 */
const LIM_MAP = {
  tHi:  ['CFG_TEMP_MIN_X10', 'CFG_TEMP_MAX_X10', 10],
  tLo:  ['CFG_TEMP_MIN_X10', 'CFG_TEMP_MAX_X10', 10],
  hHi:  ['CFG_HUMI_MIN_X10', 'CFG_HUMI_MAX_X10', 10],
  hLo:  ['CFG_HUMI_MIN_X10', 'CFG_HUMI_MAX_X10', 10],
  gas:  ['CFG_GAS_MIN',  'CFG_GAS_MAX',  1],
  hrLo: ['CFG_HR_MIN',   'CFG_HR_MAX',   1],
  hrHi: ['CFG_HR_MIN',   'CFG_HR_MAX',   1],
  sp:   ['CFG_SPO2_MIN', 'CFG_SPO2_MAX', 1]
}
Object.keys(LIM_MAP).forEach(function (k) {
  const m = LIM_MAP[k]
  const lo = macroInt(HDR_ALM, m[0]) / m[2]
  const hi = macroInt(HDR_ALM, m[1]) / m[2]
  const g = bemfa.T_LIMIT[k]
  ok(g[0] === lo && g[1] === hi,
     'T_LIMIT.' + k + ' === [' + lo + ', ' + hi + ']  (' + g[0] + ', ' + g[1] + ')',
     JSON.stringify(g) + ' vs [' + lo + ',' + hi + ']')
})

/* buz 的固件判据是"只拒 >1"（uint8 无下限），T_LIMIT 写 [0,1] 与之一致。
 * 这条断言存在的意义：万一将来有人把 buz 改成多档，这里会失败。 */
ok(bemfa.T_LIMIT.buz[0] === 0 && bemfa.T_LIMIT.buz[1] === 1,
   'T_LIMIT.buz === [0,1]，与固件"只拒 >1"一致', JSON.stringify(bemfa.T_LIMIT.buz))

/* 指纹覆盖的字段顺序：两侧都不能改 */
ok(/\(int32 little-endian each\)/.test(HDR_ALM) &&
   /tHi,tLo,hHi,hLo,gas,hrLo,hrHi,sp,buz/.test(HDR_ALM),
   'alarm_cfg.h 声明的指纹顺序仍是 tHi,tLo,hHi,hLo,gas,hrLo,hrHi,sp,buz', '')
ok(bemfa.T_FIELDS.map(f => f.k).join(',') === 'tHi,tLo,hHi,hLo,gas,hrLo,hrHi,sp,buz',
   'T_FIELDS 的顺序与固件 CFG_HASH_ORDER_DOC 一致',
   bemfa.T_FIELDS.map(f => f.k).join(','))

/* 定点 / 整数分流必须与固件一致（这是踩过的坑本身） */
ok(bemfa.T_FIELDS.filter(f => f.x10).map(f => f.k).join(',') === 'tHi,tLo,hHi,hLo',
   '只有温湿度是 ×10 定点，其余按整数下发',
   bemfa.T_FIELDS.filter(f => f.x10).map(f => f.k).join(','))

/* ======================================================================
 * 3. 指纹：与 Python 规格产出的对照向量逐条复算
 * ====================================================================== */

head('3. 指纹 cv 与 alarm_cfg_vectors.txt 逐条对照')

function readLines(file) {
  return fs.readFileSync(path.join(HERE, file), 'utf8')
    .split(/\r?\n/)
    .filter(l => l.trim() && !l.trim().startsWith('#'))
}

/* 报文里字段的顺序与每一格的量纲，就是 T_FIELDS —— 用它把一行向量还原成
 * 界面单位的 T（wire 里温湿度是 ×10，其余是整数）。 */
function T_fromWire(wire) {
  const T = {}
  bemfa.T_FIELDS.forEach(function (f, i) {
    T[f.k] = f.x10 ? wire[i] / 10 : wire[i]
  })
  return T
}

const vectors = readLines('alarm_cfg_vectors.txt')
ok(vectors.length >= 4, '读到 ' + vectors.length + ' 条对照向量', String(vectors.length))

let vecBad = 0
const vecDetail = []
vectors.forEach(function (line) {
  const p = line.trim().split(/\s+/)
  const name = p[0]
  const wantCv = Number(p[1])
  const wire = p.slice(2).map(Number)
  const T = T_fromWire(wire)
  const got = bemfa.cfgHash(T)
  if (got !== wantCv) {
    vecBad++
    vecDetail.push(name + ': JS=' + got + ' PY=' + wantCv)
  }
  ok(got === wantCv, 'cv[' + name + '] = ' + wantCv, 'JS 算出 ' + got)
})

/* 出厂默认值单独钉一遍 —— 这个数同时写在 alarm_cfg.h 的注释里（38972/0x983C），
 * 是最容易被三个人用三种方式记错的一个数。 */
const DEF_CV = bemfa.cfgHash(bemfa.T_DEF)
ok(DEF_CV === 38972, '出厂默认门限的指纹 = 38972 (0x983C)', String(DEF_CV))
ok(/38972\(0x983C\)/.test(HDR_ALM),
   'alarm_cfg.h 注释里写的也是 38972(0x983C)', '')

/* 指纹必须对每一个字段都敏感：逐字段改一个最小单位，指纹都要变。
 * 不敏感的话，"改了门限却显示已生效"就永远发现不了。 */
let coll = 0
const collKeys = []
bemfa.T_FIELDS.forEach(function (f) {
  const lim = bemfa.T_LIMIT[f.k]
  const probe = Object.assign({}, bemfa.T_DEF)
  probe[f.k] = (bemfa.T_DEF[f.k] + (f.x10 ? 0.1 : 1) <= lim[1])
    ? bemfa.T_DEF[f.k] + (f.x10 ? 0.1 : 1)
    : bemfa.T_DEF[f.k] - (f.x10 ? 0.1 : 1)
  if (bemfa.cfgHash(probe) === DEF_CV) { coll++; collKeys.push(f.k) }
})
ok(coll === 0, '每个字段单独动一个最小单位，指纹都会变（无碰撞字段）', collKeys.join(','))

/* ======================================================================
 * 4. 报文原文：与 Python 侧逐字节相同
 * ====================================================================== */

head('4. buildConfigMsg 与 alarm_cfg_msgs.txt 逐字节对照')

const msgs = readLines('alarm_cfg_msgs.txt')
ok(msgs.length === vectors.length,
   '报文条数与向量条数一致', msgs.length + ' vs ' + vectors.length)

let msgBad = 0
const msgDetail = []
msgs.forEach(function (line) {
  const tab = line.indexOf('\t')
  const name = line.slice(0, tab)
  const want = line.slice(tab + 1)
  const row = vectors.filter(v => v.trim().split(/\s+/)[0] === name)[0]
  const T = T_fromWire(row.trim().split(/\s+/).slice(2).map(Number))
  const got = bemfa.buildConfigMsg(T)
  const same = got === want
  if (!same) { msgBad++; msgDetail.push(name) }
  ok(same, '报文[' + name + '] 逐字节相同' + (same ? '' : ''), same ? '' : ('\n        JS: ' + got + '\n        PY: ' + want))
})

ok(msgBad === 0, '所有对照报文逐字节一致', msgDetail.join(','))

/* 报文长度：平台对该主题只保留最新一条，且记录上限 191 字节（见 docs/18）。
 * 下发报文要是超了，会静默失败 —— 这类"太长就丢"的问题只能靠长度断言挡。 */
const sample = bemfa.buildConfigMsg(bemfa.T_DEF, 99)
ok(sample.length < 191, '典型下发报文 ' + sample.length + ' 字节 < 191', String(sample.length))

/* 报文里每个字段的值必须等于 wire（防止字段名写错/漏字段/多字段）。
 * 这里用的是极简正则抽取，不是重写固件扫描器 —— 只验"我写进去的是什么"。 */
function fieldsOfMsg(msg) {
  const out = {}
  const re = /"([A-Za-z][A-Za-z0-9_]*)":(-?\d+(?:\.\d+)?)/g
  let m
  while ((m = re.exec(msg))) out[m[1]] = Number(m[2])
  return out
}
const probeT = { tHi: 25, tLo: -5, hHi: 70, hLo: 30, gas: 25, hrLo: 60, hrHi: 140, sp: 94, buz: 0 }
const probeMsg = bemfa.buildConfigMsg(probeT, 7)
const gotF = fieldsOfMsg(probeMsg)
ok(gotF.cfg === 1, '报文含 "cfg":1 标记（设备靠它认领这条报文）', String(gotF.cfg))
ok(gotF.m === 7, '报文含 "m":7（小程序侧序号，与设备的 seq 错开）', String(gotF.m))
ok(gotF.seq === undefined, '报文里【没有】seq 字段（不能用它，会污染设备的回环送达判据）', '')
let fieldBad = []
bemfa.T_FIELDS.forEach(function (f) {
  const wire = f.x10 ? Math.round(probeT[f.k] * 10) : probeT[f.k]
  const want = f.x10 ? wire / 10 : wire
  if (gotF[f.k] !== want) fieldBad.push(f.k + '=' + gotF[f.k] + ' 期望 ' + want)
})
ok(fieldBad.length === 0, '报文里 9 个字段全部等于按量纲换算后的值', fieldBad.join('; '))
ok(!/\.\d{2}/.test(probeMsg), '报文里没有两位小数（固件的定点解析只认一位）', probeMsg)

/* ======================================================================
 * 5. 校验规则与固件同边界
 * ====================================================================== */

head('5. validateThresh 与固件 cfg_validate 同边界')

function T(over) { return Object.assign({}, bemfa.T_DEF, over || {}) }
function bad(t) { return bemfa.validateThresh(t) }

ok(bad(T()).length === 0, '出厂默认门限本身可以通过校验', bad(T()).join('；'))

/* 量程上下边界：边界值必须【通过】，越界一格必须【拒绝】 */
const EDGES = [
  ['tHi', 60, 61], ['tHi', -20, -21],
  ['tLo', 60, 61], ['tLo', -20, -21],
  ['hHi', 100, 101], ['hHi', 0, -1],
  ['hLo', 100, 101], ['hLo', 0, -1],
  ['hrLo', 200, 201], ['hrLo', 20, 19],
  ['hrHi', 200, 201], ['hrHi', 20, 19],
  ['sp', 100, 101], ['sp', 50, 49],
  ['gas', 100, 101], ['gas', 1, 0]
]
let edgeBad = []
EDGES.forEach(function (e) {
  const k = e[0], inRange = e[1], outRange = e[2]
  const t1 = T(); t1[k] = inRange
  const t2 = T(); t2[k] = outRange
  /* 边界值要能通过（除了会把高低顺序搞颠的那几种组合，单独排除） */
  const invertible = (k === 'tHi' && inRange <= t1.tLo) ||
                     (k === 'tLo' && inRange >= t1.tHi) ||
                     (k === 'hHi' && inRange <= t1.hLo) ||
                     (k === 'hLo' && inRange >= t1.hHi) ||
                     (k === 'hrHi' && inRange <= t1.hrLo) ||
                     (k === 'hrLo' && inRange >= t1.hrHi)
  if (!invertible && bad(t1).length !== 0) {
    edgeBad.push(k + '=' + inRange + ' 应通过却被拒: ' + bad(t1).join('；'))
  }
  if (bad(t2).length === 0) edgeBad.push(k + '=' + outRange + ' 应被拒却通过')
})
ok(edgeBad.length === 0, '16 组量程边界：边界值通过、越界一格被拒', edgeBad.join(' | '))

/* 高低顺序：三条规则各来一发，且要"颠倒"和"相等"都拒 */
const ORDERS = [
  ['tLo >= tHi', { tLo: 40, tHi: 40 }],
  ['tLo > tHi',  { tLo: 41, tHi: 30 }],
  ['hLo >= hHi', { hLo: 90, hHi: 90 }],
  ['hLo > hHi',  { hLo: 95, hHi: 30 }],
  ['hrLo >= hrHi', { hrLo: 120, hrHi: 120 }],
  ['hrLo > hrHi',  { hrLo: 150, hrHi: 60 }]
]
ORDERS.forEach(function (o) {
  const b = bad(T(o[1]))
  ok(b.length > 0, '拒绝 ' + o[0], b.join('；'))
})

/* 【整条拒绝的语义】越界值不能被"逐项采用"—— 校验不过时必须一个字段都不落地。
 * 小程序侧对应的是：problems 非空时 onSend 根本不发请求（第 7 节验）。 */
ok(bad(T({ gas: 500 })).length === 1 && /gas/.test(bad(T({ gas: 500 }))[0]),
   '越界报告里点名到具体字段（用户才知道改哪一项）', bad(T({ gas: 500 })).join('；'))

/* ======================================================================
 * 6. 假 wx 环境
 * ====================================================================== */

const store = {}

function installEnv() {
  const state = {
    httpHandler: null,
    reqs: [],
    toasts: [],
    modals: [],
    clipboard: '',
    tabs: [],
    modalConfirm: true
  }

  global.wx = {
    request(opt) {
      state.reqs.push({ url: opt.url, data: opt.data })
      const t = { aborted: false }
      setTimeout(() => {
        if (t.aborted) return
        const r = state.httpHandler
          ? state.httpHandler(opt)
          : { fail: true, errMsg: 'no handler' }
        if (r.fail) opt.fail && opt.fail({ errMsg: r.errMsg || 'request:fail' })
        else opt.success && opt.success({ statusCode: r.statusCode || 200, data: r.data })
      }, 0)
      return t
    },
    getStorageSync(k) { return store[k] === undefined ? '' : store[k] },
    setStorageSync(k, v) { store[k] = v },
    removeStorageSync(k) { delete store[k] },
    showToast(o) { state.toasts.push(o.title) },
    showModal(o) {
      state.modals.push(o)
      if (o.success) setTimeout(() => o.success({ confirm: state.modalConfirm, cancel: !state.modalConfirm }), 0)
    },
    setClipboardData(o) { state.clipboard = o.data; o.success && o.success() },
    switchTab(o) { state.tabs.push(o.url) },
    stopPullDownRefresh() {},
    nextTick(cb) { setTimeout(cb, 0) },
    getAccountInfoSync() { return { miniProgram: { appId: 'wxtest0000000000' } } },
    getNetworkType(o) { setTimeout(() => o.success && o.success({ networkType: 'wifi' }), 0) },
    getWindowInfo() { return { pixelRatio: 1 } },
    createSelectorQuery() {
      const q = {}
      q.in = () => q
      q.select = () => q
      q.fields = () => q
      q.exec = (cb) => cb([null])
      return q
    }
  }

  global.Page = function (cfg) { state.pageCfg = cfg }
  global.App = function (cfg) { state.app = cfg }
  return state
}

function makePage(cfg) {
  const inst = Object.assign({}, cfg)
  inst.data = JSON.parse(JSON.stringify(cfg.data))
  inst.setData = function (patch) { Object.assign(inst.data, patch) }
  return inst
}

/* 每个页面各自 require 一次（模块级都调了 getApp()，缓存必须清） */
function loadPage(state, rel) {
  const p = path.join(MP, rel)
  delete require.cache[require.resolve(p)]
  state.pageCfg = null
  require(p)
  return state.pageCfg
}

const RAW = {
  cv: bemfa.cfgHash(bemfa.T_DEF),
  /* over 用来临时改某个字段 —— 8.5 节要靠它造出"只有读对了编译期常量才成立"
   * 的那一帧（合加速度很大、且带一项本机存了假值的量），把"是不是真去读了
   * 常量"卡死。 */
  frame(temp, humi, gas, hr, spo2, cvv, over) {
    const o = {
      ts: '00:10:00', seq: 200, hr: hr === undefined ? null : hr,
      spo2: spo2 === undefined ? null : spo2,
      t: temp, h: humi, gas: gas,
      ax: 0.1, ay: 0.2, az: 1.0, st: 'normal', up: 60000, tsv: 0
    }
    if (cvv !== undefined) o.cv = cvv
    if (over) Object.keys(over).forEach(k => { o[k] = over[k] })
    return JSON.stringify(o)
  }
}

function cloudOk(msg) {
  return { statusCode: 200, data: { code: 0, message: 'OK', data: [{ msg: msg, time: '2026-09-18 09:00:00', unix: Math.floor(Date.now() / 1000) }] } }
}

/* ======================================================================
 * 7. 阈值设置页
 * ====================================================================== */

async function testSettingsPage() {
  head('7. 阈值设置页（pages/settings）驱动验证')

  const state = installEnv()
  global.getApp = function () {
    return { getCfg: () => ({ uid: 'u'.repeat(32), topic: 'temp001', channel: 'http', intervalSec: 5 }) }
  }

  Object.keys(store).forEach(k => delete store[k])
  bemfa.resetThresh()

  state.httpHandler = () => cloudOk(RAW.frame(26.4, 58.0, 21, null, null, RAW.cv))
  const cfg = loadPage(state, 'pages/settings/settings.js')
  const page = makePage(cfg)
  page.onLoad()
  page.reload()
  await sleep(30)

  /* --- 7.1 初始状态 = 出厂默认 ---
   * 这里比的是"这一页能改到的字段全集"（滑块行 + 开关行）与"下发报文的字段集"。
   * 要防的是：报文里加了一个字段而界面忘了加控件 —— 那会变成一个用户看不到、
   * 也改不了、却一直和下发给设备的值不一致的字段。 */
  const keysRendered = page.data.editKeys.slice().sort()
  const keysWire = bemfa.T_FIELDS.map(f => f.k).sort()
  ok(keysRendered.join(',') === keysWire.join(','),
     '设置页覆盖了下发报文的全部 9 个字段（滑块 8 个 + 蜂鸣开关 1 个），且没有多余的',
     keysRendered.join(',') + ' vs ' + keysWire.join(','))
  ok(page.data.groups.reduce((a, g) => a.concat(g.items.map(i => i.k)), []).length === 8,
     '滑块行共 8 个（温湿度各 2 + 心率 2 + 血氧 1 + 气体 1）',
     String(page.data.groups.reduce((a, g) => a.concat(g.items.map(i => i.k)), []).length))
  ok(page.data.groups.length === 5, '分成 5 组（温度/湿度/心率/血氧/气体）', String(page.data.groups.length))
  ok(page.data.cmdMsg === bemfa.buildConfigMsg(bemfa.T_DEF),
     '初始报文 = 出厂默认的报文', page.data.cmdMsg)
  ok(page.data.problems.length === 0, '初始状态没有校验问题', JSON.stringify(page.data.problems))
  ok(/\/set$/.test(page.data.cmdTopicText),
     '下发主题以 /set 结尾（不能直发裸主题，否则顶掉上报记录）', page.data.cmdTopicText)

  /* --- 7.2 当前读数（用于"按当前读数填"） --- */
  ok(page.cur && page.cur.temp === 26.4, '取到当前温度 26.4', JSON.stringify(page.cur))
  ok(page.data.cvDev === RAW.cv,
     '回读到设备指纹 ' + RAW.cv, String(page.data.cvDev))
  ok(page.data.cv.level === 'ok',
     '设备指纹与本机一致 → 判为"已生效"', page.data.cv.text)

  /* --- 7.3 拖滑块 --- */
  page.onSlide({ currentTarget: { dataset: { k: 'tHi' } }, detail: { value: 25 } })
  ok(page.data.T.tHi === 25, '拖滑块把 tHi 改成 25', String(page.data.T.tHi))
  ok(page.data.cmdMsg.indexOf('"tHi":25') >= 0, '报文里出现 "tHi":25', page.data.cmdMsg)
  ok(page.data.cv.level === 'warn',
     '改了门限之后指纹对不上 → 判为"未同步"（这正是它该干的事）', page.data.cv.text)

  /* --- 7.4 加减按钮在量程内夹紧 --- */
  page.onSlide({ currentTarget: { dataset: { k: 'tHi' } }, detail: { value: 60 } })
  page.onStep({ currentTarget: { dataset: { k: 'tHi', delta: '1' } } })
  ok(page.data.T.tHi === 60, 'tHi 已在上限 60，再加一次仍停在 60（不会造出必被拒的值）',
     String(page.data.T.tHi))
  page.onSlide({ currentTarget: { dataset: { k: 'tLo' } }, detail: { value: -20 } })
  page.onStep({ currentTarget: { dataset: { k: 'tLo', delta: '-1' } } })
  ok(page.data.T.tLo === -20, 'tLo 已在下限 -20，再减一次仍停在 -20', String(page.data.T.tLo))
  /* 0.5 步进的温湿度，加减按钮按 0.5 走，不能按 1 走 */
  page.onSlide({ currentTarget: { dataset: { k: 'tLo' } }, detail: { value: 10 } })
  page.onStep({ currentTarget: { dataset: { k: 'tLo', delta: '1' } } })
  ok(page.data.T.tLo === 10.5, '温度按 0.5 步进（不是 1）', String(page.data.T.tLo))

  /* --- 7.5 按当前读数填 --- */
  page.onUseCur({ currentTarget: { dataset: { k: 'tHi' } } })
  ok(page.data.T.tHi === 26.5,
     '当前 26.4℃ → 高温门限填成 26.5（对齐到 0.5 步进）', String(page.data.T.tHi))

  /* --- 7.6 蜂鸣器开关 --- */
  page.onBuzChange({ detail: { value: false } })
  ok(page.data.T.buz === 0, '开关关掉 → buz=0', String(page.data.T.buz))
  ok(page.data.cmdMsg.indexOf('"buz":0') >= 0, '报文里出现 "buz":0', page.data.cmdMsg)
  page.onBuzChange({ detail: { value: true } })
  ok(page.data.T.buz === 1, '开关打开 → buz=1', String(page.data.T.buz))

  /* --- 7.7 非法组合：拦住，且不发请求 --- */
  const reqsBefore = state.reqs.length
  page.onSlide({ currentTarget: { dataset: { k: 'tLo' } }, detail: { value: 50 } })
  page.onSlide({ currentTarget: { dataset: { k: 'tHi' } }, detail: { value: 30 } })
  ok(page.data.problems.length > 0,
     '低温 50 > 高温 30 → 界面立刻标出问题', page.data.problems.join('；'))
  const modalsBefore = state.modals.length
  page.onSend()
  await sleep(20)
  ok(state.reqs.length === reqsBefore,
     '有问题时点下发【不发出任何请求】（否则用户只会看到"点了没反应"）',
     String(state.reqs.length - reqsBefore))
  ok(state.modals.length === modalsBefore + 1,
     '弹出模态说明为什么不能下发', String(state.modals.length))
  ok(store[bemfa.KEY_THRESH] === undefined ||
     JSON.stringify(bemfa.loadThresh()) === JSON.stringify(bemfa.T_DEF),
     '非法组合不会污染本机保存的门限', JSON.stringify(store[bemfa.KEY_THRESH]))

  /* --- 7.8 合法下发：请求形状 + 本机保存 --- */
  page.onSlide({ currentTarget: { dataset: { k: 'tHi' } }, detail: { value: 25 } })
  page.onSlide({ currentTarget: { dataset: { k: 'tLo' } }, detail: { value: 5 } })
  const wantMsg = bemfa.buildConfigMsg(page.data.T)
  const n0 = state.reqs.length
  state.httpHandler = (opt) =>
    /sendMessage/.test(opt.url) ? { statusCode: 200, data: { code: 0, msg: 'success' } }
                                : cloudOk(RAW.frame(26.4, 58, 21, null, null, RAW.cv))
  page.onSend()
  await sleep(30)

  ok(state.reqs.length === n0 + 1, '下发改动只发出 1 条请求', String(state.reqs.length - n0))
  const sent = state.reqs[state.reqs.length - 1]
  ok(/apis\.bemfa\.com\/va\/sendMessage$/.test(sent.url),
     '请求打到 /va/sendMessage（与取数同一主机，不需要第二条白名单）', sent.url)
  ok(sent.data.topic === 'temp001/set', 'topic = temp001/set', String(sent.data.topic))
  ok(sent.data.msg === wantMsg, 'msg 与界面预览的一致', String(sent.data.msg))
  ok(sent.data.uid && sent.data.type === 1, 'uid 与 type=1 都带上了', JSON.stringify(sent.data.uid ? 'uid ok' : 'uid missing'))
  ok(sent.data.msg.length < 191, '下发报文 ' + sent.data.msg.length + ' 字节 < 191', '')

  const saved = bemfa.loadThresh()
  ok(saved.tHi === 25 && saved.tLo === 5,
     '推送成功后本机保存了这组门限（本机那份 = "希望设备生效的门限"）',
     JSON.stringify(saved))
  ok(page.data.lastSend && page.data.lastSend.ok === true,
     '界面报告推送成功', JSON.stringify(page.data.lastSend && page.data.lastSend.ok))

  /* --- 7.9 推送失败：本机绝不能保存 --- */
  const beforeFail = JSON.stringify(bemfa.loadThresh())
  page.onSlide({ currentTarget: { dataset: { k: 'gas' } }, detail: { value: 33 } })
  state.httpHandler = () => ({ fail: true, errMsg: 'request:fail url not in domain list' })
  page.onSend()
  await sleep(30)
  ok(JSON.stringify(bemfa.loadThresh()) === beforeFail,
     '推送失败时本机门限【不变】—— 否则本机判读会永久性地与设备不一致',
     JSON.stringify(bemfa.loadThresh()))
  ok(page.data.lastSend && page.data.lastSend.ok === false, '界面如实报告推送失败', '')
  ok(/url not in domain list/.test(page.data.lastSend.text),
     '失败原因里带上原始 errMsg（一手证据，不被推测覆盖）', page.data.lastSend.text)

  /* --- 7.10 指纹三态 --- */
  ok(page.cmpCv(RAW.cv, RAW.cv).level === 'ok', '指纹相等 → ok', '')
  ok(page.cmpCv(1, RAW.cv).level === 'warn', '指纹不等 → warn', '')
  ok(page.cmpCv(RAW.cv, null).level === 'info', '设备没报指纹（老固件）→ info', '')
  ok(/固件较旧/.test(page.cmpCv(RAW.cv, null).text),
     '老固件时明说"无法回读"，不谎报成未同步', page.cmpCv(RAW.cv, null).text)

  /* --- 7.11 恢复默认值：只改界面，不悄悄把本机那份也换掉 --- */
  const savedBeforeReset = JSON.stringify(bemfa.loadThresh())
  state.modalConfirm = true
  page.onReset()
  await sleep(20)
  ok(page.data.T.tHi === bemfa.T_DEF.tHi && page.data.T.gas === bemfa.T_DEF.gas,
     '恢复默认把界面填回出厂值', JSON.stringify({ tHi: page.data.T.tHi, gas: page.data.T.gas }))
  ok(JSON.stringify(bemfa.loadThresh()) === savedBeforeReset,
     '恢复默认【只改界面】，本机已保存的那份要等重新下发才变（避免本机说一套、设备做一套）',
     JSON.stringify(bemfa.loadThresh()) + ' vs ' + savedBeforeReset)
  ok(page.data.cmdMsg === bemfa.buildConfigMsg(bemfa.T_DEF),
     '恢复默认后报文预览变回出厂默认的报文', page.data.cmdMsg)

  /* 收尾：没保存的那份留着影响后面的用例，清掉 */
  return state
}

/* ======================================================================
 * 8. 首页 / 趋势页接入本机门限
 * ====================================================================== */

async function testPagesFollowSavedThresh() {
  head('8. 实时页与趋势页跟随【本机保存的门限】')

  const state = installEnv()
  global.getApp = function () {
    return { getCfg: () => ({ uid: 'u'.repeat(32), topic: 'temp001', channel: 'http', intervalSec: 5, keepPoints: 240 }) }
  }

  /* --- 8.1 出厂默认下，26.4℃ 属于正常 --- */
  Object.keys(store).forEach(k => delete store[k])
  bemfa.resetThresh()

  let cfg = loadPage(state, 'pages/index/index.js')
  let page = makePage(cfg)
  page.onLoad()

  const frame26 = bemfa.parseTelemetry(RAW.frame(26.4, 58.0, 21, null, null, RAW.cv))
  page.applyReading(frame26, Math.floor(Date.now() / 1000), 'http', Math.floor(Date.now() / 1000))
  ok(page.data.v.tempCls === 'v-ok', '默认 40℃ 高温门限下，26.4℃ 判为正常', page.data.v.tempCls)
  ok(page.data.tips.length === 0, '默认门限下没有预警项', JSON.stringify(page.data.tips))
  ok(page.data.cvText.indexOf('已生效') >= 0,
     '设备指纹与本机一致 → 显示已生效', page.data.cvText)

  /* --- 8.2 把高温门限设成 25 之后（本机保存 = 已下发），同一帧数据要变成报警 --- */
  bemfa.saveThresh(Object.assign({}, bemfa.T_DEF, { tHi: 25 }))
  page.applyReading(frame26, Math.floor(Date.now() / 1000), 'http', Math.floor(Date.now() / 1000))
  ok(page.data.v.tempCls === 'v-danger',
     '高温门限改成 25 后，26.4℃ 立刻判为危险（界面跟着本机门限走）', page.data.v.tempCls)
  ok(page.data.tips.length >= 1 && /温度/.test(page.data.tips[0].text),
     '预警列表里出现温度项', JSON.stringify(page.data.tips))
  ok(page.data.v.tempNote.indexOf('25') >= 0,
     '提示文案里写的是新门限 25，不是写死的 40', page.data.v.tempNote)
  /* 【2026-09-22 措辞修订】原来这里的断言是"必须出现「未同步」"。
   * 但用户明确提出的用法是「我不需要板子报警, 只用小程序告警就行」——
   * 这时本机门限与设备门限【本来就该不同】, 那是他要的结果, 不是故障。
   * 所以 cvText 不再写死"未同步/请重新下发"(那会把用户推去反复点下发,
   * 反而改了设备门限、违背本意), 改为把两种可能都讲清楚。
   * 断言也跟着从"措辞必须含未同步"改成"必须同时给出两个指纹值 + 两条出路",
   * 这样既能挡住"退化成含糊一句话", 也不会再把正常用法判成错。 */
  ok(/48791/.test(page.data.cvText) && /38972/.test(page.data.cvText),
     '设备与本机指纹不同 → 两个值都如实报出来（48791 / 38972）', page.data.cvText)
  ok(page.data.cvText.indexOf('仅存本机') >= 0 && page.data.cvText.indexOf('保存并下发设备') >= 0,
     '两种可能都说清楚：可能是有意的「仅存本机」，也可能真要下发（不再武断让人重发）',
     page.data.cvText)
  ok(page.data.cvText.indexOf('未同步') < 0,
     '不再出现「未同步」这种武断措辞（会把"只让小程序告警"的正常用法误报成故障）',
     page.data.cvText)

  /* --- 8.3 湿度门限：此前没有判据，现在要能判 --- */
  bemfa.saveThresh(Object.assign({}, bemfa.T_DEF, { hHi: 50 }))
  page.applyReading(frame26, Math.floor(Date.now() / 1000), 'http', Math.floor(Date.now() / 1000))
  ok(page.data.v.humiCls === 'v-warn',
     '高湿门限 50、实际 58 → 判为警告', page.data.v.humiCls)
  ok(/50/.test(page.data.v.humiNote), '湿度提示里带上门限值', page.data.v.humiNote)

  /* --- 8.4 心率 / 气体也走同一套 --- */
  bemfa.saveThresh(Object.assign({}, bemfa.T_DEF, { hrLo: 70, hrHi: 90, gas: 10 }))
  const frameHR = bemfa.parseTelemetry(RAW.frame(26.4, 58, 21, 130, 95, RAW.cv))
  page.applyReading(frameHR, Math.floor(Date.now() / 1000), 'http', Math.floor(Date.now() / 1000))
  ok(page.data.v.hrCls === 'v-warn', '心率 130 超出 70~90 → 警告', page.data.v.hrCls)
  ok(page.data.v.gasCls === 'v-danger', '气体 21 超过门限 10 → 危险', page.data.v.gasCls)
  const tipKeys = page.data.tips.map(t => t.key)
  ok(tipKeys.indexOf('hr') >= 0 && tipKeys.indexOf('gas') >= 0,
     '预警列表同时包含心率与气体', tipKeys.join(','))

  /* --- 8.5 不在线配置范围内的项仍按编译期常量 ---
   * 2026-09-22 起"不在线配置范围"的只剩【撞击门限】一项（光照门限已随
   * 功能整体删除, 见 docs/39）。这一节的关键是"造出一帧只有读对常量才成立
   * 的读数"：
   *   合加速度 3.0g → 按 THRESH.accelHigh = 1.8 判为"剧烈撞击"；若还用着
   *                  早期的 2.5 或本机那份 99，就会判成正常。 */
  bemfa.saveThresh(Object.assign({}, bemfa.T_DEF, { accelHigh: 99 }))
  const frameAcc = bemfa.parseTelemetry(
    RAW.frame(26.4, 58, 21, null, null, RAW.cv, { ax: 3.0, ay: 0, az: 0 }))
  ok(Math.abs(frameAcc.accel - 3.0) < 1e-9,
     '这一帧的读数确实取到了合加速度 3.0g', String(frameAcc.accel))
  page.applyReading(frameAcc, Math.floor(Date.now() / 1000), 'http', Math.floor(Date.now() / 1000))
  ok(page.data.v.accCls === 'v-danger',
     '合加速度 3.0g > 常量门限 1.8g → 剧烈撞击（不是 2.5，也不是 99）',
     page.data.v.accCls)
  ok(bemfa.loadThresh().accelHigh === undefined,
     '本机保存的那份里【不含】accelHigh（这一项刻意不在下发范围内）',
     String(bemfa.loadThresh().accelHigh))
  ok(bemfa.loadThresh().luxLow === undefined && bemfa.loadThresh().luxHigh === undefined,
     '本机保存的那份里也不再有任何 lux 门限（docs/39 已整条删除）',
     String(bemfa.loadThresh().luxLow) + ' / ' + String(bemfa.loadThresh().luxHigh))

  /* --- 8.6 趋势页的阈值线跟着本机门限 --- */
  bemfa.saveThresh(Object.assign({}, bemfa.T_DEF, { tHi: 28, tLo: 15, gas: 42, hrHi: 100, hrLo: 45, sp: 92 }))
  cfg = loadPage(state, 'pages/trend/trend.js')
  page = makePage(cfg)
  page.points = [
    { unix: 1, seq: 1, temp: 26.4, humi: 58, gas: 21, hr: null, spo2: null, accel: 1.0, st: 'normal' }
  ]

  const inputTemp = page.buildInput({ key: 'temp', keys: ['temp'] }, 300, 200)
  const tv = inputTemp.thresholds.map(t => t.v).sort((a, b) => a - b)
  ok(tv.join(',') === '15,28', '温度视图的阈值线来自本机门限 (15 / 28)', tv.join(','))

  const inputGas = page.buildInput({ key: 'gas', keys: ['gas'] }, 300, 200)
  ok(inputGas.thresholds.length === 1 && inputGas.thresholds[0].v === 42,
     '气体视图的阈值线 42', JSON.stringify(inputGas.thresholds))

  const inputHr = page.buildInput({ key: 'hr', keys: ['hr'] }, 300, 200)
  const hv = inputHr.thresholds.map(t => t.v).sort((a, b) => a - b)
  ok(hv.join(',') === '45,100', '心率视图的阈值线 45 / 100', hv.join(','))

  const inputHumi = page.buildInput({ key: 'humi', keys: ['humi'] }, 300, 200)
  ok(inputHumi.thresholds.length === 2,
     '湿度视图现在也有阈值线了（此前没有）', JSON.stringify(inputHumi.thresholds))

  /* 双指标视图不画阈值线（两根线的量纲不同，画了会误导） */
  const inputBoth = page.buildInput({ key: 'temp_humi', keys: ['temp', 'humi'] }, 300, 200)
  ok(inputBoth.thresholds.length === 0, '温湿度双轴视图不画阈值线', '')

  /* 不在线配置的那一项仍然有阈值线，且取的是编译期常量
     （光照视图 2026-09-22 已整条删除, 见 docs/39） */
  const inputAcc = page.buildInput({ key: 'accel', keys: ['accel'] }, 300, 200)
  ok(inputAcc.thresholds.length === 1 &&
     inputAcc.thresholds[0].v === bemfa.THRESH.accelHigh,
     '撞击阈值线取 THRESH.accelHigh = ' + bemfa.THRESH.accelHigh, JSON.stringify(inputAcc.thresholds))

  return state
}

/* ======================================================================
 * 9. app.json 注册
 * ====================================================================== */

function testRouting() {
  head('9. 页面注册与 tabBar')

  const appJson = JSON.parse(fs.readFileSync(path.join(MP, 'app.json'), 'utf8'))
  ok(appJson.pages.indexOf('pages/settings/settings') >= 0,
     'app.json 的 pages 里注册了 pages/settings/settings', appJson.pages.join(','))
  const bar = (appJson.tabBar && appJson.tabBar.list) || []
  ok(bar.length === 3 && bar[2].pagePath === 'pages/settings/settings',
     'tabBar 第三项指向阈值设置页', JSON.stringify(bar.map(b => b.pagePath)))
  /* 页面四件套都得在磁盘上（预检脚本也查，但这里一起钉住，免得两个脚本只跑一个） */
  ;['js', 'wxml', 'wxss', 'json'].forEach(function (ext) {
    ok(fs.existsSync(path.join(MP, 'pages', 'settings', 'settings.' + ext)),
       'settings.' + ext + ' 存在', '')
  })
  /* 首页必须能从界面跳到设置页（只有 tabBar 没有入口也算能用，但入口更明确） */
  const idxWxml = fs.readFileSync(path.join(MP, 'pages', 'index', 'index.wxml'), 'utf8')
  ok(/onOpenSettings/.test(idxWxml), '实时页上有进入阈值设置页的按钮', '')
  ok(/阈值预警/.test(idxWxml), '实时页上有阈值预警横幅', '')
  /* 预警横幅绑定的是 tips[].text —— 曾经写成 {{item}}，那会把对象渲染成
   * "[object Object]"。当 checkThresholds 从"字符串数组"改成"对象数组"时
   * 很容易漏改这一处。 */
  ok(!/\{\{\s*item\s*\}\}/.test(idxWxml.replace(/<!--[\s\S]*?-->/g, '')),
     '实时页的预警列表渲染 item.text（不是裸 item）', '')

  /* 设置页上那些"开关型"字段必须真的有对照的控件 */
  const setWxml = fs.readFileSync(path.join(MP, 'pages', 'settings', 'settings.wxml'), 'utf8')
  ok(/bindchange="onBuzChange"/.test(setWxml), '设置页上的蜂鸣开关绑定了 onBuzChange', '')
  ok(/bindchange="onSlide"/.test(setWxml) && /bindtap="onStep"/.test(setWxml),
     '滑块与加减按钮都绑定了处理函数', '')
  /* 【注意这里为什么先赋给常量】直接写
   *     ok(...)
   *     ['onSend', ...].forEach(...)
   * 会被 JS 的自动分号插入（ASI）解析成 `ok(...)['onSend',...]` ——
   * 上一行的返回值被当成被索引的对象。写测试脚本时踩过一次。 */
  const handlers = ['onSend', 'onReset', 'onUseCur', 'onRefreshCur', 'onCopyMsg', 'onCopyTopic']
  handlers.forEach(function (h) {
    ok(new RegExp('bind\\w+="' + h + '"').test(setWxml),
       '设置页有 ' + h + ' 的入口', '')
  })
}

/* ======================================================================
 * 10. 「仅存本机」—— 只让小程序告警，不下发设备
 *
 * 【为什么单独立一节】用户的原话是"我不需要让板子报警, 只用小程序告警就行"。
 * 这条路径【不经过设备】, 一旦被后人删掉或改回"必须下发才保存",
 * 前面的第 7/8 节照样全绿 —— 因为那些用例走的都是下发链路。
 * 所以必须单独把"滑块改了 → 点仅存本机 → 本机生效且设备门限纹丝不动"钉死。
 * ====================================================================== */

async function testSaveLocalOnly() {
  head('10. 仅存本机（只让小程序告警，不下发设备）')

  const state = installEnv()
  global.getApp = function () {
    return { getCfg: () => ({ uid: 'u'.repeat(32), topic: 'temp001', channel: 'http', intervalSec: 5 }) }
  }

  Object.keys(store).forEach(k => delete store[k])
  bemfa.resetThresh()

  const cfg = loadPage(state, 'pages/settings/settings.js')
  const page = makePage(cfg)
  page.onLoad()

  /* 设备一直在报出厂默认指纹 38972（即：设备门限没被动过） */
  state.httpHandler = (opt) =>
    /sendMessage/.test(opt.url) ? { statusCode: 200, data: { code: 0, msg: 'success' } }
                                : cloudOk(RAW.frame(26.4, 58, 21, null, null, RAW.cv))
  page.onShow()
  await sleep(30)

  /* --- 10.1 拖滑块只改界面，本机 storage 不动 → dirty 必须亮起来 --- */
  const before = JSON.stringify(bemfa.loadThresh())
  page.onSlide({ currentTarget: { dataset: { k: 'tHi' } }, detail: { value: 25 } })
  ok(page.data.T.tHi === 25, '拖滑块后界面门限变成 25', String(page.data.T.tHi))
  ok(JSON.stringify(bemfa.loadThresh()) === before,
     '拖滑块【不】落本机（避免"改了一半"被当成已生效）', JSON.stringify(bemfa.loadThresh()))
  ok(page.data.dirty === true,
     '界面立刻标出"门限还没保存"（这就是"调了阈值却不告警"的现场）', String(page.data.dirty))

  /* --- 10.2 点「仅存本机」：本机生效，但一条下发请求都不许发出去 --- */
  const n0 = state.reqs.length
  page.onSaveLocal()
  await sleep(30)

  ok(state.reqs.length === n0,
     '「仅存本机」【一条请求都不发】—— 绝不碰设备',
     '多发了 ' + (state.reqs.length - n0) + ' 条')
  const saved = bemfa.loadThresh()
  ok(saved.tHi === 25, '本机已保存新门限 25（实时页会按它判读）', JSON.stringify(saved))
  ok(page.data.dirty === false, '保存后 dirty 灭掉', String(page.data.dirty))
  ok(state.toasts.indexOf('已存到本机') >= 0,
     '给出"已存到本机"的提示（必须与"已下发设备"能区分开）', JSON.stringify(state.toasts))

  /* --- 10.3 设备门限纹丝不动：回读上来的指纹仍是出厂默认 --- */
  page.onRefreshCur()
  await sleep(30)
  ok(page.data.cvDev === RAW.cv,
     '设备 cv 仍是 38972（设备侧门限完全没变，板子不会因此报警）', String(page.data.cvDev))
  ok(page.data.cv.level === 'warn' && page.data.cv.text.indexOf('仅存本机') >= 0,
     '两边不同时提示里带上「仅存本机」，不会把人推去反复点下发', page.data.cv.text)

  /* --- 10.4 不合法的门限【不许】存进本机（整条拒绝） --- */
  const beforeBad = JSON.stringify(bemfa.loadThresh())
  page.onSlide({ currentTarget: { dataset: { k: 'gas' } }, detail: { value: 0 } })
  state.modals.length = 0
  page.onSaveLocal()
  await sleep(30)
  ok(JSON.stringify(bemfa.loadThresh()) === beforeBad,
     '越界门限（gas=0，下限是 1）被整条拒绝，本机那份不变', JSON.stringify(bemfa.loadThresh()))
  ok(state.modals.length === 1, '并且弹框说明为什么不能存', String(state.modals.length))
}

/* ======================================================================
 * 主流程
 * ====================================================================== */

async function main() {
  say('='.repeat(72))
  say('阈值在线配置 / 小程序侧 静态验证')
  say('='.repeat(72))

  await testSettingsPage()
  await testPagesFollowSavedThresh()
  testRouting()
  await testSaveLocalOnly()

  head('汇总')
  say('  通过 ' + pass + ' 项，失败 ' + fail + ' 项')
  if (fail === 0) {
    say('')
    say('  结论：小程序与固件在门限、量程、指纹、报文四个层面完全对齐。')
    say('  仍只能在设备上验的部分：巴法云的 /set 推送是否真的到达、蜂鸣器响不响。')
  } else {
    say('')
    say('  有失败项。若失败集中在第 1/2 节，多半是固件改了门限而 bemfa.js')
    say('  的镜像常量没跟着改 —— 那会让"设备在响、小程序说正常"。')
  }

  /* 结论落盘（见 say 的说明）。写失败也不能把"断言全过"这个结论改掉，
   * 所以用 try 包住，退出码只看 fail。 */
  try {
    fs.writeFileSync(path.join(HERE, 'wxmp_thresh_test_result.txt'),
                     OUT.join('\n') + '\n', 'utf8')
  } catch (e) {
    try { console.error('结果文件写入失败: ' + e.message) } catch (e2) {}
  }

  process.exit(fail ? 1 : 0)
}

main().catch(function (e) {
  console.error('脚本自身出错: ' + (e && e.stack ? e.stack : e))
  process.exit(2)
})
