/* =========================================================================
 * bemfa.js —— 巴法云 HTTP 接口封装（微信小程序侧）
 *
 * 【为什么不用 MQTT】
 *   板子那边是直连 bemfa.com:9501 跑 MQTT 的，但小程序【没有裸 TCP socket】，
 *   只能发 HTTPS(wx.request) / WSS。所以小程序只能用平台提供的 HTTP 查询接口
 *   定时轮询，拿"该主题的最新一条消息"。
 *
 * 【接口实测结论】(2026-09-16 由 tools/wxmp_api_probe.py 验证)
 *   ✓ https://apis.bemfa.com/va/getmsg?uid=<UID>&topic=<TOPIC>&type=1&num=1
 *       {"code":0,"message":"OK","data":[{"msg":"{...}","time":"2026-09-16 09:27:30","unix":1789522050}]}
 *   ✓ https://apis.bemfa.com/vb/api/v2/allTopic?openID=<UID>&type=1
 *       {"code":0,"msg":"success","data":{"data":[{topic,time,unix,online,pubOnline,msg,name,...}]}}
 *       （多返回 online/pubOnline/设备名，但需遍历主题；本模块默认不用）
 *   ✗ https://api.bemfa.com/api/device/v1/data/1/get/  → 40012 topic error
 *   ✗ num>1 想取历史 → data:null（该接口只给最新一条，历史只能靠自己缓存）
 *
 * 【重要】小程序后台必须把 apis.bemfa.com 加进 request 合法域名，否则真机上
 * 会直接失败（开发者工具里可临时勾"不校验合法域名"）。见 README.md。
 * ========================================================================= */

/* 板子上报的状态字段取值，一一对应 app_tasks.c 的 alarm_text_ascii() */
const STATUS_MAP = {
  'normal':      { level: 'ok',     cn: '状态正常',        icon: '✓' },
  'FALL ALARM':  { level: 'danger', cn: '检测到摔倒',      icon: '!' },
  'GAS ALARM':   { level: 'danger', cn: '气体浓度超标',    icon: '!' },
  'SOS ALARM':   { level: 'danger', cn: 'SOS 紧急求助',    icon: '!' },
  'HR ABNORMAL': { level: 'warn',   cn: '心率异常',        icon: '!' },
  'SPO2 LOW':    { level: 'warn',   cn: '血氧偏低',        icon: '!' },
  'TEMP HIGH':   { level: 'warn',   cn: '环境高温',        icon: '!' },
  'TEMP LOW':    { level: 'warn',   cn: '环境低温',        icon: '!' },
  'WIFI FAIL':   { level: 'warn',   cn: '网络上报失败',    icon: '!' },
  'SENSOR ERR':  { level: 'warn',   cn: '传感器读取失败',  icon: '!' },
  'unknown':     { level: 'info',   cn: '数据暂不可用',    icon: '?' }
}

/* 与固件 user_config.h 保持一致的阈值，用于前端二次提示（不替代固件报警）0000000
 *
 * 【2026-09-18 起这一组是"出厂默认值", 不再是唯一的一份】
 * 真正生效的门限由用户在小程序"阈值设置"页里改、存在本机、再下发到设备,
 * 最后以设备回读的 cv 指纹为准。这里保留同一组数, 是为了让"没设置过时
 * 界面显示什么"有个确定的答案, 以及让"恢复默认"有意义。
 *
 * 【这一组里的每一个数都必须能在 user_config.h / alarm_cfg.h 里找到出处】
 * 它是"镜像", 固件改了这里没改就会静默地对不上 —— 具体表现是：设备在响,
 * 小程序却说正常（或反过来）。已踩过两次, 故由 tools/wxmp_thresh_test.js
 * 直接读固件头文件逐项核对, 不再靠人眼比对。
 *
 * 【2026-09-22 起光照（lux）整条链路已删除】
 *   用户要求把光照功能从固件、OLED、上报报文与小程序里全部去掉, 因此
 *   这里的 luxLow / luxHigh 两个镜像常量、checkThresholds 里的光照判据、
 *   首页的"光照 PT0603"卡片、趋势页的"光照"视图都一并删除。
 *   ⇒ 固件侧判"光照不足"的 ALARM_LIGHT_LOW 也已删除,
 *     STATUS_MAP 里不再需要 'LOW LIGHT' 这一项。 */
const THRESH = {
  gas:       50,     /* GAS_ALARM_LEVEL */
  tempHigh:  40,     /* TEMP_HIGH_X10 = 400 */
  tempLow:   10,     /* TEMP_LOW_X10  = 100 */
  humiHigh:  90,     /* HUMI_HIGH_X10 = 900 */
  humiLow:   20,     /* HUMI_LOW_X10  = 200 */
  hrMin:     50,     /* HR_MIN_BPM */
  hrMax:     120,    /* HR_MAX_BPM */
  spo2Min:   90,     /* SPO2_MIN_PERCENT */

  /* ---- 下面这一项曾经写错, 2026-09-18 更正, 说明留在注释里避免再错 ----
   *
   * accelHigh: 原值 2.5 是文档里早期的撞击门槛。固件现已改为
   *   FALL_ACCEL_HIGH_G = 1.8f（受 MPU6050 ±2g 物理量程限制，见 docs/25）。
   *   沿用 2.5 的后果是 1.8~2.5g 之间的撞击在设备上已触发判决，小程序却
   *   一个字都不提 —— 这类"设备在响、界面安静"的偏差正是要避免的。
   *
   * 这个量【不在】在线配置范围内（撞击门槛属于摔倒判据，刻意不给小程序改），
   * 所以它不会出现在阈值设置页里。 */
  accelHigh: 1.8     /* FALL_ACCEL_HIGH_G */
}

/* ------------------------------------------------------------------ *
 * 可在线下发的门限：默认值 / 量程 / 下发报文 / 指纹
 *
 * 【为什么默认值在这里又抄了一遍】
 *   它们与固件 user_config.h 第五节是同一批数。两端都要能独立工作, 抄一份
 *   是没办法的事, 但抄错会被测试抓住: tools/wxmp_thresh_test.js 会直接读
 *   固件头文件, 逐项核对这里的数值与量程 —— 而不是靠人眼比对。
 *
 * 【量程为什么小程序也要有一份】
 *   固件对越界值是【整条拒绝】(保持原配置)。若小程序不做前置校验, 用户拖
 *   滑块的越界值点下去只会看到"没生效", 完全不知道是被拒了。所以这里在
 *   下发前就拦住, 并给出越界的具体理由。
 * ------------------------------------------------------------------ */

/* 界面单位就是这里的单位: ℃ / %RH / bpm / 0~100 / % */
const T_DEF = {
  tHi: 40, tLo: 10, hHi: 90, hLo: 20,
  gas: 50, hrLo: 50, hrHi: 120, sp: 90, buz: 1
}

/* 量程, 与固件 alarm_cfg.h 的 CFG_* 宏一一对应(由测试脚本核对) */
const T_LIMIT = {
  tHi: [-20, 60], tLo: [-20, 60],
  hHi: [0, 100], hLo: [0, 100],
  gas: [1, 100], hrLo: [20, 200], hrHi: [20, 200], sp: [50, 100],
  buz: [0, 1]
}

/* 字段顺序 = 指纹的计算顺序, 与固件 AlarmCfg_Hash 完全一致, 不能改。
 *   x10 = true  → 按 ×10 定点下发(温湿度)
 *   x10 = false → 纯整数下发(气体/心率/血氧/开关)
 * 这两类混用会让固件把 gas=30 读成 300(见 tools/alarm_cfg_sim.py 的断言)。 */
const T_FIELDS = [
  { k: 'tHi',  x10: true  },
  { k: 'tLo',  x10: true  },
  { k: 'hHi',  x10: true  },
  { k: 'hLo',  x10: true  },
  { k: 'gas',  x10: false },
  { k: 'hrLo', x10: false },
  { k: 'hrHi', x10: false },
  { k: 'sp',   x10: false },
  { k: 'buz',  x10: false }
]

const KEY_THRESH = 'helmet_thresh'

/* 【下发接口地址不能写在这一段】它曾经就是这一行：
 *     const API_SENDMSG = API_HOST + '/va/sendMessage'
 * 但 const 有暂时性死区 —— API_HOST 要到下面才初始化，于是模块一被
 * require 就抛 ReferenceError: Cannot access 'API_HOST' before initialization。
 * 语法检查（vm.Script）和微信的编译都发现不了它，只有真正执行才炸，
 * 现象是小程序一打开就白屏。现在它挪到 API_HOST 定义之后，与 API_GETMSG 并列。
 * 【教训】这类"跨语句的初始化顺序"错误只能靠真跑一遍抓 —— 所以
 * tools/wxmp_thresh_test.js 的第一个动作就是 require 本模块。 */

/* 下发主题后缀。巴法云的规则: 往 "<主题>/set" 发布 = 推送给所有订阅了
 * "<主题>" 的设备(发布者自己不会收到)。设备订阅的正是裸主题, 所以不用
 * 改任何固件配置就能收到。
 * 【不要往裸主题直发】那会把设备的上报记录顶掉(云端只留最新一条)。 */
const CMD_TOPIC_SUFFIX = '/set'

/* 判断"在线/离线"的阈值（秒）。板子上报周期 10s，轮询 5s。 */
const FRESH_OK   = 30
const FRESH_WARN = 90

/* 【要加进白名单的就是下面这一个字符串】
 * 它和巴法云的 MQTT 主机 bemfa.com 不是同一个域名 —— 微信的白名单不认子域，
 * 所以「配了 bemfa.com」或「配了不带协议的 apis.bemfa.com」都不算数，
 * 必须一字不差地填 https://apis.bemfa.com。
 * （已实测：bemfa.com/va/getmsg 与 api.bemfa.com/va/getmsg 都是 404，
 *   接口只在这个主机上，所以也没法靠换域名绕过去。） */
const API_HOST = 'https://apis.bemfa.com'
const API_GETMSG = API_HOST + '/va/getmsg'
/* 下发用的推送接口。与取数是【同一个主机】—— 也就是说只要取数能用，
 * 这条路就能用，不需要再配第二条白名单。 */
const API_SENDMSG = API_HOST + '/va/sendMessage'

/* 请求参数集中在这里，诊断时要显示"到底请求了哪个地址" */
function buildUrl(cfg) {
  const c = cfg || {}
  return API_GETMSG + '?uid=' + (c.uid || '') + '&topic=' + (c.topic || '') + '&type=1&num=1'
}

/* ------------------------------------------------------------------ *
 * 失败归类
 *
 * 【为什么要"归类"和"原始 errMsg"同时保留】
 *   早期版本用我猜测的原因【覆盖】了 wx 给的原始 errMsg，结果当失败原因
 *   不在猜测表里时，唯一的一手线索被丢掉了，只能来回猜。
 *   现在两者都留着：hint 是给人看的建议，raw 是证据。
 * ------------------------------------------------------------------ */
const FAIL_HINT = {
  domain:  '域名未加入白名单：微信公众平台 → 开发管理 → 开发设置 → 服务器域名 → ' +
           'request 合法域名，添加 ' + API_HOST +
           '（必须是带 https:// 的完整主机名；配 bemfa.com 无效，那是另一个域名）',
  network: '网络层不通：手机没网 / DNS 解析失败 / 当前网络屏蔽了该域名。换 4G 或另一个 WiFi 试',
  tls:     'TLS 证书握手失败：当前网络可能有代理或证书劫持，换网络重试',
  timeout: '请求超时：换网络重试；若一直超时，先在手机上打开 ' + API_HOST + ' 看能否访问',
  unknown: '未归类的失败，请把原始 errMsg 原文发出来'
}

function classifyFail(err) {
  const raw = (err && err.errMsg) || String(err || '')
  let kind = 'unknown'

  if (/url not in domain list/i.test(raw))               kind = 'domain'
  else if (/timeout/i.test(raw))                          kind = 'timeout'
  else if (/ssl|handshake|cert/i.test(raw))               kind = 'tls'
  else if (/ERR_|net::|ERR_NAME|ERR_CONNECTION/i.test(raw)) kind = 'network'

  return { raw: raw, kind: kind, hint: FAIL_HINT[kind] }
}

/* 造一个"带证据"的错误对象：message 给用户看，raw/kind/hint 给诊断用 */
function makeFail(err, httpStatus) {
  const c = classifyFail(err)
  const e = new Error(c.hint || c.raw)
  e.raw = c.raw
  e.kind = c.kind
  e.hint = c.hint
  e.httpStatus = httpStatus || 0
  return e
}

/* 平台业务错误码 -> 人话 */
function codeHint(code, message) {
  const M = {
    10002: '请求参数有误（检查私钥/主题是否为空、type 是否为 1）',
    40000: '平台未知错误',
    40004: '私钥或主题错误（私钥要和控制台一致；主题要在该账号下已创建）'
  }
  const base = '平台返回 code=' + code + (message ? '（' + message + '）' : '')
  return M[code] ? (base + ' — ' + M[code]) : base
}

/* ------------------------------------------------------------------ *
 * 拉取该主题的最新一条消息
 * 成功 resolve( { msg, cloudTime, unix } )
 * 失败 reject(Error) —— 带 .raw / .kind / .hint 三个诊断字段
 * ------------------------------------------------------------------ */
function fetchLatest(cfg) {
  return new Promise(function (resolve, reject) {
    if (!cfg || !cfg.uid) return reject(new Error('未配置巴法云私钥(UID)'))
    if (!cfg.topic)       return reject(new Error('未配置主题名'))

    wx.request({
      url: API_GETMSG,
      method: 'GET',
      data: { uid: cfg.uid, topic: cfg.topic, type: 1, num: 1 },
      timeout: 8000,
      success: function (res) {
        if (res.statusCode !== 200) {
          const e = new Error('平台返回 HTTP ' + res.statusCode)
          e.kind = 'http'
          e.raw = 'statusCode=' + res.statusCode
          e.hint = '接口返回非 200：先确认手机能打开 ' + API_HOST + '；若被网络劫持会出现这种情况'
          return reject(e)
        }
        const body = res.data
        if (!body || typeof body !== 'object') {
          const e = new Error('平台返回内容无法识别（可能被网络劫持插入内容）')
          e.kind = 'body'
          e.raw = String(body).slice(0, 120)
          return reject(e)
        }
        if (body.code !== 0) {
          const e = new Error(codeHint(body.code, body.message))
          e.kind = 'biz'
          e.raw = JSON.stringify(body).slice(0, 200)
          return reject(e)
        }
        const arr = body.data
        if (!Array.isArray(arr) || arr.length === 0) {
          const e = new Error('接口通了，但该主题下还没有任何消息（设备还没成功上报过）')
          e.kind = 'empty'
          e.raw = JSON.stringify(body).slice(0, 200)
          return reject(e)
        }
        const it = arr[0]
        resolve({ msg: it.msg, cloudTime: it.time || '', unix: it.unix || 0 })
      },
      fail: function (err) {
        reject(makeFail(err))
      }
    })
  })
}

/* ------------------------------------------------------------------ *
 * 连接诊断：发一次真实请求，把"成不成、卡在哪一层"如实报出来。
 * 【设计要点】永远 resolve，绝不 reject —— 失败也是诊断结果，不该被吞掉。
 * ------------------------------------------------------------------ */
function probe(cfg) {
  const c = cfg || {}
  const t0 = Date.now()
  return new Promise(function (resolve) {
    wx.request({
      url: API_GETMSG,
      method: 'GET',
      data: { uid: c.uid, topic: c.topic, type: 1, num: 1 },
      timeout: 8000,
      success: function (res) {
        resolve({ ok: true, ms: Date.now() - t0, statusCode: res.statusCode, body: res.data })
      },
      fail: function (err) {
        const cc = classifyFail(err)
        resolve({ ok: false, ms: Date.now() - t0, raw: cc.raw, kind: cc.kind, hint: cc.hint })
      }
    })
  })
}

/* ------------------------------------------------------------------ *
 * 解析固件上报的紧凑 JSON 报文
 *
 * 固件真实格式（NET_JSON_COMPACT = 1，约 165 字节，必须 ≤191 字节才落库）：
 *   {"seq":14,"hr":null,"spo2":null,"sc":0,"ps":3,"d":58234,"a":312,"b":9,
 *    "cv":38972,"t":26.4,"h":58.0,"gas":21,"ax":3.9,"ay":0.2,"az":0.6,
 *    "st":"normal","up":148245}
 *
 * 【2026-09-22 起】ts/tsv 已不再上报（换成了 d/a/b，见 docs/37）；
 *   光照 lux 字段随光照功能一并删除，解析里不再读取它。
 * ------------------------------------------------------------------ */
function parseTelemetry(msg) {
  let d
  try {
    d = JSON.parse(msg)
  } catch (e) {
    throw new Error('报文不是合法 JSON（前 60 字符）：' + String(msg).slice(0, 60))
  }
  if (d && d.ovf) {
    throw new Error('设备报文超长被降级上报（ovf=1），本条只有序号、没有传感器数据')
  }

  const num = function (v) {
    if (v === null || v === undefined || v === '') return null
    const n = Number(v)
    return isNaN(n) ? null : n
  }

  const ax = num(d.ax), ay = num(d.ay), az = num(d.az)
  const hasAcc = (ax !== null || ay !== null || az !== null)
  const accel = hasAcc
    ? Math.sqrt((ax || 0) * (ax || 0) + (ay || 0) * (ay || 0) + (az || 0) * (az || 0))
    : null

  return {
    /* 设备侧时间：无电池 RTC 每次上电会归零，仅供参照 */
    ts: d.ts || '',
    tsv: num(d.tsv),                 /* 1 = 时间戳可信，0 = 未校时 */
    seq: num(d.seq),
    hr: num(d.hr),
    spo2: num(d.spo2),
    temp: num(d.t),
    humi: num(d.h),
    gas: num(d.gas),
    ax: ax, ay: ay, az: az,
    accel: accel,
    st: d.st || '',
    up: num(d.up),
    /* PPG 测量状态(固件 HR_State_t 的取值): 0 没手指 / 1 稳定中 /
     * 2 采集中 / 3 测量完成。取值含义见 ppgNote() 的说明。
     * 老固件不带这个字段, 此时为 null。 */
    ps: num(d.ps),
    /* 设备当前生效的配置指纹(2026-09-18 起)。用来回答"我下发的门限到底
     * 生效了没有" —— 不相等就说明设备还没收到, 或者被整条拒了。
     * 老固件不带这个字段, 此时为 null, 界面上要能容忍。 */
    cv: num(d.cv)
  }
}

/* ------------------------------------------------------------------ *
 * PPG 测量状态(ps) -> 人话
 *
 * 【为什么必须有这个函数】设备做的是【一次测量】: 贴手 → 稳定 3 秒 →
 * 采集 10 秒 → 出结果并冻结。这 13 秒里 hr 一直是 null, 加上"测完了但
 * 没采到有效脉搏"也是 null —— 一共四种情况 hr 都是 null, 而它们的
 * 处置【完全不同】。固件为此专门上报了 ps 字段(app_events.h 的 ppg_st)。
 *
 * 2026-09-21 之前, 小程序把 ps 丢掉了, 对 hr=null 一律显示"未佩戴" ——
 * 于是"测完了没测出"被说成"没戴", 方向指错, 用户只能反复猜。
 * 实测抓到的真实一帧: {"ps":3,"hr":null} = 测量已跑完、判定没测到,
 * 界面却写"未检测到（未佩戴 / 信号不足）"。
 *
 * 参数 ps 为 null 时(老固件)退回原来的笼统文案, 不能显示"状态 0"。
 * ------------------------------------------------------------------ */
function ppgNote(d) {
  const ps = (d && d.ps !== undefined) ? d.ps : null
  if (ps === 0) {
    return '传感器没检测到皮肤接触：把指腹贴住光学窗口（两个小圆点那一面），不要隔外壳或保护膜'
  }
  if (ps === 1) {
    return '已检测到接触，正在稳定（约 3 秒），之后才开始采集'
  }
  if (ps === 2) {
    return '正在采集 10 秒窗口，保持不动；窗口跑完才发布结果'
  }
  if (ps === 3) {
    return '本次测量已跑完，但没采到有效脉搏 —— 拿开手、重新贴一次再测'
  }
  return '未检测到（未佩戴 / 信号不足）'
}

/* ps 的短标签, 用在需要一行说明"设备在干嘛"的地方 */
function ppgStateText(ps) {
  if (ps === 0) return '设备：未检测到手指'
  if (ps === 1) return '设备：稳定中'
  if (ps === 2) return '设备：采集中'
  if (ps === 3) return '设备：测量已完成'
  return '设备：状态未知'
}

/* 状态字段 -> 展示用（等级 / 中文 / 图标） */
function classifyStatus(st) {
  if (STATUS_MAP[st]) return STATUS_MAP[st]
  return { level: 'warn', cn: st ? ('未知状态 ' + st) : '无状态字段', icon: '?' }
}

/* ------------------------------------------------------------------ *
 * 数据新鲜度：用【云端接收时间】判断，不要用报文里的 ts
 *   理由：无电池 RTC，ts 每次上电回到编译初值，会和真实时间差好几天。
 * ------------------------------------------------------------------ */
function evalFreshness(unix, nowSec) {
  if (!unix) return { level: 'unknown', text: '无云端时间戳', age: null }
  const now = nowSec || Math.floor(Date.now() / 1000)
  const age = Math.max(0, now - unix)
  if (age <= FRESH_OK) {
    return { level: 'ok', text: '设备在线', age: age }
  }
  if (age <= FRESH_WARN) {
    return { level: 'warn', text: '数据延迟 ' + age + ' 秒', age: age }
  }
  return { level: 'danger', text: '设备已离线 ' + fmtAge(age), age: age }
}

function fmtAge(sec) {
  if (sec < 60) return sec + ' 秒'
  if (sec < 3600) return Math.floor(sec / 60) + ' 分钟'
  if (sec < 86400) return Math.floor(sec / 3600) + ' 小时'
  return Math.floor(sec / 86400) + ' 天'
}

/* 运行时长 ms -> "1天02:03:04" */
function fmtUptime(ms) {
  if (ms === null || ms === undefined) return '--'
  let s = Math.floor(ms / 1000)
  const d = Math.floor(s / 86400); s -= d * 86400
  const h = Math.floor(s / 3600);  s -= h * 3600
  const m = Math.floor(s / 60);    s -= m * 60
  const p2 = function (n) { return (n < 10 ? '0' : '') + n }
  const hms = p2(h) + ':' + p2(m) + ':' + p2(s)
  return d > 0 ? (d + '天 ' + hms) : hms
}

/* ------------------------------------------------------------------ *
 * 阈值的"前端二次提示"：把越过门限的量列出来。
 * 【为什么是"二次"】判据本身在固件里(蜂鸣器由它驱动)。小程序这一份的作用是
 * 把结果翻译成人话并列出来 —— 它用的是【你设置的那套门限】(与下发给设备
 * 的完全相同), 所以两边不会各说各话。
 * 传 T 就用 T, 不传则读本机保存的门限。
 * ------------------------------------------------------------------ */
function checkThresholds(d, T) {
  const t = T || loadThresh()
  const out = []
  if (d.gas !== null && d.gas > t.gas) {
    out.push({ key: 'gas', text: '气体浓度 ' + d.gas + '，已超过门限 ' + t.gas })
  }
  if (d.temp !== null && d.temp > t.tHi) {
    out.push({ key: 'tHi', text: '温度 ' + d.temp + '°C，已超过高温门限 ' + t.tHi + '°C' })
  }
  if (d.temp !== null && d.temp < t.tLo) {
    out.push({ key: 'tLo', text: '温度 ' + d.temp + '°C，已低于低温门限 ' + t.tLo + '°C' })
  }
  if (d.humi !== null && d.humi > t.hHi) {
    out.push({ key: 'hHi', text: '湿度 ' + d.humi + '%RH，已超过高湿门限 ' + t.hHi + '%RH' })
  }
  if (d.humi !== null && d.humi < t.hLo) {
    out.push({ key: 'hLo', text: '湿度 ' + d.humi + '%RH，已低于低湿门限 ' + t.hLo + '%RH' })
  }
  if (d.hr !== null && (d.hr < t.hrLo || d.hr > t.hrHi)) {
    out.push({ key: 'hr', text: '心率 ' + d.hr + ' bpm，超出 ' + t.hrLo + '~' + t.hrHi + ' 范围' })
  }
  if (d.spo2 !== null && d.spo2 < t.sp) {
    out.push({ key: 'sp', text: '血氧 ' + d.spo2 + '%，低于 ' + t.sp + '%' })
  }
  /* 下面这一条不在在线配置的范围里: 撞击门限是摔倒判据的一部分
   * (安全功能, 刻意不给小程序改)。 */
  if (d.accel !== null && d.accel > THRESH.accelHigh) {
    out.push({ key: 'accel', text: '合加速度 ' + d.accel.toFixed(2) + 'g，超过撞击门限 ' + THRESH.accelHigh + 'g' })
  }
  return out
}

/* ================================================================== *
 * 阈值：本机保存 / 校验 / 下发报文 / 指纹
 * ================================================================== */

/* 读出本机保存的门限。缺字段用默认值补齐 —— 这样以后新增字段时,
 * 老用户存下来的那份不会因为少一个键而整份失效。 */
function loadThresh() {
  let saved = null
  try { saved = wx.getStorageSync(KEY_THRESH) } catch (e) { saved = null }
  const t = Object.assign({}, T_DEF)
  if (saved && typeof saved === 'object') {
    T_FIELDS.forEach(function (f) {
      const v = Number(saved[f.k])
      if (!isNaN(v) && saved[f.k] !== null && saved[f.k] !== '') t[f.k] = v
    })
  }
  return t
}

function saveThresh(t) {
  const next = Object.assign({}, T_DEF, t || {})
  try { wx.setStorageSync(KEY_THRESH, next) } catch (e) { /* 存储满等异常不该拖垮界面 */ }
  return next
}

function resetThresh() {
  try { wx.removeStorageSync(KEY_THRESH) } catch (e) {}
  return Object.assign({}, T_DEF)
}

/* 校验一组门限。返回【问题说明的数组】, 空数组 = 可以下发。
 *
 * 【为什么要和固件做同一套校验】固件对越界/矛盾的配置是整条拒绝。若这里
 * 放行, 用户看到的现象是"点了下发却没反应"; 若这里就拦下, 界面上能直接
 * 说清"高温门限必须高于低温门限"。两侧的边界值由测试脚本核对固件头文件。 */
function validateThresh(t) {
  const bad = []
  const num = function (k) { return Number(t[k]) }

  T_FIELDS.forEach(function (f) {
    const v = num(f.k)
    const lim = T_LIMIT[f.k]
    if (isNaN(v)) { bad.push(f.k + ' 不是数字'); return }
    if (v < lim[0] || v > lim[1]) {
      bad.push(f.k + ' = ' + v + ' 超出允许范围 ' + lim[0] + '~' + lim[1])
    }
  })

  /* 顺序约束: 与固件 cfg_validate 的最后三条一一对应。
   * 颠倒的高低门限会让固件那条判据"永远为真"或"永远为假"。 */
  if (!(num('tLo') < num('tHi')))   bad.push('低温门限必须小于高温门限')
  if (!(num('hLo') < num('hHi')))   bad.push('低湿门限必须小于高湿门限')
  if (!(num('hrLo') < num('hrHi'))) bad.push('心率下限必须小于上限')

  return bad
}

/* 门限 → 下发用的定点整数数组(顺序 = T_FIELDS = 固件指纹顺序) */
function threshToWire(t) {
  return T_FIELDS.map(function (f) {
    const v = Number(t[f.k]) || 0
    return f.x10 ? Math.round(v * 10) : Math.round(v)
  })
}

/* 门限指纹 —— 必须与固件 AlarmCfg_Hash 逐位一致。
 *   h = 5381; 逐值(按 T_FIELDS 顺序)取 int32 小端 4 字节:
 *   h = ((h * 33) ^ byte) & 0xFFFF
 * 【为什么用这么简单的算法】它要在 C 和 JS 两侧手写实现。任何涉及浮点、
 * 字符编码或字节序假设的算法都有"两边算出不同数"的风险, 而后果是:
 * 设备其实生效了, 小程序却一直说"没生效", 人会不停地重发。
 * 交叉验证: tools/alarm_cfg_sim.py 产出对照向量, wxmp_thresh_test.js 逐个核对。 */
function cfgHash(t) {
  const vals = threshToWire(t)
  let h = 5381
  for (let i = 0; i < vals.length; i++) {
    let x = vals[i] < 0 ? (vals[i] + 4294967296) : vals[i]
    for (let k = 0; k < 4; k++) {
      h = ((h * 33) ^ (x & 0xff)) & 0xffff
      x = x >>> 8
    }
  }
  return h
}

/* 定点整数 → 报文字面量。整数就写整数(40 而不是 40.0), 报文更短;
 * 且保证固件解析回来是同一个数(它按十进制整数部分 ×10 + 一位小数)。 */
function wireToText(w) {
  const sign = w < 0 ? '-' : ''
  const a = Math.abs(w)
  const i = Math.floor(a / 10), d = a % 10
  return d === 0 ? (sign + i) : (sign + i + '.' + d)
}

/**
 * 生成下发给设备的配置报文。
 * 【"cfg":1 这个标记是必须的】设备在 AT 接收缓冲里就靠它把"给我的配置"与
 * "我自己刚发出去的回显报文"区分开。少了它, 设备会整条忽略。
 * 【字段名里不要用 seq】设备用 "seq":N, 做回环确认(证明自己的数据真到了
 * 云端), 混进同名字段会污染那个判据。这里用 m 表示"小程序自己的消息序号"。
 */
function buildConfigMsg(t, m) {
  const wire = threshToWire(t)
  const parts = ['"cfg":1']
  if (m !== undefined && m !== null) parts.push('"m":' + Math.round(m))
  for (let i = 0; i < T_FIELDS.length; i++) {
    const f = T_FIELDS[i]
    parts.push('"' + f.k + '":' + (f.x10 ? wireToText(wire[i]) : String(wire[i])))
  }
  return '{' + parts.join(',') + '}'
}

/**
 * 通过 HTTP 接口把配置推给设备。
 *
 * 走的是巴法云的"推送消息"接口(GET /va/sendMessage), 与取数用的是同一个
 * 主机 —— 也就是说只要取数能用, 这条路就能用, 不需要再配第二条白名单。
 *
 * @param cfg   {uid, topic}
 * @param msg   报文文本(buildConfigMsg 的产物)
 * @param opt   {direct:true} = 直发裸主题(不推荐, 会顶掉云端的上报记录)。
 *              默认发到 "<主题>/set", 由平台推给订阅该主题的设备。
 * 成功 resolve({ topic, msg, bytes, direct }); 失败 reject(Error, 带诊断字段)
 */
function sendConfig(cfg, msg, opt) {
  const c = cfg || {}
  const direct = !!(opt && opt.direct)
  const topic = (c.topic || '') + (direct ? '' : CMD_TOPIC_SUFFIX)

  return new Promise(function (resolve, reject) {
    if (!c.uid)   return reject(new Error('未配置巴法云私钥(UID)'))
    if (!c.topic) return reject(new Error('未配置主题名'))
    if (!msg)     return reject(new Error('配置报文为空'))

    wx.request({
      url: API_SENDMSG,
      method: 'GET',
      data: { uid: c.uid, topic: topic, type: 1, msg: msg },
      timeout: 8000,
      success: function (res) {
        if (res.statusCode !== 200) {
          const e = new Error('推送接口返回 HTTP ' + res.statusCode)
          e.kind = 'http'
          e.raw = 'statusCode=' + res.statusCode
          return reject(e)
        }
        const body = res.data
        if (!body || typeof body !== 'object') {
          const e = new Error('推送接口返回内容无法识别')
          e.kind = 'body'
          e.raw = String(body).slice(0, 120)
          return reject(e)
        }
        if (body.code !== 0) {
          const e = new Error(codeHint(body.code, body.message))
          e.kind = 'biz'
          e.raw = JSON.stringify(body).slice(0, 200)
          return reject(e)
        }
        resolve({ topic: topic, msg: msg, bytes: msg.length, direct: direct })
      },
      fail: function (err) {
        reject(makeFail(err))
      }
    })
  })
}

/* 下发主题(供界面显示与复制, 便于在巴法云控制台手工验证) */
function cmdTopic(cfg, direct) {
  return ((cfg && cfg.topic) || '') + (direct ? '' : CMD_TOPIC_SUFFIX)
}

/* 本地记录：数据点（趋势用）与报警（记录用） */
const KEY_POINTS = 'helmet_points'
const KEY_ALARMS = 'helmet_alarms'
const KEY_LASTSEQ = 'helmet_last_seq'

function loadPoints() { return wx.getStorageSync(KEY_POINTS) || [] }
function loadAlarms() { return wx.getStorageSync(KEY_ALARMS) || [] }
function clearRecords() {
  wx.removeStorageSync(KEY_POINTS)
  wx.removeStorageSync(KEY_ALARMS)
  wx.removeStorageSync(KEY_LASTSEQ)
}

/**
 * 把一条新数据并入本地记录。
 * 【必须按 seq 去重】：板子 10s 上报一次，而小程序 5s 轮询一次，
 * 直接追加会出现同一个 seq 被记两次，趋势图会有重复点。
 * 返回 { appended: 是否新增了点, restarted: 设备是否重启过 }
 */
function recordPoint(d, unix, keep) {
  const limit = keep || 60
  const lastSeq = wx.getStorageSync(KEY_LASTSEQ)
  const restarted = (lastSeq !== '' && lastSeq !== null && d.seq !== null && d.seq < lastSeq)

  let appended = false
  if (d.seq === null || d.seq !== lastSeq) {
    const pts = loadPoints()
    pts.push({
      unix: unix || 0,
      seq: d.seq,
      hr: d.hr, spo2: d.spo2,
      temp: d.temp, humi: d.humi,
      gas: d.gas,
      accel: d.accel,
      st: d.st
    })
    while (pts.length > limit) pts.shift()
    wx.setStorageSync(KEY_POINTS, pts)
    wx.setStorageSync(KEY_LASTSEQ, d.seq)
    appended = true
  }

  /* 报警状态才记一条记录（normal 不记，避免记录被刷爆） */
  const info = classifyStatus(d.st)
  if (info.level !== 'ok' && info.level !== 'info') {
    const als = loadAlarms()
    const lastOne = als[als.length - 1]
    if (!lastOne || lastOne.seq !== d.seq) {
      als.push({
        unix: unix || 0,
        seq: d.seq,
        st: d.st,
        cn: info.cn,
        level: info.level,
        hr: d.hr, spo2: d.spo2, temp: d.temp, gas: d.gas, accel: d.accel
      })
      while (als.length > 100) als.shift()
      wx.setStorageSync(KEY_ALARMS, als)
    }
  }

  return { appended: appended, restarted: restarted }
}

/* 云端 unix（秒）-> 本地可读时间字符串 */
function fmtCloudTime(unix) {
  if (!unix) return '--'
  const dt = new Date(unix * 1000)
  const p2 = function (n) { return (n < 10 ? '0' : '') + n }
  return p2(dt.getMonth() + 1) + '-' + p2(dt.getDate()) + ' ' +
         p2(dt.getHours()) + ':' + p2(dt.getMinutes()) + ':' + p2(dt.getSeconds())
}

module.exports = {
  THRESH: THRESH,
  T_DEF: T_DEF,
  T_LIMIT: T_LIMIT,
  T_FIELDS: T_FIELDS,
  KEY_THRESH: KEY_THRESH,
  API_SENDMSG: API_SENDMSG,
  CMD_TOPIC_SUFFIX: CMD_TOPIC_SUFFIX,
  loadThresh: loadThresh,
  saveThresh: saveThresh,
  resetThresh: resetThresh,
  validateThresh: validateThresh,
  threshToWire: threshToWire,
  cfgHash: cfgHash,
  wireToText: wireToText,
  buildConfigMsg: buildConfigMsg,
  sendConfig: sendConfig,
  cmdTopic: cmdTopic,
  STATUS_MAP: STATUS_MAP,
  API_HOST: API_HOST,
  API_GETMSG: API_GETMSG,
  FAIL_HINT: FAIL_HINT,
  buildUrl: buildUrl,
  codeHint: codeHint,
  probe: probe,
  fetchLatest: fetchLatest,
  parseTelemetry: parseTelemetry,
  ppgNote: ppgNote,
  ppgStateText: ppgStateText,
  classifyStatus: classifyStatus,
  evalFreshness: evalFreshness,
  checkThresholds: checkThresholds,
  fmtUptime: fmtUptime,
  fmtCloudTime: fmtCloudTime,
  fmtAge: fmtAge,
  loadPoints: loadPoints,
  loadAlarms: loadAlarms,
  clearRecords: clearRecords,
  recordPoint: recordPoint
}
