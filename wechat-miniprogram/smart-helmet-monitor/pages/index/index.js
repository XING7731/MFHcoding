// pages/index/index.js —— 实时监测（双通道取数）
//
// 数据链路：板子(10s MQTT 上报) → 巴法云主题 → 本页
//                                            ├── WSS 实时推送（wss://bemfa.com:9504/wss）
//                                            └── HTTP 轮询（apis.bemfa.com/va/getmsg）
//
// 【为什么要两条通道】
//   小程序的两类合法域名是【互相独立】的配置项：
//     · WSS  需要 socket 合法域名  wss://bemfa.com
//     · HTTP 需要 request 合法域名 https://apis.bemfa.com
//   只配了其中一条时，另一条必然失败。两条都实现，"总有一条通"。
//   （实测：WSS 用私钥作 ClientID 连接期间，板子仍持续上报，不会互踢。
//     验证脚本 tools/wxmp_wss_test.js）
//
// 三个关键判据（都是本项目实测踩出来的，别改错）：
//   ① 判断"设备在不在线"用【最近一次真正收到新数据的时刻】，不要用报文里的 ts
//      —— 板子没有 VBAT 电池，RTC 每次上电都回到编译初值，ts 会和真实时间差好几天。
//   ② 判断"有没有新数据"用【seq 是否变化】，并按 seq 去重
//      —— HTTP 轮询比上报快，不去重会在趋势里出现重复点。
//   ③ 在线判据不能只看"请求成功"。HTTP 轮询成功只是说明"接口通了"，
//      如果板子早停了，接口照样返回那条老记录。所以在线判据绑定在 seq 变化上。

const bemfa = require('../../utils/bemfa.js')
const mqttws = require('../../utils/mqttws.js')
const app = getApp()

function p2(n) { return (n < 10 ? '0' : '') + n }

/* 诊断用的失败归类 -> 人话。和 bemfa.js 的 FAIL_HINT 配合：
 * FAIL_HINT 给"怎么办"，这里给"是什么"。 */
const DIAG_KIND = {
  domain:  '域名未加入 request 合法域名白名单',
  network: '网络层不通（无网 / DNS 解析失败 / 被该网络屏蔽）',
  tls:     'TLS 证书握手失败（网络可能有代理或劫持）',
  timeout: '请求超时',
  unknown: '未能归类，看原始 errMsg'
}

/* WSS 连不上的典型原因 -> 人话 */
function wssHint(errText) {
  const m = String(errText || '')
  if (/url not in domain|domain list/i.test(m)) {
    return 'socket 合法域名未配置：微信公众平台 → 开发管理 → 开发设置 → 服务器域名 → ' +
           'socket 合法域名，填 wss://bemfa.com（注意：这条和 request 是两栏，不能互相顶替）'
  }
  if (/timeout/i.test(m))  return '连接超时：换网络重试；部分企业/校园网会拦长连接'
  if (/fail|refus|reset|1006/i.test(m)) {
    return '连接被断开：可能是网络拦截了长连接，改用 HTTP 轮询模式即可绕开'
  }
  return 'WSS 连接失败：' + m
}

/* 把任意返回体压成一行可读文本，用于诊断报告 */
function strBody(b) {
  if (b === null || b === undefined) return '(空)'
  if (typeof b === 'string') return b.slice(0, 200)
  try { return JSON.stringify(b).slice(0, 200) } catch (e) { return '(无法序列化)' }
}

Page({
  data: {
    /* 连接设置 */
    cfg: { uid: '', topic: '', intervalSec: 5, channel: 'auto' },
    showSetting: false,
    uidInput: '',
    topicInput: '',
    intervalInput: '',

    /* 状态 */
    loading: true,
    errMsg: '',
    errRaw: '',          /* wx 给的原始 errMsg —— 一手证据，不要在 UI 上省略它 */
    fresh: { level: 'unknown', text: '正在获取…', age: null },
    cloudTime: '--',
    lastPollAt: '--',

    /* 通道 */
    chan: { kind: 'http', level: 'info', text: '通道未启动', detail: '' },
    /* 通道选择器用 */
    chanOptions: [
      { key: 'auto', label: '自动（推送优先）' },
      { key: 'wss',  label: 'WSS 实时推送' },
      { key: 'http', label: 'HTTP 轮询' }
    ],

    /* 诊断 */
    appId: '(未取到)',
    apiHost: '',
    wssUrl: '',
    diag: null,
    diagRunning: false,

    /* 展示层 */
    v: {},
    st: { level: 'info', cn: '等待数据', icon: '?' },
    rawSt: '',
    tips: [],
    cvText: '--',

    /* 统计 */
    pollCount: 0,
    newCount: 0,
    seqGap: '',
    restarted: false,
    paused: false
  },

  /* ================= 生命周期 ================= */

  onLoad() {
    this.lastSeq = null
    this.lastDataAt = 0        /* 最近一次"真正收到新 seq"的秒级时刻 —— 在线判据的基准 */
    this.wssCli = null
    this.wssFallbackTimer = null
    this.httpTimer = null
    this.tickTimer = null

    /* 【为什么要把 AppID 显示出来】
     * "我明明配了合法域名" 最常见的两种落空原因是：
     *   ① 配的是另一个域名。小程序要用的两个域名是
     *        request: https://apis.bemfa.com
     *        socket : wss://bemfa.com
     *      白名单不认子域，api / apis 少一个 s 就完全对不上；
     *   ② 域名配在了【另一个 AppID】的账号下。
     * 第 ② 种光看代码发现不了，把 AppID 摆在界面上就能一眼核对。 */
    let appId = '(未取到)'
    try {
      const acc = wx.getAccountInfoSync()
      appId = (acc && acc.miniProgram && acc.miniProgram.appId) || appId
    } catch (e) { /* 旧基础库或工具环境取不到，不影响主流程 */ }

    this.setData({
      appId: appId,
      apiHost: bemfa.API_HOST,
      wssUrl: mqttws.DEFAULT_URL
    })
    this.syncCfgToData()
  },

  onShow() {
    this.syncCfgToData()
    if (!this.data.paused) this.startChannel()
  },

  onHide() { this.stopChannel() },
  onUnload() { this.stopChannel() },

  /* ================= 配置同步 ================= */

  syncCfgToData() {
    const cfg = app.getCfg()
    this.setData({
      cfg: cfg,
      uidInput: cfg.uid,
      topicInput: cfg.topic,
      intervalInput: String(cfg.intervalSec)
    })
  },

  /* ================= 通道管理 ================= */

  /* 统一入口：根据配置决定走哪条路。'auto' = 先 WSS，连不上自动退回 HTTP。 */
  startChannel() {
    this.stopChannel()
    const cfg = app.getCfg()
    const mode = cfg.channel || 'auto'

    if (mode === 'http') {
      this.startHttp('按配置使用 HTTP 轮询')
      return
    }

    /* WSS 模式（含 auto）：先连推送。给 12 秒连接窗口，
     * 超时或报错就按配置决定是否退回轮询。 */
    this.startWss(mode === 'auto')
  },

  stopChannel() {
    this.stopWss()
    this.stopHttp()
    this.stopTick()
  },

  /* ---------------- HTTP 轮询 ---------------- */

  startHttp(reason) {
    this.stopHttp()
    /* 通道等级反映【这条通道健不健在】，不反映"是哪条"。
     * 降级来的轮询同样是健康可用的，所以照样给 ok；
     * "这是降级来的"这件事放在文案里说，不和健康度混在一个字段里
     * （否则界面会出现"能用却标着警告"这种自相矛盾的样子）。 */
    const isFallback = !!reason
    this.setChannel('http', 'ok',
                    isFallback ? 'HTTP 轮询（已降级）' : 'HTTP 轮询中',
                    reason || '')
    this.poll()
    const sec = Math.max(2, Number(this.data.cfg.intervalSec) || 5)
    this.httpTimer = setInterval(() => this.poll(), sec * 1000)
    this.startTick()
  },

  stopHttp() {
    if (this.httpTimer) { clearInterval(this.httpTimer); this.httpTimer = null }
  },

  /* ---------------- WSS 实时推送 ---------------- */

  startWss(allowFallback) {
    this.stopWss()
    this.setChannel('wss', 'info', 'WSS 连接中…', mqttws.DEFAULT_URL)

    let settled = false
    const cfg = app.getCfg()

    /* 连接窗口超时 -> 判定为不可用 */
    this.wssFallbackTimer = setTimeout(() => {
      if (settled) return
      settled = true
      this.handleWssDown('连接超时（12 秒内未完成握手）', allowFallback)
    }, 12000)

    this.wssCli = mqttws.connect({
      url: cfg.wssUrl || mqttws.DEFAULT_URL,
      clientId: cfg.uid,
      topic: cfg.topic,
      keepAliveSec: cfg.wssKeepAlive || 60,
      reconnect: false,          /* 重连由本页统一管，避免和降级逻辑打架 */

      onStatus: (text, detail) => {
        if (/已订阅/.test(text)) {
          /* 订阅成功才算真正就绪 */
          settled = true
          if (this.wssFallbackTimer) { clearTimeout(this.wssFallbackTimer); this.wssFallbackTimer = null }
          this.setChannel('wss', 'ok', 'WSS 实时推送', text)
          this.startTick()
        }
      },

      onMessage: (payload) => {
        this.onPush(payload)
      },

      onError: (e) => {
        if (settled) {
          /* 已连上之后出错：直接降级 */
          this.handleWssDown(e, allowFallback)
        } else {
          settled = true
          if (this.wssFallbackTimer) { clearTimeout(this.wssFallbackTimer); this.wssFallbackTimer = null }
          this.handleWssDown(e, allowFallback)
        }
      }
    })
  },

  stopWss() {
    if (this.wssFallbackTimer) { clearTimeout(this.wssFallbackTimer); this.wssFallbackTimer = null }
    if (this.wssCli) {
      try { this.wssCli.close() } catch (e) {}
      this.wssCli = null
    }
  },

  /* WSS 挂掉：要么退回轮询，要么把失败如实报出来 */
  handleWssDown(reason, allowFallback) {
    this.stopWss()
    if (allowFallback) {
      /* 降级原因里带上"该去哪配"的指引，不要只留一句原始错误码 */
      this.startHttp(wssHint(reason))
      return
    }
    this.setChannel('wss', 'danger', 'WSS 连接失败', reason)
    this.setData({
      loading: false,
      errMsg: wssHint(reason),
      errRaw: String(reason || ''),
      fresh: { level: 'unknown', text: '数据获取失败', age: null }
    })
  },

  /* ---------------- 新鲜度心跳 ---------------- */

  /* WSS 模式下没有数据到达时不会触发渲染，界面会一直停在最后一条的状态。
   * 这个定时器专门负责把"数据延迟/设备离线"及时刷新到界面上。 */
  startTick() {
    this.stopTick()
    this.tickTimer = setInterval(() => this.refreshFreshness(), 3000)
  },

  stopTick() {
    if (this.tickTimer) { clearInterval(this.tickTimer); this.tickTimer = null }
  },

  refreshFreshness() {
    if (!this.lastDataAt) return
    this.setData({ fresh: bemfa.evalFreshness(this.lastDataAt) })
  },

  setChannel(kind, level, text, detail) {
    this.setData({ chan: { kind: kind, level: level, text: text, detail: detail || '' } })
  },

  onTogglePause() {
    const paused = !this.data.paused
    this.setData({ paused: paused })
    if (paused) this.stopChannel()
    else        this.startChannel()
  },

  onManualRefresh() {
    this.setData({ errMsg: '' })
    if (this.data.chan.kind === 'wss') {
      /* 推送模式重连一次即可 */
      this.startWss(this.data.cfg.channel === 'auto')
    } else {
      this.poll()
    }
  },

  /* ================= 统一数据入口 ================= */

  /* WSS 推送到达 */
  onPush(payload) {
    let d
    try {
      d = bemfa.parseTelemetry(payload)
    } catch (e) {
      this.setData({ errMsg: '收到推送但解析失败：' + e.message, errRaw: String(payload).slice(0, 200) })
      return
    }
    /* 推送的每一帧都意味着"设备此刻活着"，所以新鲜度基准取本机当前时间，
     * 而不是报文里那个不可信的 ts。 */
    this.applyReading(d, Math.floor(Date.now() / 1000), 'wss')
  },

  /* HTTP 轮询到达 */
  poll() {
    const cfg = app.getCfg()
    bemfa.fetchLatest(cfg).then(res => {
      let d
      try {
        d = bemfa.parseTelemetry(res.msg)
      } catch (e) {
        this.setData({
          loading: false,
          errMsg: e.message,
          lastPollAt: this.nowStr()
        })
        return
      }
      /* HTTP 侧有云端接收时间，用它做新鲜度基准更准（老记录会直接反映成"离线"）。
       * 回退到本机时间只在平台没给 unix 时发生。 */
      this.applyReading(d, res.unix || Math.floor(Date.now() / 1000), 'http', res.unix)
    }).catch(err => {
      this.setData({
        loading: false,
        /* message 是"建议怎么办"，raw 是 wx 给的原文。
         * 两个都要显示：只显示建议时，一旦原因不在我的推测表里就再也查不下去了。 */
        errMsg: err.hint || err.message || String(err),
        errRaw: err.raw || '',
        fresh: { level: 'unknown', text: '数据获取失败', age: null },
        pollCount: this.data.pollCount + 1,
        lastPollAt: this.nowStr()
      })
      /* 第一次失败就自动跑一次诊断，省掉"用户再点一下"这一步 */
      if (!this.data.diag) this.runDiagnose()
    })
  },

  nowStr() {
    const n = new Date()
    return p2(n.getHours()) + ':' + p2(n.getMinutes()) + ':' + p2(n.getSeconds())
  },

  /* 两条通道共用这一套：解析后的数据 -> 判色 -> 记录 -> 渲染。
   * 抽出来是为了保证 WSS 和 HTTP 的行为完全一致，不会一边有一边没有。 */
  applyReading(d, basisUnix, source, cloudUnix) {
    const now = new Date()
    const cfg = app.getCfg()

    /* 记入本机趋势缓存（按 seq 去重，详见 bemfa.js） */
    let rec = { appended: false, restarted: false }
    try {
      rec = bemfa.recordPoint(d, basisUnix, cfg.keepPoints)
    } catch (e) { /* 存储失败不该影响主流程 */ }

    /* 【在线判据的关键一步】只有"新 seq"才推进 lastDataAt。
     * 若用"请求成功"来推进，板子停机后接口仍会一直返回同一条老记录，
     * 界面就会永远显示"在线"——这正是最容易看走眼的地方。 */
    const isNew = (rec.appended || d.seq === null)
    if (isNew) this.lastDataAt = basisUnix

    /* 序号间隔：正常每轮 +1；跳号说明中间有报文没落库（例如超长被平台丢弃） */
    let gap = this.data.seqGap
    const prev = this.lastSeq
    if (typeof prev === 'number' && typeof d.seq === 'number') {
      const diff = d.seq - prev
      if (diff < 0)       gap = '序号回到 ' + d.seq + ' → 设备重启过'
      else if (diff > 1)  gap = '较上一条 +' + diff + ' 条（中间有报文未落库，或刚打开）'
      else                gap = ''
    }
    if (typeof d.seq === 'number') this.lastSeq = d.seq

    this.setData({
      loading: false,
      errMsg: '',
      errRaw: '',
      fresh: bemfa.evalFreshness(this.lastDataAt || basisUnix),
      st: bemfa.classifyStatus(d.st),
      tips: bemfa.checkThresholds(d),
      rawSt: d.st || '(报文无 st 字段)',
      /* WSS 推送里没有云端时间，只有设备时间（不可信），所以显示本机接收时刻并标注 */
      cloudTime: (source === 'wss' || !cloudUnix)
        ? ('本地收 ' + this.nowStr())
        : bemfa.fmtCloudTime(cloudUnix),
      lastPollAt: this.nowStr(),
      v: this.buildView(d),
      cvText: this.cvText(d.cv),
      pollCount: this.data.pollCount + 1,
      newCount: this.data.newCount + (rec.appended ? 1 : 0),
      restarted: this.data.restarted || rec.restarted || (gap.indexOf('重启') >= 0),
      seqGap: gap
    })
  },

  /* ================= 连接诊断 ================= */

  onDiagnose() { this.runDiagnose() },

  /* 两条通道各发一次真实尝试，把"成不成、卡在哪一层"如实报出来。
   * 【设计要点】永远 resolve，绝不 reject —— 失败也是诊断结果，不该被吞掉。 */
  runDiagnose() {
    if (this.data.diagRunning) return
    this.setData({ diagRunning: true })

    const cfg = app.getCfg()
    const rows = []
    const add = (k, v) => rows.push({
      k: k,
      v: String(v === undefined || v === null ? '' : v)
    })

    const fail = (verdict, tip) => {
      this.setData({
        diagRunning: false,
        diag: { rows: rows, level: 'danger', verdict: verdict, tip: tip, at: this.nowStr() }
      })
    }

    /* ---------- 第 1 段：HTTP 通道 ---------- */
    add('小程序 AppID', this.data.appId)
    add('私钥(前 8 位)', cfg.uid ? (cfg.uid.slice(0, 8) + '…') : '（空！）')
    add('主题名', cfg.topic || '（空！）')

    const httpStep = new Promise(resolve => {
      wx.getNetworkType({
        success: r => resolve(r.networkType || 'unknown'),
        fail: () => resolve('（取不到）')
      })
    }).then(net => {
      add('手机网络', net)
      add('HTTP 请求地址', bemfa.API_GETMSG)
      return bemfa.probe(cfg)
    }).then(r => {
      if (!r.ok) {
        add('HTTP 结果', '失败（耗时 ' + r.ms + 'ms）')
        add('HTTP 原始 errMsg', r.raw)
        add('HTTP 归类', DIAG_KIND[r.kind] || r.kind)
        return { httpOk: false, kind: r.kind, hint: r.hint, raw: r.raw }
      }
      if (r.statusCode !== 200) {
        add('HTTP 结果', '通了，但 HTTP ' + r.statusCode)
        add('HTTP 原始 body', strBody(r.body))
        return { httpOk: false, kind: 'http', raw: 'statusCode=' + r.statusCode,
                 hint: '先确认手机浏览器能打开 ' + bemfa.API_HOST + '。非 200 常见于网络劫持或运营商拦截。' }
      }
      const body = r.body
      if (!body || typeof body !== 'object') {
        add('HTTP 结果', '200 但内容不是 JSON')
        add('HTTP 原始 body', strBody(r.body))
        return { httpOk: false, kind: 'body', raw: strBody(r.body),
                 hint: '大概率被网络劫持插入了内容，换个网络再试。' }
      }
      if (body.code !== 0) {
        add('HTTP 结果', '200 但平台报错 code=' + body.code + (body.message ? '（' + body.message + '）' : ''))
        return { httpOk: false, kind: 'biz', raw: 'code=' + body.code,
                 hint: bemfa.codeHint(body.code, body.message) }
      }
      if (!body.data || !body.data.length) {
        add('HTTP 结果', '200，但该主题下没有任何消息')
        return { httpOk: true, empty: true,
                 hint: '域名和私钥都没问题。要么板子还没成功上报过，要么主题名与固件里不一致。' }
      }
      add('HTTP 结果', '✓ 200（耗时 ' + r.ms + 'ms）')
      add('HTTP 取到 seq', (function () {
        try { return JSON.parse(body.data[0].msg).seq } catch (e) { return '(解析失败)' }
      })())
      add('HTTP 云端时间', body.data[0].time || '--')
      return { httpOk: true, empty: false }
    }).catch(e => {
      add('HTTP 结果', '诊断自身出错：' + ((e && e.message) || e))
      return { httpOk: false, kind: 'unknown', raw: '', hint: '' }
    })

    /* ---------- 第 2 段：WSS 通道 ---------- */
    const wssStep = (httpRes) => new Promise(resolve => {
      add('WSS 地址', cfg.wssUrl || mqttws.DEFAULT_URL)

      /* 只有在 HTTP 也不通时才值得再花 10 秒试 WSS，避免诊断太久。
       * 反过来 HTTP 通、WSS 不通也要报出来 —— 这才是"推送用不了但轮询能用"的场景。 */
      const t0 = Date.now()
      let done = false
      const finish = (state, detail) => {
        if (done) return
        done = true
        if (cli) { try { cli.close() } catch (e) {} }
        add('WSS 结果', state === 'ok'
          ? ('✓ 已连接并订阅（耗时 ' + (Date.now() - t0) + 'ms）')
          : ('失败：' + detail))
        resolve({ wssOk: state === 'ok', detail: detail })
      }

      const cli = mqttws.connect({
        url: cfg.wssUrl || mqttws.DEFAULT_URL,
        clientId: cfg.uid,
        topic: cfg.topic,
        keepAliveSec: 30,
        reconnect: false,
        onStatus: (text) => { if (/已订阅/.test(text)) finish('ok', text) },
        onError: (e) => finish('err', e),
        onMessage: () => {}      /* 诊断只需证明"能连上并订阅"，不必等数据 */
      })

      setTimeout(() => finish('err', '10 秒内未完成握手或订阅'), 10000)
    })

    httpStep.then(httpRes => {
      if (!httpRes.httpOk) {
        /* HTTP 不通时 WSS 往往是唯一出路，所以继续测 */
        return wssStep(httpRes).then(w => {
          if (w.wssOk) {
            this.setData({
              diagRunning: false,
              diag: {
                rows: rows, level: 'warn',
                verdict: 'HTTP 不通，但 WSS 可以 —— 建议把取数通道设为自动或 WSS',
                tip: '两条通道的白名单是独立的两栏。现在只有 socket 那栏配好了，' +
                     '所以请把通道切到「自动」或「WSS 实时推送」，即可正常取数。' +
                     '若想同时修好 HTTP，再去 request 合法域名里加上 ' + bemfa.API_HOST + '。',
                at: this.nowStr()
              }
            })
            return
          }
          /* 两条都不通：给最可能的那条建议 */
          const isDomain = httpRes.kind === 'domain'
          this.setData({
            diagRunning: false,
            diag: {
              rows: rows, level: 'danger',
              verdict: isDomain
                ? '两条通道都不通，且 HTTP 报的是"域名未在白名单"'
                : '两条通道都不通',
              tip: (httpRes.hint || '') +
                   '　两条通道需要的白名单（在微信公众平台 → 开发管理 → 开发设置 → 服务器域名）：' +
                   '① request 合法域名 = ' + bemfa.API_HOST +
                   '；② socket 合法域名 = wss://bemfa.com。' +
                   '注意 api.bemfa.com 与 apis.bemfa.com 是两个不同域名，不能互相顶替。',
              at: this.nowStr()
            }
          })
        })
      }

      /* HTTP 通了，再看 WSS（用于判断推送能不能用） */
      return wssStep(httpRes).then(w => {
        if (httpRes.empty) {
          this.setData({
            diagRunning: false,
            diag: { rows: rows, level: 'warn',
                    verdict: '接口通，但该主题下没有任何消息',
                    tip: httpRes.hint, at: this.nowStr() }
          })
          return
        }
        this.setData({
          diagRunning: false,
          diag: {
            rows: rows,
            level: w.wssOk ? 'ok' : 'ok',
            verdict: w.wssOk
              ? '全部正常：两条通道都能用，推荐「自动」模式（实时推送 + 轮询兜底）'
              : 'HTTP 通道正常，WSS 通道不可用（不影响使用，轮询照常工作）',
            tip: w.wssOk
              ? '若页面仍显示失败，多半是上一次请求的瞬时抖动，点“立即刷新”即可。'
              : '要用实时推送，请到 socket 合法域名里加上 wss://bemfa.com（与 request 是两栏）。',
            at: this.nowStr()
          }
        })
      })
    }).catch(e => fail('诊断脚本自身出错', String((e && e.message) || e)))
  },

  /* ================= 取值 + 判色 ================= */

  buildView(d) {
    /* 【门限从哪来】可在线配置的那几项（温湿度 / 心率 / 血氧 / 气体）读【本机保存的
     * 门限】—— 它和下发到设备的是同一份数，所以"界面说的"和"蜂鸣器做的"不会各
     * 说各话。要改就上「阈值设置」页改并下发。
     * 不能在线改的一项（撞击门限是摔倒判据的一部分，
     * 属于安全功能，刻意不给小程序改）仍读编译期常量 THRESH。 */
    const T  = bemfa.loadThresh()
    const TH = bemfa.THRESH

    /* 心率 —— 没读数时不能只说"未检测到"。
     * 设备做的是【一次测量】(贴手 → 稳定 3 秒 → 采集 10 秒 → 冻结结果),
     * 下面四种情况 hr 都是 null, 但处置完全不同, 固件用 ps 把它们分开了:
     *   ps=0 没接触到皮肤 / ps=1 稳定中 / ps=2 采集中(1、2 等着就行)
     *   ps=3 测完了但没采到有效脉搏  ← 这个要提醒用户【重测】, 不是"没戴"
     * 所以 ps=3 标 warn 而不是 mute —— 否则界面把"测了没测出"说成"没戴",
     * 方向就指错了(2026-09-21 实测抓到 {"ps":3,"hr":null} 被显示成"未佩戴")。 */
    let hrCls = (d.ps === 3) ? 'v-warn' : 'v-mute'
    let hrNote = bemfa.ppgNote(d)
    if (d.hr !== null) {
      if (d.hr < T.hrLo || d.hr > T.hrHi) {
        hrCls = 'v-warn'
        hrNote = '超出 ' + T.hrLo + '~' + T.hrHi + ' bpm 门限'
      } else {
        hrCls = 'v-ok'
        hrNote = '在 ' + T.hrLo + '~' + T.hrHi + ' 正常范围内'
      }
    }

    /* 血氧 —— 与心率同源: 同一颗 MAX30102、同一个测量窗口。
     * hr 有值而 spo2 没有时, 说明窗口本身是成功的, 只是血氧那一路没成,
     * 不要复用"没采到脉搏"这句。 */
    let spo2Cls = (d.ps === 3) ? 'v-warn' : 'v-mute'
    let spo2Note = (d.hr !== null)
      ? '本次窗口没测出血氧（血氧对信号稳定性要求更高）'
      : bemfa.ppgNote(d)
    if (d.spo2 !== null) {
      if (d.spo2 < T.sp) { spo2Cls = 'v-danger'; spo2Note = '低于 ' + T.sp + '%' }
      else               { spo2Cls = 'v-ok';     spo2Note = '正常' }
    }

    /* 温度（这里报的是环境/体表温度，取自 DHT11，失败时降级到 DS18B20） */
    let tempCls = 'v-mute', tempNote = '无数据'
    if (d.temp !== null) {
      if (d.temp > T.tHi)      { tempCls = 'v-danger'; tempNote = '高于 ' + T.tHi + '°C 高温门限' }
      else if (d.temp < T.tLo) { tempCls = 'v-warn';   tempNote = '低于 ' + T.tLo + '°C 低温门限' }
      else                     { tempCls = 'v-ok';     tempNote = '正常范围' }
    }

    /* 湿度 —— 门限是本次新加的：此前湿度只有读数、没有任何判据 */
    let humiCls = 'v-mute', humiNote = '无数据'
    if (d.humi !== null) {
      if (d.humi > T.hHi)      { humiCls = 'v-warn'; humiNote = '高于 ' + T.hHi + '%RH 高湿门限' }
      else if (d.humi < T.hLo) { humiCls = 'v-warn'; humiNote = '低于 ' + T.hLo + '%RH 低湿门限' }
      else                     { humiCls = 'v-ok';   humiNote = '在 ' + T.hLo + '~' + T.hHi + ' 范围内' }
    }

    /* 气体（MQ-2，相对量程 0~100） */
    let gasCls = 'v-mute', gasNote = '无数据'
    if (d.gas !== null) {
      if (d.gas > T.gas)           { gasCls = 'v-danger'; gasNote = '超过报警门限 ' + T.gas }
      else if (d.gas > T.gas * 0.8){ gasCls = 'v-warn';   gasNote = '接近门限 ' + T.gas }
      else                         { gasCls = 'v-ok';     gasNote = '门限 ' + T.gas + ' 以下' }
    }

    /* 姿态：合加速度 |a|。静止正常佩戴时应接近 1g */
    let accCls = 'v-mute', accNote = '无数据', posture = '--'
    if (d.accel !== null) {
      if (d.accel > TH.accelHigh) {
        accCls = 'v-danger'; posture = '剧烈撞击'
        accNote = '超过摔倒判据门限 ' + TH.accelHigh + 'g'
      } else if (d.accel > 1.3) {
        accCls = 'v-warn'; posture = '运动中'
        accNote = '高于静止水平（1g）'
      } else if (d.accel >= 0.7) {
        accCls = 'v-ok'; posture = '静止'
        accNote = '约 1g，符合静止佩戴'
      } else {
        accCls = 'v-warn'; posture = '异常'
        accNote = '接近 0g，疑似失重/掉落'
      }
    }

    return {
      hr: d.hr === null ? '--' : String(d.hr), hrCls: hrCls, hrNote: hrNote,
      spo2: d.spo2 === null ? '--' : String(d.spo2), spo2Cls: spo2Cls, spo2Note: spo2Note,
      temp: d.temp === null ? '--' : Number(d.temp).toFixed(1), tempCls: tempCls, tempNote: tempNote,
      humi: d.humi === null ? '--' : Number(d.humi).toFixed(1), humiCls: humiCls, humiNote: humiNote,
      gas: d.gas === null ? '--' : String(d.gas), gasCls: gasCls, gasNote: gasNote,

      ax: d.ax === null ? '--' : Number(d.ax).toFixed(1),
      ay: d.ay === null ? '--' : Number(d.ay).toFixed(1),
      az: d.az === null ? '--' : Number(d.az).toFixed(1),
      accel: d.accel === null ? '--' : d.accel.toFixed(2),
      accCls: accCls, accNote: accNote, posture: posture,

      seq: d.seq === null ? '--' : String(d.seq),
      uptime: bemfa.fmtUptime(d.up),
      devTime: d.ts || '--',
      tsvText: d.tsv === 1 ? '已校时（可信）' : '未校时（设备无电池，忽略此时间）',
      /* 设备此刻在 PPG 测量流程的哪一步。放在设备信息区, 因为它描述的是
       * "设备正在干什么", 而不是某一个读数的好坏。 */
      ppgText: bemfa.ppgStateText(d.ps),
      cv: d.cv
    }
  },

  /* 设备上生效的门限 vs 本机门限 —— 指纹对上就说明下发生效了。
   * 【为什么可以这么做】固件每帧遥测都带一个 16 位配置指纹 cv，由 9 个门限值
   * 按固定顺序算出来（见 utils/bemfa.js 的 cfgHash）。
   * 【不等 ≠ 故障】只有两种情况需要担心：① 门限改完还没下发；② 下发被固件整条
   * 拒了（越界或高低颠倒）。但还有一种【正常】情况：用户故意只让小程序告警
   * （「阈值设置」页的「仅存本机」），本机门限与设备门限本来就该不同。
   * 所以这里措辞不能再写死"请重新下发"，要把这种用法说明白 —— 否则用户会去
   * 反复点击下发，反而把设备门限改了、违背他的本意。 */
  cvText(devCv) {
    if (devCv === null || devCv === undefined) return '设备未上报指纹（固件较旧，无法回读）'
    const mine = bemfa.cfgHash(bemfa.loadThresh())
    if (devCv === mine) return '设备已生效 cv=' + devCv + '（与本机一致）'
    return '设备 cv=' + devCv + '，本机 ' + mine +
           ' → 两边门限不同。若你在「阈值设置」用了「仅存本机」，这是正常的；' +
           '若想两边一致，去那里点「保存并下发设备」。'
  },

  /* ================= 连接设置 ================= */

  onToggleSetting() {
    this.setData({ showSetting: !this.data.showSetting })
  },

  /* 复制要加白名单的域名 —— 手输容易漏 https:// 或把 apis 打成 api，
   * 而白名单对不上就是直接失败。给个"复制"能消掉一整类人为错误。 */
  onCopyHost() {
    wx.setClipboardData({
      data: bemfa.API_HOST,
      success: () => wx.showToast({ title: '已复制 request 域名', icon: 'none' })
    })
  },

  onCopyWss() {
    /* socket 合法域名只写主机名，不带路径和端口 */
    wx.setClipboardData({
      data: 'wss://bemfa.com',
      success: () => wx.showToast({ title: '已复制 socket 域名', icon: 'none' })
    })
  },

  onUidInput(e) { this.setData({ uidInput: e.detail.value }) },
  onTopicInput(e) { this.setData({ topicInput: e.detail.value }) },
  onIntervalInput(e) { this.setData({ intervalInput: e.detail.value }) },

  /* 切换取数通道：立即重建连接，不用保存 */
  onPickChannel(e) {
    const key = e.currentTarget.dataset.key
    app.saveCfg({ channel: key })
    this.syncCfgToData()
    wx.showToast({ title: '已切换通道', icon: 'none' })
    this.startChannel()
  },

  onSaveCfg() {
    const uid = (this.data.uidInput || '').trim()
    const topic = (this.data.topicInput || '').trim()
    let iv = parseInt(this.data.intervalInput, 10)
    if (isNaN(iv) || iv < 2) iv = 2
    if (iv > 300) iv = 300

    if (!uid || !topic) {
      wx.showToast({ title: '私钥与主题都要填', icon: 'none' })
      return
    }

    app.saveCfg({ uid: uid, topic: topic, intervalSec: iv })
    this.lastSeq = null
    this.lastDataAt = 0
    this.setData({ pollCount: 0, newCount: 0, seqGap: '' })
    this.syncCfgToData()
    wx.showToast({ title: '已保存，重新连接', icon: 'success' })
    this.startChannel()
  },

  onResetCfg() {
    app.resetCfg()
    this.lastSeq = null
    this.lastDataAt = 0
    this.syncCfgToData()
    wx.showToast({ title: '已恢复默认', icon: 'success' })
    this.startChannel()
  },

  onOpenTrend() {
    wx.switchTab({ url: '/pages/trend/trend' })
  },

  onOpenSettings() {
    wx.switchTab({ url: '/pages/settings/settings' })
  }
})
