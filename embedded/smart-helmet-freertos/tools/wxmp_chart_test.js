#!/usr/bin/env node
/* =========================================================================
 * wxmp_chart_test.js —— 趋势折线图（utils/chart.js + pages/trend）的静态验证
 *
 * 【为什么需要它】
 *   这台机器上没有微信开发者工具的命令行，canvas 画出来的像素没法看。
 *   所以 chart.js 被刻意拆成两半（纯计算 / 落到 ctx），这个脚本分别验：
 *     A. 纯计算：刻度必须包住数据、折线遇空值必须断开、孤点不能丢、
 *                双 Y 轴不能共用一套刻度、阈值线出了范围不许画也不许拉轴。
 *     B. 落地：每条绘图原语都真的落到 ctx 上了吗？虚线用完复位了吗？
 *                （"buildPlan 发了一种 drawPlan 不认识的类型"是这类代码
 *                  最典型的哑巴 bug —— 图空白，却不报任何错。）
 *     C. 页面：用最小 wx 假环境 require 真实的 pages/trend/trend.js，
 *              喂真实形状的报文，验统计值与"画布确实被画了"。
 *
 * 【本脚本不验什么】像素好不好看、字体大不大、颜色配不配 —— 那些只能在
 *   开发者工具或真机上看。这里只保证"画出来的东西和数据是对得上的"。
 *
 * 运行: node wxmp_chart_test.js
 * ========================================================================= */

const path = require('path')

const MP = 'C:/Users/27362/Desktop/WeChat smart helmet'
const chart = require(path.join(MP, 'utils', 'chart.js'))
const P = chart.PALETTE

let pass = 0, fail = 0
function ok(cond, name, extra) {
  if (cond) { pass++; console.log('  ok   ' + name) }
  else { fail++; console.log('  FAIL ' + name + (extra ? '   -> ' + extra : '')) }
}
function head(t) {
  console.log('\n' + t)
  console.log('-'.repeat(Math.max(20, t.length)))
}

function itemsOf(plan, t, color) {
  return plan.items.filter(function (i) {
    if (i.t !== t) return false
    if (color && i.color !== color) return false
    return true
  })
}

/* 一条序列在图上的纵向跨距占绘图区多少比例。
 * 【这个量是"双轴是否真的分开算了"的关键判据】见 C3。 */
function ySpanPct(plan, color) {
  const ys = []
  itemsOf(plan, 'line', color).forEach(function (l) {
    l.pts.forEach(function (p) { ys.push(p[1]) })
  })
  if (!ys.length) return 0
  const lo = Math.min.apply(null, ys)
  const hi = Math.max.apply(null, ys)
  return (hi - lo) / plan.plotH
}

/* ---------------------------------------------------------------- 假 ctx */

function fakeCtx() {
  const calls = []
  function rec(name) {
    return function () {
      calls.push({ op: name, args: Array.prototype.slice.call(arguments) })
    }
  }
  return {
    calls: calls,
    clearRect: rec('clearRect'),
    beginPath: rec('beginPath'),
    closePath: rec('closePath'),
    moveTo: rec('moveTo'),
    lineTo: rec('lineTo'),
    stroke: rec('stroke'),
    fill: rec('fill'),
    arc: rec('arc'),
    fillText: rec('fillText'),
    scale: rec('scale'),
    setLineDash: rec('setLineDash'),
    strokeStyle: '', fillStyle: '', lineWidth: 1,
    font: '', textAlign: '', textBaseline: ''
  }
}
function callsOf(ctx, op) { return ctx.calls.filter(function (c) { return c.op === op }) }

/* ---------------------------------------------------------------- 数据 */

const NOW = Math.floor(Date.now() / 1000)

/* 与固件紧凑报文同形状的一个趋势点 */
function mkPoint(i, total, over) {
  const base = {
    unix: NOW - (total - 1 - i) * 10,
    seq: 100 + i,
    hr: null, spo2: null,
    temp: 26, humi: 55, gas: 20, accel: 3.9,
    st: 'normal'
  }
  return Object.assign(base, over || {})
}

/* 12 个点：温度 26.0→27.1、湿度 55.0→60.5、心率故意留三段缺口、血氧全缺 */
const HR_PAT = [80, 81, null, null, null, 90, 91, null, 88, 89, 90, 91]
const PTS = []
for (let i = 0; i < 12; i++) {
  PTS.push(mkPoint(i, 12, {
    temp: Number((26 + i * 0.1).toFixed(1)),
    humi: Number((55 + i * 0.5).toFixed(1)),
    hr: HR_PAT[i]
  }))
}

/* ------------------------------------------------ A. chart.js 纯计算 */

head('A. chart.js —— 取数列')

const ex = chart.extractSeries([
  { temp: 26.4 }, { temp: null }, { temp: '' }, { temp: 'abc' }, {}, { temp: 0 }, { temp: '-3.2' }
], 'temp')

ok(ex.length === 7, 'A1 取出来的数组与点数等长（缺测占位，不跳过）', String(ex.length))
ok(ex[0] === 26.4, 'A1 数字原样保留')
ok(ex[1] === null && ex[2] === null && ex[3] === null && ex[4] === null,
   'A1 null / 空串 / 非数字 / 字段缺失 一律记为空缺', JSON.stringify(ex))
ok(ex[5] === 0 && ex[6] === -3.2,
   '★ A1 值 0 不能被当成空缺（Number(null)===0 是这个坑的来源）', JSON.stringify(ex))

head('A. chart.js —— 纵轴刻度')

const RANGES = [
  [26.4, 28.4], [55, 55], [0, 0], [-5, 5], [0, 1000], [0.001, 0.002],
  [999, 1001], [12, 12.05], [3.9, 3.9], [100, 100], [-40, -10], [0.5, 0.6]
]
let badFit = 0, badOrder = 0, badCount = 0, badFirstLast = 0
RANGES.forEach(function (r) {
  const sc = chart.niceScale(r[0], r[1], 4)
  if (!sc) { badFit++; return }
  const lo = Math.min(r[0], r[1]), hi = Math.max(r[0], r[1])
  /* ① 必须包住数据：不然最高的点会被画出图外 */
  if (!(sc.min <= lo + 1e-9 && sc.max >= hi - 1e-9)) badFit++
  /* ② 必须撑得开：全相等时若不撑开，就会 y=(v-min)/(max-min) 除零 → 整张图空白 */
  if (!(sc.max > sc.min)) badOrder++
  /* ③ 刻度数量别太离谱（挤成一团或只剩两根都没法看） */
  if (!(sc.ticks.length >= 3 && sc.ticks.length <= 10)) badCount++
  /* ④ 首末刻度就是轴的边界 */
  if (!(sc.ticks[0] === sc.min && sc.ticks[sc.ticks.length - 1] === sc.max)) badFirstLast++
})
ok(badFit === 0, 'A2 ' + RANGES.length + ' 组范围，刻度都包得住数据（含负数/小数/全相等）', badFit + ' 组不合格')
ok(badOrder === 0, 'A2 每组都 max > min（全相等时撑开，不会除零）')
ok(badCount === 0, 'A2 刻度数都在 3~10 之间')
ok(badFirstLast === 0, 'A2 首末刻度等于轴边界')

let inc = true
const one = chart.niceScale(26.4, 28.4, 4)
for (let i = 1; i < one.ticks.length; i++) if (!(one.ticks[i] > one.ticks[i - 1])) inc = false
ok(inc, 'A2 刻度严格递增（不会出现重复或回退）', JSON.stringify(one.ticks))

const flat = chart.niceScale(55, 55, 4)
ok(flat && isFinite(flat.min) && isFinite(flat.max) && flat.ticks.every(isFinite),
   '★ A3 湿度稳定在 55.0（方差为 0）时，刻度有限且可用', JSON.stringify(flat))
ok(flat.min < 55 && flat.max > 55, 'A3 这条平线落在轴的中间，而不是贴着底边')

ok(chart.decimalsOf(0.5) === 1 && chart.decimalsOf(2) === 0 && chart.decimalsOf(0.2) === 1,
   'A2 步长小数位推算正确（0.5→1 位、2→0 位）')
ok(chart.niceStep(0.275) === 0.2 && chart.niceStep(2.5) === 2 && chart.niceStep(7.5) === 10,
   'A2 步长收成 1/2/5/10 的整数倍')

head('A. chart.js —— 折线遇缺测必须断开')

const pBreak = chart.buildPlan({
  width: 300, height: 150, count: 6,
  series: [{ key: 'hr', color: P.hr, axis: 0, values: [26, 27, null, null, 25, 24] }],
  labels: ['a', 'b', 'c', 'd', 'e', 'f']
})
ok(pBreak.series[0].segs === 2, 'A5 中间两个缺测把折线切成 2 段', String(pBreak.series[0].segs))
const segs = itemsOf(pBreak, 'line', P.hr)
ok(segs.length === 2, 'A5 画出来就是 2 条折线段', String(segs.length))
ok(segs[0].pts.length === 2 && segs[1].pts.length === 2,
   '★ A5 每段各 2 个点 —— 缺测处没有被补一个点连过去',
   segs[0].pts.length + ' / ' + segs[1].pts.length)
ok(Math.max(segs[0].pts[0][0], segs[0].pts[1][0]) < Math.min(segs[1].pts[0][0], segs[1].pts[1][0]),
   'A5 两段在 x 上不重叠，断口是真的断开了')

const pLone = chart.buildPlan({
  width: 300, height: 150, count: 3,
  series: [{ key: 'hr', color: P.hr, axis: 0, values: [null, 27, null] }],
  labels: ['a', 'b', 'c']
})
ok(pLone.series[0].valid === 1 && pLone.series[0].segs === 1, 'A6 前后都缺测的一个孤点仍算 1 个有效值')
ok(itemsOf(pLone, 'line', P.hr).length === 0, 'A6 孤点连不成线，不是 1 个点的线')
ok(itemsOf(pLone, 'dot', P.hr).length === 1, '★ A6 孤点画成圆点（否则这个真实测量值就凭空消失了）')

head('A. chart.js —— 几何与双轴')

const pGeo = chart.buildPlan({
  width: 300, height: 150, count: 5,
  series: [{ key: 't', color: P.temp, axis: 0, values: [20, 22.5, 25, 27.5, 30] }],
  labels: ['a', 'b', 'c', 'd', 'e']
})
const geoLine = itemsOf(pGeo, 'line', P.temp)[0]
ok(geoLine.pts[0][0] === pGeo.padLeft, 'A9 第一个点在左边界上', String(geoLine.pts[0][0]))
ok(geoLine.pts[4][0] === pGeo.plotRight, 'A9 最后一个点在右边界上', String(geoLine.pts[4][0]))
const allY = geoLine.pts.map(function (p) { return p[1] })
ok(Math.min.apply(null, allY) >= pGeo.padTop - 1e-6 &&
   Math.max.apply(null, allY) <= pGeo.plotBottom + 1e-6,
   'A9 所有点都落在绘图区内（不会被画到轴外面）',
   Math.min.apply(null, allY).toFixed(1) + '~' + Math.max.apply(null, allY).toFixed(1))
ok(geoLine.pts[0][1] > geoLine.pts[4][1], 'A9 值越大 y 越小（纵轴方向没搞反）')

const p1 = chart.buildPlan({
  width: 300, height: 150, count: 1,
  series: [{ key: 't', color: P.temp, axis: 0, values: [26] }],
  labels: ['a']
})
const lone = itemsOf(p1, 'dot', P.temp)[0]
ok(lone && Math.abs(lone.x - (p1.padLeft + p1.plotW / 2)) < 1e-6,
   'A10 只有 1 个点时画在水平中间（而不是贴左边）')

const p2 = chart.buildPlan({
  width: 300, height: 150, count: 12,
  hasRightAxis: true,
  series: [
    { key: 'temp', name: '温度', color: P.temp, axis: 0, values: PTS.map(function (p) { return p.temp }) },
    { key: 'humi', name: '湿度', color: P.humi, axis: 1, values: PTS.map(function (p) { return p.humi }) }
  ],
  labels: PTS.map(function (p, i) { return 't' + i })
})
ok(p2.series.length === 2 && p2.series[0].axis === 0 && p2.series[1].axis === 1,
   'A7 双序列各占一根轴')
ok(itemsOf(p2, 'area').length === 0, 'A7 两根线时不画面积（两块半透明叠起来会糊）')
ok(itemsOf(p2, 'line', P.temp).length === 1 && itemsOf(p2, 'line', P.humi).length === 1,
   'A7 两根线都画了')
const rightTexts = p2.items.filter(function (i) {
  return i.t === 'text' && i.align === 'left' && i.x > p2.plotRight
})
ok(rightTexts.length === p2.series[1].scale.ticks,
   'A7 右侧刻度数字的个数与右轴刻度数一致', String(rightTexts.length))
ok(rightTexts.length > 0 && rightTexts[0].color === P.humi,
   '★ A7 右轴刻度用的是湿度那根线的颜色（否则人不知道该读哪边）')

head('A. chart.js —— 阈值线')

const pThOut = chart.buildPlan({
  width: 300, height: 150, count: 5,
  series: [{ key: 't', color: P.temp, axis: 0, values: [26, 26.5, 27, 26.8, 27.1] }],
  thresholds: [{ v: 40, label: '高温 40' }, { v: 10, label: '低温 10' }],
  labels: ['a', 'b', 'c', 'd', 'e']
})
ok(itemsOf(pThOut, 'line').filter(function (i) { return i.dash }).length === 0,
   'A8 温度才 26~27，40℃/10℃ 的门限在显示范围外 -> 一条都不画')
ok(pThOut.series[0].scale.max < 40,
   '★ A8 也没有为了让门限露脸而把纵轴拉到 40（那样曲线会被压成一条平线）',
   String(pThOut.series[0].scale.max))

const pThIn = chart.buildPlan({
  width: 300, height: 150, count: 5,
  series: [{ key: 't', color: P.temp, axis: 0, values: [39, 39.5, 40, 41, 42] }],
  thresholds: [{ v: 40, label: '高温 40' }, { v: 10, label: '低温 10' }],
  labels: ['a', 'b', 'c', 'd', 'e']
})
ok(itemsOf(pThIn, 'line').filter(function (i) { return i.dash }).length === 1,
   '★ A8 温度顶到 42 时，40℃ 门限线画出来了（一条，10℃ 那条仍在范围外）')

const pThMulti = chart.buildPlan({
  width: 300, height: 150, count: 12, hasRightAxis: true,
  series: [
    { key: 'temp', color: P.temp, axis: 0, values: PTS.map(function (p) { return p.temp }) },
    { key: 'humi', color: P.humi, axis: 1, values: PTS.map(function (p) { return p.humi }) }
  ],
  thresholds: [{ v: 40, label: '高温 40' }],
  labels: PTS.map(function (p, i) { return 't' + i })
})
ok(itemsOf(pThMulti, 'line').filter(function (i) { return i.dash }).length === 0,
   'A8 双序列视图不画门限线（说不清它属于哪根轴）')

head('A. chart.js —— x 轴标签与空输入')

ok(JSON.stringify(chart.labelIndexes(1)) === '[0]', 'A11 1 个点 -> 1 个标签')
ok(JSON.stringify(chart.labelIndexes(3)) === '[0,1,2]', 'A11 3 个点 -> 全标')
ok(JSON.stringify(chart.labelIndexes(12)) === '[0,5,11]', 'A11 12 个点 -> 首/中/末')
const xl = p2.items.filter(function (i) {
  return i.t === 'text' && Math.abs(i.y - (p2.plotBottom + 12)) < 1e-6
})
ok(xl.length === 3, 'A11 图上真的画了 3 个时间标签', String(xl.length))

const pEmpty = chart.buildPlan({})
ok(pEmpty.items.length === 0 && pEmpty.series.length === 0, 'A12 完全没有数据 -> 空的绘图计划，不报错')
const pAllNull = chart.buildPlan({
  width: 300, height: 150, count: 12,
  series: [{ key: 'spo2', name: '血氧', color: P.spo2, axis: 0, values: PTS.map(function () { return null }) }]
})
ok(pAllNull.series.length === 1 && pAllNull.series[0].valid === 0 && pAllNull.series[0].scale === null,
   '★ A12 整条序列全缺测时仍留一条记录（排错时能区分"算过且为空"与"没处理"）')
ok(itemsOf(pAllNull, 'line', P.spo2).length === 0, 'A12 全缺测不画线')

ok(chart.withAlpha('#d85a30') === 'rgba(216,90,48,0.14)', 'A13 面积填充色换算正确', chart.withAlpha('#d85a30'))
ok(chart.withAlpha('rgb(1,2,3)') === 'rgb(1,2,3)', 'A13 认不出的颜色原样返回（宁可没填充，不要画错色）')

/* ------------------------------------------------ B. drawPlan 落地 */

head('B. drawPlan —— 原语是否真的落到 ctx 上')

const KINDS = ['line', 'area', 'dot', 'text']
ok(p2.items.every(function (i) { return KINDS.indexOf(i.t) >= 0 }) &&
   pThIn.items.every(function (i) { return KINDS.indexOf(i.t) >= 0 }),
   '★ B0 buildPlan 发出的原语类型都在 drawPlan 支持的集合里（否则会静默画不出来）')

const ctx2 = fakeCtx()
const n2 = chart.drawPlan(ctx2, p2)
ok(n2 === p2.items.length, 'B1 每条原语都落地了', n2 + '/' + p2.items.length)
ok(callsOf(ctx2, 'clearRect').length === 1, 'B1 绘制前先清屏（否则上一帧的线会留在画布上）')
ok(callsOf(ctx2, 'stroke').length === itemsOf(p2, 'line').length,
   'B1 stroke 次数 = 折线段数', callsOf(ctx2, 'stroke').length + ' vs ' + itemsOf(p2, 'line').length)
ok(callsOf(ctx2, 'fillText').length === itemsOf(p2, 'text').length,
   'B1 fillText 次数 = 文本数')
ok(callsOf(ctx2, 'arc').length === itemsOf(p2, 'dot').length,
   'B1 arc 次数 = 圆点数')

const ctx3 = fakeCtx()
const n3 = chart.drawPlan(ctx3, pThIn)
ok(n3 === pThIn.items.length, 'B2 带门限线的图同样全部落地')
const dashes = callsOf(ctx3, 'setLineDash')
ok(dashes.length >= 2, 'B2 虚线设置过（门限线要用虚线，和实测数据区分开）', String(dashes.length))
ok(dashes[0].args[0].join(',') === '4,3', 'B2 用了 4,3 的虚线段', JSON.stringify(dashes[0].args[0]))
ok(dashes[dashes.length - 1].args[0].length === 0,
   '★ B2 画完虚线复位成实线（不复位的话，后面所有线都变虚线）')

const ctx4 = fakeCtx()
ok(chart.drawPlan(ctx4, chart.buildPlan({})) === 0, 'B3 空计划 -> 落地 0 条，不报错')
ok(chart.drawPlan(null, p2) === 0 && chart.drawPlan(ctx4, null) === 0, 'B3 ctx/plan 为空时不抛异常')

/* ------------------------------------------------ C. 趋势页 */

head('C. pages/trend/trend.js —— 页面逻辑')

const store = {}
const state = {
  toasts: [],
  queryCount: 0,
  ctx: fakeCtx(),
  box: null
}
const canvasNode = {
  width: 0, height: 0,
  getContext: function (kind) { return state.ctx }
}
state.box = { node: canvasNode, width: 300, height: 150 }

global.wx = {
  setStorageSync: function (k, v) { store[k] = v },
  getStorageSync: function (k) { return store[k] === undefined ? '' : store[k] },
  removeStorageSync: function (k) { delete store[k] },
  showToast: function (o) { state.toasts.push(o.title) },
  showModal: function (o) { if (o.success) o.success({ confirm: true }) },
  switchTab: function () {},
  stopPullDownRefresh: function () {},
  nextTick: function (fn) { fn() },
  getWindowInfo: function () { return { pixelRatio: 2 } },
  createSelectorQuery: function () {
    const q = {
      in: function () { return q },
      select: function () { return q },
      fields: function () { return q },
      exec: function (cb) { state.queryCount++; cb([state.box]) }
    }
    return q
  }
}
global.Page = function (cfg) { state.pageCfg = cfg }

require(path.join(MP, 'pages', 'trend', 'trend.js'))

function makePage() {
  const inst = Object.assign({}, state.pageCfg)
  inst.data = JSON.parse(JSON.stringify(state.pageCfg.data))
  inst.setData = function (patch) { Object.assign(inst.data, patch) }
  return inst
}
function pick(page, key) {
  page.onPickMetric({ currentTarget: { dataset: { key: key } } })
}

/* --- C1 空缓存 --- */
const page = makePage()
state.queryCount = 0
page.onShow()
ok(page.data.empty === true, 'C1 没有缓存 -> 走空状态')
ok(page.data.legend.length === 2, 'C1 温湿度视图仍有 2 行图例可显示', String(page.data.legend.length))
ok(page.data.legend[0].last === '--' && page.data.legend[0].valid === 0,
   'C1 没有数据时显示 --（不是 0）')
ok(state.queryCount === 0, 'C1 空状态不去查画布节点（省掉一次无用的查询）')

/* --- C2 有数据：统计值 --- */
wx.setStorageSync('helmet_points', PTS)
wx.setStorageSync('helmet_last_seq', 111)
page.onShow()

ok(page.data.empty === false && page.data.count === 12, 'C2 12 个点读进来')
ok(page.data.legend.length === 2 && page.data.legend[0].name === '温度',
   'C2 默认视图是温湿度（两行图例）')
ok(page.data.legend[0].last === '27.1' && page.data.legend[0].max === '27.1' &&
   page.data.legend[0].min === '26.0',
   'C2 温度 最新/最高/最低 正确',
   page.data.legend[0].last + ' / ' + page.data.legend[0].max + ' / ' + page.data.legend[0].min)

let sumT = 0
PTS.forEach(function (p) { sumT += p.temp })
ok(page.data.legend[0].avg === (sumT / PTS.length).toFixed(1),
   'C2 温度平均值与逐点求平均一致', page.data.legend[0].avg)
ok(page.data.legend[1].min === '55.0' && page.data.legend[1].max === '60.5',
   'C2 湿度 最高/最低 正确',
   page.data.legend[1].min + '~' + page.data.legend[1].max)
ok(page.data.legend[0].valid === 12 && page.data.legend[1].valid === 12,
   'C2 温湿度各 12 个有效值')

/* --- C3 双轴：两根线各自的纵向跨距都要够大 --- */
ok(page.lastPlan && page.lastPlan.series.length === 2, 'C3 绘图计划里有 2 条序列')
ok(page.lastPlan.series[0].axis === 0 && page.lastPlan.series[1].axis === 1,
   'C3 温度走左轴、湿度走右轴')
const spanT = ySpanPct(page.lastPlan, P.temp)
const spanH = ySpanPct(page.lastPlan, P.humi)
ok(spanT > 0.4 && spanH > 0.4,
   '★ C3 两根线各自纵向都用掉了 40% 以上的高度（共用一套刻度的话都只有 10% 上下，会压成平线）',
   '温度 ' + (spanT * 100).toFixed(0) + '% / 湿度 ' + (spanH * 100).toFixed(0) + '%')
ok(itemsOf(page.lastPlan, 'area').length === 0, 'C3 双线视图不画面积')

/* --- C4 心率：大段缺测要断成多段 --- */
pick(page, 'hr')
ok(page.data.activeKey === 'hr' && page.data.legend.length === 1, 'C4 切到心率：只剩 1 行图例')
ok(page.data.legend[0].valid === 8, 'C4 心率 12 个点里 8 个有效', String(page.data.legend[0].valid))
ok(page.data.legend[0].last === '91' && page.data.legend[0].max === '91' &&
   page.data.legend[0].min === '80',
   'C4 心率统计值只统计有效点',
   page.data.legend[0].min + '~' + page.data.legend[0].max)
ok(page.lastPlan.series[0].segs === 3,
   '★ C4 缺测把心率折线切成 3 段（80-81 / 90-91 / 88-91）',
   String(page.lastPlan.series[0].segs))
ok(itemsOf(page.lastPlan, 'line').filter(function (i) { return i.dash }).length === 0,
   'C4 心率 80~91，120/50 的门限在显示范围外 -> 不画门限线')
ok(page.lastPlan.series[0].scale.max < 120,
   '★ C4 也没把纵轴拉到 120（否则 80~91 这段波动会被压平）',
   String(page.lastPlan.series[0].scale.max))

/* --- C5 血氧：全缺测 --- */
pick(page, 'spo2')
ok(page.data.legend[0].valid === 0 && page.data.legend[0].last === '--',
   'C5 血氧全缺测 -> 显示 --、有效值 0')
ok(page.lastPlan.series.length === 1 && page.lastPlan.series[0].valid === 0,
   'C5 计划里仍登记了血氧序列（valid=0），不是"没处理"')
ok(itemsOf(page.lastPlan, 'line', P.spo2).length === 0, 'C5 血氧一条线都不画')

/* --- C6 温度顶到门限附近：门限线要出现 --- */
const HIGH = []
for (let i = 0; i < 8; i++) {
  HIGH.push(mkPoint(i, 8, { temp: Number((39 + i * 0.4).toFixed(1)) }))
}
wx.setStorageSync('helmet_points', HIGH)
page.reload()
pick(page, 'temp')
ok(page.data.count === 8 && page.data.legend[0].max === '41.8',
   'C6 换一批数据后重新统计（不用重进页面）', page.data.legend[0].max)
ok(itemsOf(page.lastPlan, 'line').filter(function (i) { return i.dash }).length === 1,
   '★ C6 温度顶到 41.8 时，40℃ 门限线出现在图上')

/* --- C7 画布确实被画了 --- */
state.queryCount = 0
page.refresh()
ok(state.queryCount > 0, 'C7 每次都去查画布节点')
ok(canvasNode.width === 600 && canvasNode.height === 300,
   'C7 画布背板按 devicePixelRatio=2 放大（否则线条发虚）',
   canvasNode.width + 'x' + canvasNode.height)
ok(callsOf(state.ctx, 'scale').length > 0 && callsOf(state.ctx, 'scale')[0].args[0] === 2,
   'C7 绘制前按 2 倍缩放坐标系')
ok(callsOf(state.ctx, 'clearRect').length > 0, '★ C7 画布被真的绘制过（不是只算了计划不画）')
ok(callsOf(state.ctx, 'stroke').length > 0, 'C7 至少画了一条线或刻度')

/* --- C8 画布节点查不到时不能崩 --- */
const savedBox = state.box
state.box = null
page.refresh()
ok(true, 'C8 取不到画布节点时静默返回，不抛异常')
ok(/画布/.test(page.data.drawNote), 'C8 并在界面上说明画布尚未就绪：' + page.data.drawNote)
state.box = savedBox
page.refresh()

/* --- C9 清空 --- */
page.onClear()
ok(page.data.empty === true && page.data.legend.length === 0 && page.data.count === 0,
   'C9 清空后回到空状态')
ok(page.lastPlan === null && wx.getStorageSync('helmet_points') === '',
   'C9 缓存与绘图计划一并清掉')
ok(state.toasts.indexOf('已清空') >= 0, 'C9 有清空提示')

/* --- C10 缺 unix 的点也不能崩（时间轴标签退化成序号） --- */
const NOUNIX = [mkPoint(0, 3, { unix: 0 }), mkPoint(1, 3, { unix: 0 }), mkPoint(2, 3, { unix: 0 })]
wx.setStorageSync('helmet_points', NOUNIX)
page.reload()
ok(page.data.fromTime === '--' && page.data.toTime === '--', 'C10 没有云端时间就显示 --，不显示 1970 年')
const labels = (page.lastPlan && page.lastPlan.items.length) ? true : false
ok(labels, 'C10 没有时间戳时仍然画得出来（标签退化为 #1 #2 #3）')
ok(state && page.data.count === 3, 'C10 3 个点都在')

/* ---------------------------------------------------------------- 汇总 */

console.log('\n' + '='.repeat(72))
console.log('结果: ' + pass + ' 项通过, ' + fail + ' 项失败')
console.log('='.repeat(72))
process.exit(fail ? 1 : 0)
