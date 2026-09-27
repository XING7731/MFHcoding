#!/usr/bin/env node
/* =========================================================================
 * wxmp_chart_preview.js —— 把折线图的【绘图计划】渲成 SVG/HTML，用来肉眼看
 *
 * 【为什么需要它】
 *   wxmp_chart_test.js 能证明"坐标与数据一致"，但证明不了"看起来对不对"：
 *   线条是不是糊在一起、刻度会不会挤、断口明不明显，这些只能用眼睛看。
 *   而真机效果要进微信开发者工具才能看 —— 那一步不在命令行里。
 *
 *   这个脚本用**同一份 buildPlan()** 的输出，换一个"后端"（SVG 而不是 canvas）
 *   渲出来。所以它不是"画个示意图给你看"，而是：
 *   计划错了，预览就一定跟着错 —— 它和真机走的是同一套坐标。
 *
 * 【不能证明什么】canvas 的实际绘制细节（字体渲染、抗锯齿、devicePixelRatio）
 *   只有真机能证明。这里只是把"计划"可视化。
 *
 * 用法: node tools/wxmp_chart_preview.js
 * 输出: _chart_preview.html（工程根目录，_ 开头，同步脚本会跳过）
 * ========================================================================= */

const fs = require('fs')
const path = require('path')

const MP = 'C:/Users/27362/Desktop/WeChat smart helmet'
const chart = require(path.join(MP, 'utils', 'chart.js'))
const P = chart.PALETTE

const W = 320          /* 与小程序里 canvas 的 CSS 尺寸同量级 */
const H = 150

/* ---------------------------------------------------------------- 数据 */

const NOW = Math.floor(Date.now() / 1000)

function series(n, fn) {
  const out = []
  for (let i = 0; i < n; i++) out.push(fn(i))
  return out
}
/* 可重复的伪随机（不用 Math.random，保证每次预览一样，便于前后对比） */
function wob(i, amp) {
  return Math.sin(i * 1.7) * amp * 0.6 + Math.sin(i * 0.37) * amp * 0.4
}

function labels(n) {
  const out = []
  for (let i = 0; i < n; i++) {
    const t = new Date((NOW - (n - 1 - i) * 10) * 1000)
    const p2 = function (x) { return (x < 10 ? '0' : '') + x }
    out.push(p2(t.getHours()) + ':' + p2(t.getMinutes()) + ':' + p2(t.getSeconds()))
  }
  return out
}

/* ① 温湿度 40 分钟：温度缓慢爬升 + 湿度缓慢下降。用来看双轴与长序列 */
const N1 = 240
const TEMP = series(N1, function (i) { return Number((26 + i * 0.016 + wob(i, 0.25)).toFixed(1)) })
const HUMI = series(N1, function (i) { return Number((58 - i * 0.03 + wob(i + 5, 1.2)).toFixed(1)) })

/* ② 心率：三段缺测 + 后段顶到 120 以上。用来看断口与门限线 */
const HR_RAW = [78, 80, 82, 79, null, null, null, null, null, 88, 92, 96, 99,
  null, null, 104, 108, 112, 116, 121, 125, 128, 126, 122]
const HR = []
for (let i = 0; i < 120; i++) HR.push(HR_RAW[i % HR_RAW.length])

/* ③ 气体：平时 18~25，中途一个尖峰冲到 66（越 50 门限）。用来看面积填充与门限线。
 * 取 60 个点，正好卡在页面 DOTS_MAX 的边界上 —— 这样"点少时画圆点"这条规则
 * 在预览里真的被演示到（超过 60 点就不画点了，圆点会糊成一条粗线）。 */
const GAS = series(60, function (i) {
  /* 三角尖峰：在 i=25.5 处达到峰值 (29-22)/2 × 13 ≈ 45.5，加上基线 20 ≈ 66 */
  const spike = (i >= 22 && i <= 28) ? (i - 22) * 13 : 0
  const spike2 = (i >= 23 && i <= 29) ? (29 - i) * 13 : 0
  const v = 20 + wob(i, 3) + Math.min(spike, spike2)
  return Number(v.toFixed(0))
})

/* ④ 血氧：全缺测（未佩戴）。用来看"不给假数据"这一条的实际样子 */
const SPO2 = series(120, function () { return null })

/* ---------------------------------------------------------- 绘图计划 */

function plan1() {
  return chart.buildPlan({
    width: W, height: H, count: N1, hasRightAxis: true,
    series: [
      { key: 'temp', name: '温度', color: P.temp, axis: 0, values: TEMP, showDots: false },
      { key: 'humi', name: '湿度', color: P.humi, axis: 1, values: HUMI, showDots: false }
    ],
    labels: labels(N1),
    thresholds: []
  })
}
function planHR() {
  return chart.buildPlan({
    width: W, height: H, count: HR.length,
    series: [{ key: 'hr', name: '心率', color: P.hr, axis: 0, values: HR, showDots: false }],
    labels: labels(HR.length),
    thresholds: [{ v: 120, label: '上限 120' }, { v: 50, label: '下限 50' }]
  })
}
function planGas() {
  return chart.buildPlan({
    width: W, height: H, count: GAS.length,
    series: [{ key: 'gas', name: '气体', color: P.gas, axis: 0, values: GAS, showDots: true }],
    labels: labels(GAS.length),
    thresholds: [{ v: 50, label: '报警 50' }]
  })
}
function planSpo2() {
  return chart.buildPlan({
    width: W, height: H, count: SPO2.length,
    series: [{ key: 'spo2', name: '血氧', color: P.spo2, axis: 0, values: SPO2, showDots: false }],
    labels: labels(SPO2.length),
    thresholds: [{ v: 90, label: '偏低 90' }]
  })
}

/* --------------------------------------------------------- SVG 后端 */

function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}
function anchor(a) {
  if (a === 'right') return 'end'
  if (a === 'center') return 'middle'
  return 'start'
}

/* 与 chart.drawPlan 一一对应的另一个后端 */
function toSvg(plan) {
  const out = []
  plan.items.forEach(function (it) {
    if (it.t === 'line') {
      const pts = it.pts.map(function (p) { return p[0].toFixed(1) + ',' + p[1].toFixed(1) }).join(' ')
      out.push('<polyline points="' + pts + '" fill="none" stroke="' + it.color +
        '" stroke-width="' + (it.width || 1) + '"' +
        (it.dash ? ' stroke-dasharray="' + it.dash.join(' ') + '"' : '') +
        ' stroke-linejoin="round" stroke-linecap="round"/>')
    } else if (it.t === 'area') {
      const pts = it.pts.map(function (p) { return p[0].toFixed(1) + ',' + p[1].toFixed(1) }).join(' ')
      out.push('<polygon points="' + pts + '" fill="' + it.color + '" stroke="none"/>')
    } else if (it.t === 'dot') {
      out.push('<circle cx="' + it.x.toFixed(1) + '" cy="' + it.y.toFixed(1) +
        '" r="' + (it.r || 2) + '" fill="' + it.color + '"/>')
    } else if (it.t === 'text') {
      out.push('<text x="' + it.x.toFixed(1) + '" y="' + it.y.toFixed(1) +
        '" fill="' + it.color + '" font-size="' + (it.size || 10) +
        '" font-family="-apple-system,PingFang SC,sans-serif" text-anchor="' + anchor(it.align) +
        '" dominant-baseline="central">' + esc(it.s) + '</text>')
    }
  })
  return out.join('\n')
}

function statsCard(name, color, values, unit, digits) {
  const vals = values.filter(function (v) { return v !== null })
  if (!vals.length) {
    return '<div class="st"><span class="dot" style="background:' + color + '"></span><b>' +
      name + '</b><span class="n">全部缺测（%d 个点）</span></div>'.replace('%d', values.length)
  }
  let mx = vals[0], mn = vals[0], sum = 0
  vals.forEach(function (v) { if (v > mx) mx = v; if (v < mn) mn = v; sum += v })
  const f = function (v) { return v.toFixed(digits) }
  return '<div class="st"><span class="dot" style="background:' + color + '"></span><b>' + name +
    '</b><span class="kv">最新 ' + f(vals[vals.length - 1]) + ' ' + unit +
    ' · 最高 ' + f(mx) + ' · 最低 ' + f(mn) + ' · 平均 ' + f(sum / vals.length) +
    ' · ' + vals.length + '/' + values.length + ' 个有效</span></div>'
}

function card(title, sub, plan, statHtml) {
  return '<div class="card">\n' +
    '<div class="title">' + title + '</div>\n' +
    '<div class="sub">' + sub + '</div>\n' +
    '<svg viewBox="0 0 ' + plan.width + ' ' + plan.height +
    '" width="' + Math.round(plan.width * 1.5) + '" height="' + Math.round(plan.height * 1.5) +
    '" xmlns="http://www.w3.org/2000/svg" style="background:#fff">\n' +
    toSvg(plan) + '\n</svg>\n' +
    statHtml + '\n</div>'
}

const p1 = plan1(), p2 = planHR(), p3 = planGas(), p4 = planSpo2()

const html = '<!DOCTYPE html>\n<html lang="zh-CN"><head><meta charset="utf-8">\n' +
'<title>智能安全帽 · 小程序趋势图预览</title>\n' +
'<style>\n' +
'body{margin:0;padding:24px;background:#f4f6fa;color:#1b2a3d;' +
'font-family:-apple-system,"PingFang SC","Helvetica Neue",sans-serif;font-size:14px;line-height:1.6}\n' +
'h2{font-size:15px;font-weight:500;margin:0 0 4px}\n' +
'.note{color:#7a879b;font-size:13px;margin:0 0 20px}\n' +
'.card{background:#fff;border:1px solid #e6ebf2;border-radius:18px;padding:16px 18px;margin-bottom:16px;max-width:520px}\n' +
'.title{font-size:15px;font-weight:500;margin-bottom:2px}\n' +
'.sub{color:#7a879b;font-size:12px;margin-bottom:10px}\n' +
'.st{display:flex;align-items:baseline;gap:8px;margin-top:10px;padding-top:10px;border-top:1px solid #e6ebf2;flex-wrap:wrap}\n' +
'.dot{width:8px;height:8px;border-radius:50%;display:inline-block;flex:none;align-self:center}\n' +
'.kv,.n{color:#7a879b;font-size:12px}\n' +
'</style></head><body>\n' +
'<h2>趋势页四种图形的实际绘图计划（由 utils/chart.js 的 buildPlan 输出，换 SVG 后端渲染）</h2>\n' +
'<p class="note">下面每一张图用的都是小程序里那份同一坐标计算。计划错了，这里就会跟着错。</p>\n' +

card('温湿度（双 Y 轴）', '240 个点 ≈ 40 分钟。左轴温度、右轴湿度，两种量级分开算刻度；两根线时不画面积填充。',
  p1,
  statsCard('温度', P.temp, TEMP, '℃', 1) + statsCard('湿度', P.humi, HUMI, '%RH', 1)) +

card('心率（含缺测断口 + 门限线）', '5+2 段没测到 -> 折线在那里断开（不是连成 0）。后段顶到 121~128，120 的上限门限线因此出现在图上。',
  p2,
  statsCard('心率', P.hr, HR, 'bpm', 0)) +

card('气体 MQ-2（单指标 + 尖峰）', '单指标图带面积填充；虚线是 50 的报警门限，尖峰冲到 66 越过了它。',
  p3,
  statsCard('气体', P.gas, GAS, '', 0)) +

card('血氧（全缺测）', '未佩戴时 120 个点全是 null：一条线都不画，统计显示"全部缺测"。这就是"不给假数据"的实际样子。',
  p4,
  statsCard('血氧', P.spo2, SPO2, '%', 0)) +

'</body></html>\n'

const out = path.join(__dirname, '..', '_chart_preview.html')
fs.writeFileSync(out, html, 'utf8')
console.log('written: ' + out)
console.log('charts: 温湿度 ' + p1.items.length + ' 条原语 / 心率 ' + p2.items.length +
            ' / 气体 ' + p3.items.length + ' / 血氧 ' + p4.items.length)
