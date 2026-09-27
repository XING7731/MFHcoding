// pages/settings/settings.js —— 阈值设置：改门限 → 下发到设备 → 回读指纹确认
//
// 【这一页解决什么】
//   原来温湿度/心率/血氧/气体的门限全是 user_config.h 里的编译期宏，
//   改一次就要重编译重烧录。现在可以在这里改、点一下下发，设备在线生效。
//
// 【两种用法：日常改门限 / 只让小程序告警】
//   ① 板子在跑 → 改完点「保存并下发设备」，落本机 + 下发设备，两边一致。
//   ② 只想让小程序告警、不关心板子响不响 → 改完点「仅本机生效」。
//      它把门限存到本机、【不碰设备】，实时监测页的阈值预警立刻按新门限判读。
//      （蜂鸣开关关掉只会让板子不响，但小程序侧的判读不受 buz 影响 —— 两条
//        判据本来就是分开的，见下方 3.3 的说明。）
//
// 【门限是怎么送到设备上的】(全部复用上报那条链路，没有新增任何基础设施)
//   小程序 --HTTPS--> apis.bemfa.com/va/sendMessage?topic=<主题>/set&msg={...}
//     → 平台把消息发布到 "<主题>/set"
//     → 平台推送给所有订阅了 "<主题>" 的设备（发布者自己不会收到）
//     → 设备本来就在收自己的上报回环，这条报文落进同一个 AT 接收缓冲
//     → 固件 net_poll_config() 认出 "cfg":1 标记 → 解析 → 提交
//   所以【不需要新的主题、不需要新的连接、不需要设备额外订阅什么】。
//
// 【为什么不能发到裸主题】
//   云端的取数接口只给"该主题最新一条"。往裸主题直发会把设备的上报记录顶掉，
//   实时监测页立刻显示成过期数据。故一律发到 "<主题>/set"。
//
// 【⚠ 改完不点按钮，门限不算改】滑块改的只是界面上的 this.data.T。
//   本机那一份（wx.setStorageSync）只有在按钮被点下之后才更新。而实时监测页
//   的阈值判读读的正是本机那一份 —— 所以"调了阈值却不告警"最可能的原因就是
//   只拖了滑块没点按钮。为消除这个陷阱，本页在有未保存改动时会把两个按钮
//   变成高亮，并在顶部直接写明。
//
// 【改了怎么知道生效了】
//   固件每帧遥测带一个 16 位配置指纹 cv（9 个门限值按固定顺序算出来）。
//   本页把"本机门限算出的指纹"与"设备报上来的指纹"摆在一起比 —— 相等就是
//   生效了。这样不必依赖设备回发应答（设备没有下行确认通道，且回发会和它
//   自己的上报抢同一条链路）。
//   【注意】走「仅本机生效」时两边指纹本来就该不等 —— 这不是故障。
//
// 【门限存在哪】
//   本机 wx.setStorageSync（这份 = "希望生效的门限"）；设备存在 RAM 里，
//   掉电回出厂默认。所以设备重启后，本页会显示指纹不一致 —— 那时点一下
//   「保存并下发设备」即可，不影响已有功能（回默认值本身是安全的）。
//   【为什么不写 Flash】擦页会停总线 20~40ms，而 5ms 周期的撞击监视任务
//   一次都不能被拖住（见 docs/28）。为省一次上电重发而冒漏判摔倒的风险，
//   这笔账不划算。

const bemfa = require('../../utils/bemfa.js')
const app = getApp()

/* 界面分组。item.k 必须与 bemfa.T_FIELDS 里的键名一致 ——
 * 那里是下发报文字段名与指纹顺序的【唯一来源】，这里只是换个说法显示。
 * step / digits 只影响界面手感，不影响下发（下发时统一按 ×10 或整数处理）。
 *
 * item.hint 的措辞：越限本身由【两条独立判据】各自处理 ——
 *   ① 小程序侧：实时监测页的阈值预警横幅（读本机门限，与 buz 无关）
 *   ② 设备侧：蜂鸣器（读设备门限，且受 buz 开关控制）
 * 所以这里只写"越限判读"，不承诺一定会响 —— 用户可能只想要小程序告警。 */
const GROUPS = [
  {
    key: 'temp', title: '温度', unit: '℃', step: 0.5, digits: 1, curKey: 'temp',
    note: '环境 / 体表温度（DHT11，读取失败时降级到 DS18B20）',
    items: [
      { k: 'tHi', name: '高温报警', hint: '高于此温度 → 判为越限' },
      { k: 'tLo', name: '低温报警', hint: '低于此温度 → 判为越限' }
    ]
  },
  {
    key: 'humi', title: '湿度', unit: '%RH', step: 1, digits: 0, curKey: 'humi',
    note: '相对湿度（DHT11）。此前湿度只有读数、没有任何判据，是本次新增的',
    items: [
      { k: 'hHi', name: '高湿报警', hint: '高于此湿度 → 判为越限' },
      { k: 'hLo', name: '低湿报警', hint: '低于此湿度 → 判为越限' }
    ]
  },
  {
    key: 'hr', title: '心率', unit: 'bpm', step: 1, digits: 0, curKey: 'hr',
    note: 'MAX30102 心率。贴手 3 秒稳定期后采集 15 秒，未佩戴时不产生读数',
    items: [
      { k: 'hrHi', name: '心率上限', hint: '高于此值 → 判为越限' },
      { k: 'hrLo', name: '心率下限', hint: '低于此值 → 判为越限' }
    ]
  },
  {
    key: 'spo2', title: '血氧', unit: '%', step: 1, digits: 0, curKey: 'spo2',
    note: 'MAX30102 血氧饱和度',
    items: [
      { k: 'sp', name: '血氧下限', hint: '低于此值 → 判为越限' }
    ]
  },
  {
    key: 'gas', title: '气体', unit: '', step: 1, digits: 0, curKey: 'gas',
    note: 'MQ-2 相对量程 0~100（未标定，只能做相对比较，不代表 ppm）',
    items: [
      { k: 'gas', name: '气体报警门限', hint: '高于此值 → 判为越限' }
    ]
  }
]

/* 迟滞量展示用 —— 与固件 alarm_cfg.h 的 ALARM_HYST_* 对应。
 * 【为什么要展示】门限附近的值必然抖动（传感器噪声），若不加迟滞，
 * 蜂鸣器会在门限上下"啾啾"地反复开关。固件的做法是：越过门限置位，
 * 回落到 门限−迟滞 才解除。这里把迟滞量写出来，免得用户以为是卡顿。 */
const HYST_NOTE = [
  '温度 0.5℃', '湿度 2%RH', '气体 3', '心率 2 bpm', '血氧 1%'
]

/* 用开关（而不是滑块）呈现的字段。新增这类字段时【必须加到这里】——
 * 它和 GROUPS 一起构成"这一页能改哪些字段"的完整答案，而
 * tools/wxmp_thresh_test.js 会拿这个集合与下发报文的字段集（bemfa.T_FIELDS）
 * 做全等比对。漏一个的后果是：报文里带了一个用户看不到也改不了的字段，
 * 它会一直悄悄地和设备上的值不一样。 */
const SWITCH_FIELDS = ['buz']

function fmtVal(v, digits) {
  return digits > 0 ? Number(v).toFixed(digits) : String(Math.round(Number(v)))
}

function p2(n) { return (n < 10 ? '0' : '') + n }

Page({
  data: {
    groups: [],
    T: {},

    /* 当前读数：让人能"看着实时值定门限"，而不必在两个页面之间来回记数 */
    cur: { temp: null, humi: null, hr: null, spo2: null, gas: null },
    curAt: '--',
    curErr: '',
    curLoading: false,

    /* 指纹核对 */
    cvLocal: 0,
    cvDev: null,
    cv: { level: 'info', text: '等待设备数据…' },

    /* 下发 */
    cmdMsg: '',
    cmdTopicText: '',
    cmdBytes: 0,
    problems: [],
    sending: false,
    lastSend: null,

    /* 有未落本的改动（拖了滑块但还没点任何按钮） */
    dirty: false,
    lastLocal: null,

    hystNote: HYST_NOTE.join(' / '),
    ranges: []
  },

  onLoad() {
    this.timer = null
  },

  onShow() {
    this.reload()
    this.startTimer()
  },

  onHide() { this.stopTimer() },
  onUnload() { this.stopTimer() },

  /* 设置页是"看一眼改一下"的场景，不需要像实时页那样 5 秒一刷。
   * 30 秒足够，也不会让"拖滑块时数字乱跳"。 */
  startTimer() {
    this.stopTimer()
    this.timer = setInterval(() => this.refreshCurrent(), 30000)
  },
  stopTimer() {
    if (this.timer) { clearInterval(this.timer); this.timer = null }
  },

  /* ================= 渲染 ================= */

  reload() {
    this.render(bemfa.loadThresh())
    this.refreshCurrent()
  },

  /* 把 T 展开成可渲染的行。每次改动都整体重建 —— 一共 9 行，代价可以忽略，
   * 换来的是"界面永远等于 this.data.T"这一个不变量。 */
  render(T) {
    const cur = this.cur || {}
    const groups = GROUPS.map(g => {
      const cv = cur[g.curKey]
      return {
        key: g.key,
        title: g.title,
        note: g.note,
        unit: g.unit,
        curText: (cv === null || cv === undefined)
          ? '无读数'
          : (fmtVal(cv, g.digits) + (g.unit ? ' ' + g.unit : '')),
        curRaw: (cv === null || cv === undefined) ? null : Number(cv),
        curKey: g.curKey,
        step: g.step,
        items: g.items.map(it => {
          const lim = bemfa.T_LIMIT[it.k] || [0, 100]
          const v = Number(T[it.k])
          return {
            k: it.k,
            name: it.name,
            hint: it.hint,
            min: lim[0], max: lim[1], step: g.step,
            val: v,
            text: fmtVal(v, g.digits) + (g.unit ? ' ' + g.unit : '')
          }
        })
      }
    })

    const msg = bemfa.buildConfigMsg(T)
    const bad = bemfa.validateThresh(T)
    const cfg = app.getCfg()
    const localCv = bemfa.cfgHash(T)

    /* 【有没有没落本的改动】拿界面上的 T 和本机存的那份比指纹。
     * 滑块改的只是 this.data.T；storage 要等按钮点下才更新。这个标志专门用来
     * 把"拖了滑块没点按钮"这个陷阱摆到界面上 —— 否则实时页不告警，用户会以为
     * 是判据坏了。 */
    const dirty = localCv !== bemfa.cfgHash(bemfa.loadThresh())

    this.setData({
      groups: groups,
      T: T,
      dirty: dirty,
      problems: bad,
      cmdMsg: msg,
      cmdBytes: msg.length,
      cmdTopicText: bemfa.cmdTopic(cfg),
      cvLocal: localCv,
      cv: this.cmpCv(localCv, this.data.cvDev),
      /* 这一页能改到的字段全集（滑块行 + 开关）。测试会拿它与下发报文的
       * 字段集做全等比对，见 SWITCH_FIELDS 的说明。 */
      editKeys: groups.reduce((a, g) => a.concat(g.items.map(i => i.k)), [])
                     .concat(SWITCH_FIELDS),
      ranges: bemfa.T_FIELDS.map(f => {
        const lim = bemfa.T_LIMIT[f.k] || [0, 0]
        return { k: f.k, range: lim[0] + '~' + lim[1], x10: f.x10 }
      })
    })
  },

  cmpCv(mine, dev) {
    if (dev === null || dev === undefined) {
      return { level: 'info',
               text: '设备未上报配置指纹（固件较旧，无法回读；本机门限仍会照常下发）' }
    }
    if (dev === mine) {
      return { level: 'ok', text: '已生效：设备 cv=' + dev + '，与本机一致' }
    }
    /* 【不等不一定是故障】用户点了「仅存本机」时，本机与设备本来就该不同 ——
     * 这正是他想要的（只让小程序告警）。所以这里用 warn 而不是 danger，
     * 并且把两种可能都说清楚，避免用户去反复点击下发、反而改了设备门限。 */
    return { level: 'warn',
             text: '两边不同：设备 cv=' + dev + '，本机 cv=' + mine +
                   '。若你刚点过「仅存本机」，这是正常的（板子仍按旧门限工作）；' +
                   '若确实想让板子也用这份门限，点「保存并下发设备」。' }
  },

  /* ================= 当前读数 ================= */

  refreshCurrent() {
    if (this.data.curLoading) return
    const cfg = app.getCfg()
    if (!cfg.uid || !cfg.topic) {
      this.setData({ curErr: '还没配置巴法云私钥与主题（在「实时监测」页填）' })
      return
    }
    this.setData({ curLoading: true })

    bemfa.fetchLatest(cfg).then(res => {
      let d
      try {
        d = bemfa.parseTelemetry(res.msg)
      } catch (e) {
        this.setData({ curLoading: false, curErr: e.message })
        return
      }
      const n = new Date()
      this.cur = { temp: d.temp, humi: d.humi, hr: d.hr, spo2: d.spo2, gas: d.gas }
      /* 设备指纹跟着这次取数一起更新 —— 它就是"回读确认"的证据来源 */
      const dev = (d.cv === null || d.cv === undefined) ? null : Number(d.cv)
      this.setData({
        curLoading: false,
        curErr: '',
        curAt: p2(n.getHours()) + ':' + p2(n.getMinutes()) + ':' + p2(n.getSeconds()),
        cvDev: dev,
        cv: this.cmpCv(bemfa.cfgHash(bemfa.loadThresh()), dev)
      })
      this.render(bemfa.loadThresh())
    }).catch(err => {
      this.setData({
        curLoading: false,
        curErr: (err.hint || err.message || String(err)) +
                (err.raw ? '（原始：' + err.raw + '）' : '')
      })
    })
  },

  onRefreshCur() { this.refreshCurrent() },

  /* ================= 改门限 ================= */

  /* 滑块松手才触发（用 bindchange 而不是 bindchanging）：拖拽过程中不 setData，
   * 避免几十次重渲染把界面拖卡。拖动时的数字由 slider 自带的 show-value 显示。 */
  onSlide(e) {
    const k = e.currentTarget.dataset.k
    const T = Object.assign({}, this.data.T)
    T[k] = Number(e.detail.value)
    this.render(T)
  },

  onStep(e) {
    const k = e.currentTarget.dataset.k
    const delta = Number(e.currentTarget.dataset.delta)
    const lim = bemfa.T_LIMIT[k] || [0, 100]
    const g = GROUPS.filter(x => x.items.some(i => i.k === k))[0]
    const step = g ? g.step : 1
    let v = Number(this.data.T[k]) + delta * step
    /* 加减也要夹在量程内：否则一路点下去会造出一个必然被固件整条拒的值 */
    v = Math.max(lim[0], Math.min(lim[1], v))
    const T = Object.assign({}, this.data.T)
    T[k] = Number(v.toFixed(3))
    this.render(T)
  },

  /* 把当前读数直接填成门限 —— 演示时最常用的动作：
   * 「想让高温报警现在就响，就把高温门限设到比当前温度低一点」。 */
  onUseCur(e) {
    const k = e.currentTarget.dataset.k
    const g = GROUPS.filter(x => x.items.some(i => i.k === k))[0]
    const cur = (this.cur || {})[g.curKey]
    if (cur === null || cur === undefined) {
      wx.showToast({ title: '还没有这个量的读数', icon: 'none' })
      return
    }
    const lim = bemfa.T_LIMIT[k] || [0, 100]
    const step = g.step
    let v = Math.round(Number(cur) / step) * step
    v = Math.max(lim[0], Math.min(lim[1], v))
    const T = Object.assign({}, this.data.T)
    T[k] = Number(v.toFixed(3))
    this.render(T)
    wx.showToast({ title: '已按当前读数填入', icon: 'none' })
  },

  onBuzChange(e) {
    const T = Object.assign({}, this.data.T)
    T.buz = e.detail.value ? 1 : 0
    this.render(T)
  },

  /* ================= 下发 ================= */

  onSend() {
    const T = this.data.T

    /* 固件对越界/矛盾的配置是【整条拒绝】。这里先拦一道，用户才能看到
     * 具体是哪一项不对；否则现象只是"点了没反应"。 */
    const bad = bemfa.validateThresh(T)
    if (bad.length) {
      wx.showModal({
        title: '这组门限不能下发',
        content: bad.join('；') + '。\n\n（设备侧对越界或高低颠倒的配置是整条拒绝、保持原样，' +
                 '不是逐项采纳 —— 逐项采纳会拼出"永远为真"的判据。）',
        showCancel: false
      })
      return
    }

    const cfg = app.getCfg()
    if (!cfg.uid || !cfg.topic) {
      wx.showToast({ title: '先配好私钥与主题', icon: 'none' })
      return
    }
    if (this.data.sending) return

    const msg = bemfa.buildConfigMsg(T)
    this.setData({ sending: true, lastSend: null })

    bemfa.sendConfig(cfg, msg).then(res => {
      /* 【只在推送成功后才落本机】本机那份的语义是"希望设备生效的门限"。
       * 若推送失败也存下来，本机判读与设备判读就永久性地不一致了 ——
       * 那正是这个功能最不该有的状态。 */
      bemfa.saveThresh(T)
      this.setData({
        sending: false,
        dirty: false,
        lastLocal: null,
        lastSend: {
          ok: true,
          at: this.nowStr(),
          text: '已推送到 ' + res.topic + '（' + res.bytes + ' 字节）。' +
                '设备 10 秒内上报的那一帧就会带上新的 cv —— 等它回来核对即可。'
        },
        cvDev: null,
        cv: { level: 'info', text: '已推送，等待设备下一帧上报…' }
      })
      wx.showToast({ title: '已下发', icon: 'success' })
      /* 立刻取一次数，让"设备指纹"尽早变成真实值 */
      setTimeout(() => this.refreshCurrent(), 12000)
    }).catch(err => {
      this.setData({
        sending: false,
        lastSend: {
          ok: false,
          at: this.nowStr(),
          text: '推送失败：' + (err.message || String(err)) +
                (err.raw ? '　原始：' + err.raw : '') +
                '　（本机门限【未】保存 —— 保持与设备一致）'
        }
      })
      wx.showToast({ title: '下发失败', icon: 'none' })
    })
  },

  /* 「仅本机生效」—— 把门限存到本机、【不碰设备】。
   *
   * 【为什么需要它】本机门限有两条用途：
   *   ① 作为"希望设备生效的门限"下发出去；
   *   ② 作为实时监测页阈值预警判读的依据（bemfa.checkThresholds 读的就是它）。
   * 用户可能只要 ② 不要 ①（"我不需要板子报警，只用小程序告警"）。这时如果
   * 只有「保存并下发设备」一条路，就会把门限推到设备上 —— 设备换了门限，
   * 而 buz 开关只管数值越限、管不到摔倒/SOS，用户会觉得"我明明只想小程序告警"。
   * 所以把"落本机"和"下发设备"拆成两个动作。
   *
   * 【不改 buz】buz 只影响设备蜂鸣器，与小程序判读无关。若用户想彻底静音板子，
   * 单独把上面的蜂鸣开关关掉再走这条路即可 —— 两件事分开表达更清楚。 */
  onSaveLocal() {
    const T = this.data.T
    const bad = bemfa.validateThresh(T)
    if (bad.length) {
      wx.showModal({
        title: '这组门限不合法',
        content: bad.join('；'),
        showCancel: false
      })
      return
    }
    bemfa.saveThresh(T)
    this.setData({
      dirty: false,
      lastLocal: {
        at: this.nowStr(),
        text: '已存到本机（未下发设备）。实时监测页的阈值预警会立即按这组门限判读。'
      }
    })
    wx.showToast({ title: '已存到本机', icon: 'success' })
  },

  onReset() {
    const def = bemfa.T_DEF
    wx.showModal({
      title: '恢复出厂默认',
      content: '界面会回到固件 user_config.h 里的出厂门限。' +
               '注意：这只是改界面上的值，还要点「保存并下发设备」才会真正生效。',
      success: (res) => {
        if (!res.confirm) return
        this.render(Object.assign({}, def))
        wx.showToast({ title: '已填入默认值', icon: 'none' })
      }
    })
  },

  onCopyMsg() {
    wx.setClipboardData({
      data: this.data.cmdMsg,
      success: () => wx.showToast({ title: '已复制报文', icon: 'none' })
    })
  },

  onCopyTopic() {
    wx.setClipboardData({
      data: this.data.cmdTopicText,
      success: () => wx.showToast({ title: '已复制下发主题', icon: 'none' })
    })
  },

  nowStr() {
    const n = new Date()
    return p2(n.getHours()) + ':' + p2(n.getMinutes()) + ':' + p2(n.getSeconds())
  },

  onBack() {
    wx.switchTab({ url: '/pages/index/index' })
  }
})
