/* =========================================================================
 * chart.js —— 折线图：纯计算 + 绘制回放（不依赖任何 wx API）
 *
 * 【为什么要把"图表"拆成两半】
 *   canvas 画出来的像素没法在本机断言（这台机器上没有微信开发者工具的命令行）。
 *   所以拆成：
 *     buildPlan(cfg)      纯数据：把数据折成【一串绘图原语】，形如
 *                         { t:'line', pts:[[x,y],…], color:'#d85a30', width:1.6 }
 *     drawPlan(ctx, plan) 只有十几行 switch，逐条把这些原语落到 ctx 上
 *   于是真正会算错的东西 —— 折线遇空值要断开、刻度范围必须包住数据、
 *   双 Y 轴各自的换算、孤点不能丢 —— 全部变成了"对着一串数字做断言"，
 *   可以在 Node 里逐项验证（见 tools/wxmp_chart_test.js）。
 *
 * 【本图对"缺测"的立场】
 *   心率/血氧没测到时是 null，不是 0。折线必须在 null 处【断开】，
 *   绝不能连过去 —— 连过去就等于告诉人"这时候心率是 0"，
 *   在一个报警系统里这是不能接受的谎。断点逻辑在 buildPlan 里，
 *   并且有专门的断言守着（测试 A5/A6）。
 * ========================================================================= */

/* 指标配色：与 app.wxss 的状态色同源，不另起一套色板 */
const PALETTE = {
  temp:  '#d85a30',   /* 温度：暖色 */
  humi:  '#2f6ea8',   /* 湿度：主色蓝 */
  gas:   '#a9740c',   /* 气体：赭 */
  hr:    '#e5484d',   /* 心率：红（与 --danger 同色） */
  spo2:  '#7f77dd',   /* 血氧：紫 */
  accel: '#10b981'    /* 加速度：绿（与 --ok 同色） */
}

const C_GRID   = '#e6ebf2'
const C_AXIS   = '#d8e0ea'
const C_TEXT   = '#9aa5b5'
const C_THRESH = '#ef9f0f'

/* 绘图区留白。左边固定给刻度数字，右边有第二根轴时也留出来 */
const PAD_TOP    = 14
const PAD_BOTTOM = 24
const PAD_LEFT   = 42
const PAD_RIGHT_AXIS  = 42
const PAD_RIGHT_PLAIN = 14

const MAX_TICKS = 10

/* ---------------------------------------------------------------- 小工具 */

function isNum(v) { return typeof v === 'number' && isFinite(v) }

/* '#rrggbb' -> 'rgba(r,g,b,a)'；识别不了就原样返回（宁可没填充，不要画错色） */
function withAlpha(hex, a) {
  const m = /^#([0-9a-fA-F]{6})$/.exec(String(hex || ''))
  if (!m) return hex
  const n = parseInt(m[1], 16)
  const al = (a === undefined) ? 0.14 : a
  return 'rgba(' + ((n >> 16) & 255) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + al + ')'
}

/* ---------------------------------------------------------- 1. 取数列 */

/**
 * 从趋势点数组里取某个字段，得到和一个点一一对应的数组。
 * 【关键】保持长度与索引一致 —— 折线的 x 是按索引算的，所以缺测处必须占位为
 * null 而不是"跳过"，否则缺测后面的点会整体左移，时间轴就对不上了。
 */
function extractSeries(points, key) {
  const pts = points || []
  const out = []
  for (let i = 0; i < pts.length; i++) {
    const p = pts[i] || {}
    const v = p[key]
    if (v === null || v === undefined || v === '') { out.push(null); continue }
    const n = Number(v)
    out.push(isFinite(n) ? n : null)
  }
  return out
}

/* ---------------------------------------------------------- 2. 坐标轴 */

/* 一个数需要几位小数才能被 step 整除（用于刻度标签，避免 0.30000000000000004） */
function decimalsOf(step) {
  if (!(step > 0) || !isFinite(step)) return 2
  for (let d = 0; d <= 8; d++) {
    const s = step * Math.pow(10, d)
    if (Math.abs(s - Math.round(s)) < 1e-6) return d
  }
  return 8
}

/* 把原始步长收成 1 / 2 / 5 / 10 × 10^n —— 刻度数字才好看 */
function niceStep(raw) {
  if (!(raw > 0) || !isFinite(raw)) return 1
  const exp = Math.floor(Math.log10(raw))
  const pow = Math.pow(10, exp)
  const f = raw / pow
  let m
  if (f <= 1.5)      m = 1
  else if (f <= 3)   m = 2
  else if (f <= 7)   m = 5
  else               m = 10
  return m * pow
}

/**
 * 由数据的最小/最大值算出一段"包得住它们"的刻度。
 *
 * 【不变式，测试里逐项断言】
 *   · min <= 数据最小 且 max >= 数据最大（数据永远落在轴内，不会被裁掉）
 *   · min < max（全相等时也要撑开，否则除零 → 所有点 NaN → 整张图空白）
 *   · ticks 严格递增，个数在 3~MAX_TICKS 之间
 *
 * 【全相等】是真实会发生的：湿度稳定在 55.0 时 span=0。这里撑开 ±10%，
 * 让这条直线落在图中间，而不是贴着底边或顶边。
 */
function niceScale(dataMin, dataMax, targetTicks) {
  const want = targetTicks || 4
  if (!isNum(dataMin) || !isNum(dataMax)) return null

  let lo = Math.min(dataMin, dataMax)
  let hi = Math.max(dataMin, dataMax)
  if (hi - lo < 1e-9) {
    const pad = Math.max(Math.abs(hi) * 0.1, 0.5)
    lo -= pad
    hi += pad
  } else {
    /* 留 6% 余量：不留的话，当最小值恰好落在整步长上（比如温度正好 26.0），
     * 最低那个点会贴着底边轴线，线看着像被切掉了 */
    const pad = (hi - lo) * 0.06
    lo -= pad
    hi += pad
  }

  const step = niceStep((hi - lo) / want)
  const dec = decimalsOf(step)
  const min = Number((Math.floor(lo / step) * step).toFixed(dec))
  const max = Number((Math.ceil(hi / step) * step).toFixed(dec))

  const ticks = []
  for (let k = 0; k < MAX_TICKS + 2; k++) {
    const v = Number((min + k * step).toFixed(dec))
    if (v > max + step * 1e-6) break
    ticks.push(v)
  }
  return { min: min, max: max, step: step, dec: dec, ticks: ticks }
}

/* ---------------------------------------------------------- 3. 绘图计划 */

/* x 轴标签只取几个，避免糊成一片：首 / 中 / 末；点数很少时全标 */
function labelIndexes(count) {
  if (count <= 0) return []
  if (count <= 4) {
    const all = []
    for (let i = 0; i < count; i++) all.push(i)
    return all
  }
  return [0, Math.floor((count - 1) / 2), count - 1]
}

/**
 * 把数据折成一串绘图原语。
 *
 * cfg = {
 *   width, height,            画布 CSS 尺寸（px）
 *   count,                    点数
 *   series: [{ key, name, color, axis(0=左 1=右), values:[num|null,…], showDots }]
 *   thresholds: [{ v, label }]  仅单序列时画，且只在坐标范围内才画
 *   labels: ['10:02:28', …]     与点一一对应，用于 x 轴
 *   hasRightAxis               是否留出右侧刻度栏
 * }
 *
 * 返回 plan = {
 *   width, height,padLeft…, plotW, plotH,
 *   items: [绘图原语…],
 *   series: [{ key, name, color, valid, segs, scale }]   ← 给测试和排错用
 * }
 */
function buildPlan(cfg) {
  const c = cfg || {}
  const width  = Math.max(40, c.width || 300)
  const height = Math.max(40, c.height || 150)
  const list   = c.series || []
  const count  = c.count || 0

  const padLeft  = PAD_LEFT
  const padRight = c.hasRightAxis ? PAD_RIGHT_AXIS : PAD_RIGHT_PLAIN
  const plotW = Math.max(10, width - padLeft - padRight)
  const plotH = Math.max(10, height - PAD_TOP - PAD_BOTTOM)

  const plan = {
    width: width, height: height,
    padLeft: padLeft, padRight: padRight,
    padTop: PAD_TOP, padBot: PAD_BOTTOM,
    plotW: plotW, plotH: plotH,
    plotRight: padLeft + plotW,
    plotBottom: PAD_TOP + plotH,
    items: [],
    series: [],
    count: count
  }
  if (count < 1 || list.length === 0) return plan

  /* --- 每个轴各自算一套刻度：轴 0 = 左，轴 1 = 右 --- */
  const axisOf = function (s) { return (s.axis === 1) ? 1 : 0 }
  const scales = {}
  ;[0, 1].forEach(function (ax) {
    let mn = Infinity, mx = -Infinity
    list.forEach(function (s) {
      if (axisOf(s) !== ax) return
      const v = s.values || []
      for (let i = 0; i < v.length; i++) {
        const x = v[i]
        if (x === null || x === undefined) continue
        if (x < mn) mn = x
        if (x > mx) mx = x
      }
    })
    scales[ax] = (mn === Infinity) ? null : niceScale(mn, mx, 4)
  })

  const xAt = function (i) {
    if (count === 1) return padLeft + plotW / 2
    return padLeft + (i / (count - 1)) * plotW
  }
  const yAt = function (v, sc) {
    return PAD_TOP + (1 - (v - sc.min) / (sc.max - sc.min)) * plotH
  }
  const fmtTick = function (v, dec) { return Number(v).toFixed(dec) }

  /* --- ① 横向网格 + 左右刻度数字 --- */
  const leftScale = scales[0]
  if (leftScale) {
    leftScale.ticks.forEach(function (t) {
      const y = yAt(t, leftScale)
      plan.items.push({ t: 'line', pts: [[padLeft, y], [plan.plotRight, y]], color: C_GRID, width: 1 })
      plan.items.push({
        t: 'text', x: padLeft - 6, y: y, s: fmtTick(t, leftScale.dec),
        color: C_TEXT, size: 10, align: 'right', baseline: 'middle'
      })
    })
  }
  const rightScale = scales[1]
  if (rightScale) {
    /* 右轴用第二根序列的颜色，一眼能对上"这条线看右边" */
    const rColor = (list.filter(function (s) { return axisOf(s) === 1 })[0] || {}).color || C_TEXT
    rightScale.ticks.forEach(function (t) {
      plan.items.push({
        t: 'text', x: plan.plotRight + 6, y: yAt(t, rightScale), s: fmtTick(t, rightScale.dec),
        color: rColor, size: 10, align: 'left', baseline: 'middle'
      })
    })
  }

  /* --- ② 底边轴线 --- */
  plan.items.push({
    t: 'line', pts: [[padLeft, plan.plotBottom], [plan.plotRight, plan.plotBottom]],
    color: C_AXIS, width: 1
  })

  /* --- ③ 阈值线（只有单序列时画：双序列说不清它属于哪根轴） --- */
  const ths = (list.length === 1) ? (c.thresholds || []) : []
  if (leftScale) {
    ths.forEach(function (th) {
      const v = Number(th && th.v)
      if (!isFinite(v)) return
      /* 不在显示范围内就不画 —— 为了画一条 40℃ 的线把纵轴从 26~30 拉到 0~40，
       * 曲线会被压成一条直线，那是得不偿失的 */
      if (v < leftScale.min || v > leftScale.max) return
      const y = yAt(v, leftScale)
      plan.items.push({
        t: 'line', pts: [[padLeft, y], [plan.plotRight, y]],
        color: C_THRESH, width: 1, dash: [4, 3]
      })
      plan.items.push({
        t: 'text', x: plan.plotRight - 2, y: Math.max(8, y - 7), s: th.label,
        color: C_THRESH, size: 9, align: 'right', baseline: 'middle'
      })
    })
  }

  /* --- ④ 数据线：遇 null 断开，孤点画成圆点 --- */
  const onlyOne = (list.length === 1)
  list.forEach(function (s) {
    const ax = axisOf(s)
    const sc = scales[ax]
    const color = s.color || '#1f4e79'
    if (!sc) {
      /* 整条序列都是 null（血氧未佩戴就是这种）：没有线可画，但要在 plan.series
       * 里留一条记录 —— 排错时才看得出"这条被算过且为空"，
       * 而不是"代码根本没处理它"。测试 C5 就是靠这条区分的。 */
      plan.series.push({
        key: s.key, name: s.name, color: color, axis: ax,
        valid: 0, segs: 0, scale: null
      })
      return
    }
    const vals = s.values || []

    const segs = []
    let cur = []
    let valid = 0
    for (let i = 0; i < count; i++) {
      const v = (i < vals.length) ? vals[i] : null
      if (v === null || v === undefined) {
        if (cur.length) { segs.push(cur); cur = [] }
        continue
      }
      valid++
      cur.push([xAt(i), yAt(v, sc)])
    }
    if (cur.length) segs.push(cur)

    segs.forEach(function (seg) {
      if (seg.length >= 2) {
        if (onlyOne) {
          /* 面积垫在线的下面。多序列时不画 —— 两块半透明叠在一起会糊成第三种颜色 */
          const areaPts = seg.concat([
            [seg[seg.length - 1][0], plan.plotBottom],
            [seg[0][0], plan.plotBottom]
          ])
          plan.items.push({ t: 'area', pts: areaPts, color: withAlpha(color) })
        }
        plan.items.push({ t: 'line', pts: seg, color: color, width: 1.6 })
        if (s.showDots) {
          seg.forEach(function (p) { plan.items.push({ t: 'dot', x: p[0], y: p[1], r: 1.8, color: color }) })
        }
      } else if (seg.length === 1) {
        /* 孤点：前后都被缺测夹住的一个有效值。画不出线，但必须让人看见这个点 */
        plan.items.push({ t: 'dot', x: seg[0][0], y: seg[0][1], r: 2.6, color: color })
      }
    })

    plan.series.push({
      key: s.key, name: s.name, color: color,
      axis: ax, valid: valid, segs: segs.length,
      scale: { min: sc.min, max: sc.max, ticks: sc.ticks.length }
    })
  })

  /* --- ⑤ x 轴时间标签（最后画，压在所有东西上面） --- */
  const labels = c.labels || []
  const idxs = labelIndexes(count)
  idxs.forEach(function (i, k) {
    const s = labels[i]
    if (s === undefined || s === null || s === '') return
    let align = 'center'
    let x = xAt(i)
    if (k === 0 && idxs.length > 1) { align = 'left'; x = padLeft }
    if (k === idxs.length - 1 && idxs.length > 1) { align = 'right'; x = plan.plotRight }
    plan.items.push({
      t: 'text', x: x, y: plan.plotBottom + 12, s: String(s),
      color: C_TEXT, size: 10, align: align, baseline: 'middle'
    })
  })

  return plan
}

/* ---------------------------------------------------------- 4. 落地绘制 */

/**
 * 把 plan 逐条落到 canvas 2d 上。
 * 这里刻意不做任何计算 —— 一旦这里开始算坐标，测试就管不到它了。
 * 返回落地的原语条数（排错时看它是否为 0，就知道画布到底画没画）。
 */
function drawPlan(ctx, plan) {
  if (!ctx || !plan || !plan.items) return 0
  ctx.clearRect(0, 0, plan.width, plan.height)

  let n = 0
  for (let i = 0; i < plan.items.length; i++) {
    const it = plan.items[i]
    const t = it.t

    if (t === 'line') {
      const p = it.pts
      if (!p || p.length < 2) continue
      ctx.beginPath()
      ctx.moveTo(p[0][0], p[0][1])
      for (let k = 1; k < p.length; k++) ctx.lineTo(p[k][0], p[k][1])
      ctx.strokeStyle = it.color
      ctx.lineWidth = it.width || 1
      if (it.dash && ctx.setLineDash) ctx.setLineDash(it.dash)
      ctx.stroke()
      if (it.dash && ctx.setLineDash) ctx.setLineDash([])   /* 用完复位，别影响后面的线 */
      n++

    } else if (t === 'area') {
      const p = it.pts
      if (!p || p.length < 3) continue
      ctx.beginPath()
      ctx.moveTo(p[0][0], p[0][1])
      for (let k = 1; k < p.length; k++) ctx.lineTo(p[k][0], p[k][1])
      ctx.closePath()
      ctx.fillStyle = it.color
      ctx.fill()
      n++

    } else if (t === 'dot') {
      ctx.beginPath()
      ctx.arc(it.x, it.y, it.r || 2, 0, Math.PI * 2)
      ctx.fillStyle = it.color
      ctx.fill()
      n++

    } else if (t === 'text') {
      ctx.font = (it.weight ? (it.weight + ' ') : '') + (it.size || 10) + 'px sans-serif'
      ctx.fillStyle = it.color
      ctx.textAlign = it.align || 'left'
      ctx.textBaseline = it.baseline || 'middle'
      ctx.fillText(String(it.s), it.x, it.y)
      n++
    }
  }
  return n
}

module.exports = {
  PALETTE: PALETTE,
  C_GRID: C_GRID,
  C_TEXT: C_TEXT,
  withAlpha: withAlpha,
  extractSeries: extractSeries,
  niceStep: niceStep,
  decimalsOf: decimalsOf,
  niceScale: niceScale,
  labelIndexes: labelIndexes,
  buildPlan: buildPlan,
  drawPlan: drawPlan
}
