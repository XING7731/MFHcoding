// pages/trend/trend.js —— 趋势图（canvas 折线）与报警记录
//
// 数据来源是【本机缓存】，不是云端：
//   巴法云的 HTTP 接口只给"该主题最新一条"（实测 num>1 会返回 data:null），
//   所以历史只能靠小程序自己在轮询到新报文时攒下来（见 utils/bemfa.js 的 recordPoint）。
//   好处是不依赖平台历史接口；代价是只能看到"打开过小程序的这段时间"。
//
// 【这一版把 CSS 柱子换成了 canvas 折线图】
//   换的原因：柱状图只能看"大小"，看不出"走势"和"哪一段断了"。
//   看护类数据真正要看的是趋势（温度是不是在往上爬、气体有没有一个尖峰），
//   而且心率的缺测段必须以【断开】的形式出现 —— 见 utils/chart.js 顶部的说明。
//
// 【画布什么时候能画】<canvas type="2d"> 的节点要先上屏才查得到。本页的图表
//   外面套了 wx:if="{{!empty}}"，所以"数据从无到有"的那一次 setData 之后必须
//   等一帧（wx.nextTick）再去取节点，否则 select 会什么都查不到、图一片空白 ——
//   这是 canvas 最容易踩且最难查的一个坑，故写在这里。

const bemfa = require('../../utils/bemfa.js')
const chart = require('../../utils/chart.js')

const CANVAS_ID = 'trend-canvas'

/* 每个指标的展示参数。
 * 【阈值线为什么不写在这张表里】温湿度 / 心率 / 血氧 / 气体的门限是用户可以在
 * 「阈值设置」页在线改的（存在本机、并下发到设备）。若在这里写死，图上那条线
 * 就会和设备真正用的门限对不上 —— 看图的人会得出"明明没超线为什么在响"这种
 * 错误结论。所以阈值线统一由 thresholdsFor() 按【本机保存的门限】现算。
 * 例外：撞击不可在线配置（撞击门限属于摔倒判据，刻意不给小程序改），
 * 它仍按编译期常量画。 */
const METRICS = {
  temp:  { name: '温度',      unit: '℃',   digits: 1, color: chart.PALETTE.temp },
  humi:  { name: '湿度',      unit: '%RH', digits: 1, color: chart.PALETTE.humi },
  gas:   { name: '气体 MQ-2', unit: '',    digits: 0, color: chart.PALETTE.gas },
  hr:    { name: '心率',      unit: 'bpm', digits: 0, color: chart.PALETTE.hr },
  spo2:  { name: '血氧',      unit: '%',   digits: 0, color: chart.PALETTE.spo2 },
  accel: { name: '合加速度',  unit: 'g',   digits: 2, color: chart.PALETTE.accel }
}

/* 按【本机保存的门限】现算阈值线。返回 { 指标key: [{v, label}, ...] }。
 * 注意这条线画的是"你设置的门限"，不是"设备当前生效的门限" —— 两者不一致时
 * 以实数监测页底部的 cv 指纹核对为准（那里会明说未同步）。 */
function thresholdsFor() {
  const T  = bemfa.loadThresh()
  const TH = bemfa.THRESH
  return {
    temp: [
      { v: T.tHi, label: '高温 ' + T.tHi },
      { v: T.tLo, label: '低温 ' + T.tLo }
    ],
    humi: [
      { v: T.hHi, label: '高湿 ' + T.hHi },
      { v: T.hLo, label: '低湿 ' + T.hLo }
    ],
    gas:  [{ v: T.gas,  label: '报警 ' + T.gas }],
    hr:   [{ v: T.hrHi, label: '上限 ' + T.hrHi },
           { v: T.hrLo, label: '下限 ' + T.hrLo }],
    spo2: [{ v: T.sp,   label: '偏低 ' + T.sp }],
    accel: [{ v: TH.accelHigh, label: '撞击 ' + TH.accelHigh }]
  }
}

/* 可选视图。"温湿度"两根线一个图，是使用频率最高的一个组合 */
const VIEWS = [
  { key: 'temp_humi', name: '温湿度', keys: ['temp', 'humi'] },
  { key: 'temp',      name: '温度',   keys: ['temp'] },
  { key: 'humi',      name: '湿度',   keys: ['humi'] },
  { key: 'gas',       name: '气体',   keys: ['gas'] },
  { key: 'hr',        name: '心率',   keys: ['hr'] },
  { key: 'spo2',      name: '血氧',   keys: ['spo2'] },
  { key: 'accel',     name: '加速度', keys: ['accel'] }
]

/* 点太密时就不画每个数据点了（240 个点画圆点只会糊成一条粗线） */
const DOTS_MAX = 60

function p2(n) { return (n < 10 ? '0' : '') + n }

function fmtClock(unix) {
  if (!unix) return ''
  const d = new Date(unix * 1000)
  return p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds())
}

function fmtDateTime(unix) {
  if (!unix) return '--'
  const d = new Date(unix * 1000)
  return p2(d.getMonth() + 1) + '-' + p2(d.getDate()) + ' ' +
         p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds())
}

/* 一列数的统计量（缺测已被滤掉）。没有有效值时全部返回 '--' */
function summarize(pts, key, digits) {
  const vals = []
  for (let i = 0; i < pts.length; i++) {
    const raw = (pts[i] || {})[key]
    if (raw === null || raw === undefined || raw === '') continue
    const n = Number(raw)
    if (!isNaN(n)) vals.push(n)
  }
  if (!vals.length) {
    return { valid: 0, last: '--', max: '--', min: '--', avg: '--' }
  }
  let mx = vals[0], mn = vals[0], sum = 0
  for (let i = 0; i < vals.length; i++) {
    if (vals[i] > mx) mx = vals[i]
    if (vals[i] < mn) mn = vals[i]
    sum += vals[i]
  }
  return {
    valid: vals.length,
    last: vals[vals.length - 1].toFixed(digits),
    max: mx.toFixed(digits),
    min: mn.toFixed(digits),
    avg: (sum / vals.length).toFixed(digits)
  }
}

Page({
  data: {
    views: VIEWS,
    activeKey: 'temp_humi',
    activeName: '温湿度',

    legend: [],
    count: 0,
    fromTime: '--',
    toTime: '--',
    empty: true,
    drawNote: '',

    alarms: [],
    alarmCount: 0
  },

  onShow() {
    this.reload()
  },

  onPullDownRefresh() {
    this.reload()
    wx.stopPullDownRefresh()
  },

  reload() {
    this.points = bemfa.loadPoints() || []

    /* 最新在前，并把云端 unix 预格式化成可读时间（wxml 里不做计算） */
    const alarms = (bemfa.loadAlarms() || []).slice().reverse().map(function (a) {
      return {
        unix: a.unix,
        time: fmtDateTime(a.unix),
        seq: a.seq,
        st: a.st,
        cn: a.cn,
        level: a.level
      }
    })

    const pts = this.points
    this.setData({
      empty: pts.length === 0,
      alarms: alarms,
      alarmCount: alarms.length,
      count: pts.length,
      fromTime: pts.length ? (fmtClock(pts[0].unix) || '--') : '--',
      toTime: pts.length ? (fmtClock(pts[pts.length - 1].unix) || '--') : '--'
    })

    this.refresh()
  },

  onPickMetric(e) {
    const key = e.currentTarget.dataset.key
    const v = VIEWS.filter(x => x.key === key)[0]
    if (!v) return
    this.setData({ activeKey: key, activeName: v.name })
    this.refresh()
  },

  activeView() {
    const key = this.data.activeKey
    return VIEWS.filter(x => x.key === key)[0] || VIEWS[0]
  },

  /* 算统计值 -> 上屏 -> 下一帧画图 */
  refresh() {
    const view = this.activeView()
    const pts = this.points || []

    const legend = view.keys.map(function (k) {
      const m = METRICS[k]
      const s = summarize(pts, k, m.digits)
      return {
        key: k, name: m.name, unit: m.unit, color: m.color,
        valid: s.valid, last: s.last, max: s.max, min: s.min, avg: s.avg
      }
    })

    this.setData({ legend: legend })

    /* wx.nextTick：等这次 setData 的视图更新落地，canvas 节点才查得到 */
    const self = this
    const go = function () { self.drawChart(view) }
    if (wx.nextTick) wx.nextTick(go)
    else setTimeout(go, 0)
  },

  /* 组装 buildPlan 的输入：把每个指标折成一条序列 */
  buildInput(view, width, height) {
    const pts = this.points || []
    const multi = view.keys.length > 1

    const series = view.keys.map(function (k, idx) {
      const m = METRICS[k]
      return {
        key: k,
        name: m.name,
        unit: m.unit,
        color: m.color,
        /* 双序列时第二根走右轴：温度 26~30 和湿度 45~65 量级差太多，
         * 共用一根轴会把温度压成一条平线 */
        axis: (multi && idx === 1) ? 1 : 0,
        values: chart.extractSeries(pts, k),
        showDots: pts.length <= DOTS_MAX
      }
    })

    const labels = pts.map(function (p, i) {
      return fmtClock(p.unix) || ('#' + (i + 1))
    })

    return {
      width: width,
      height: height,
      count: pts.length,
      series: series,
      labels: labels,
      hasRightAxis: multi,
      /* 阈值线只在单指标视图里画，且由 chart.js 判断是否落在坐标范围内。
       * 取的是 thresholdsFor() 现算的那一份（= 本机保存的门限）。 */
      thresholds: multi ? [] : (thresholdsFor()[view.keys[0]] || [])
    }
  },

  drawChart(view) {
    const pts = this.points || []
    if (!pts.length) return

    const self = this
    let q = wx.createSelectorQuery()
    if (q.in) q = q.in(this)

    q.select('#' + CANVAS_ID).fields({ node: true, size: true }).exec(function (res) {
      const box = res && res[0]
      /* 查不到节点是【正常】的：空状态时 canvas 还没有上屏。
       * 这里静默返回，不弹错 —— 统计值仍然是可读的，用户不会失去信息。 */
      if (!box || !box.node || !box.width || !box.height) {
        self.setData({ drawNote: '画布尚未就绪（下拉可重试）' })
        return
      }

      const canvas = box.node
      const ctx = canvas.getContext('2d')
      if (!ctx) {
        self.setData({ drawNote: '当前环境不支持 canvas 2d，仅显示统计值' })
        return
      }

      /* 高清屏：按设备像素比放大背板再整体缩放，否则线条发虚 */
      let dpr = 2
      if (wx.getWindowInfo) dpr = wx.getWindowInfo().pixelRatio || 2
      else if (wx.getSystemInfoSync) dpr = wx.getSystemInfoSync().pixelRatio || 2

      canvas.width = Math.round(box.width * dpr)
      canvas.height = Math.round(box.height * dpr)
      if (ctx.scale) ctx.scale(dpr, dpr)

      const plan = chart.buildPlan(self.buildInput(view, box.width, box.height))
      const drawn = chart.drawPlan(ctx, plan)

      /* 留一份计划在实例上：排错时能直接看出"算出来的是不是空的" */
      self.lastPlan = plan
      self.setData({ drawNote: drawn ? '' : '没有可画的数据点' })
    })
  },

  onClear() {
    const self = this
    wx.showModal({
      title: '清空本机记录',
      content: '会删掉本机缓存的趋势点和报警记录（不影响巴法云上的数据）。确定吗？',
      confirmColor: '#e5484d',
      success: (res) => {
        if (!res.confirm) return
        bemfa.clearRecords()
        self.points = []
        self.lastPlan = null
        self.setData({
          legend: [], alarms: [], alarmCount: 0, empty: true,
          count: 0, fromTime: '--', toTime: '--', drawNote: ''
        })
        wx.showToast({ title: '已清空', icon: 'success' })
      }
    })
  },

  onBack() {
    wx.switchTab({ url: '/pages/index/index' })
  }
})
