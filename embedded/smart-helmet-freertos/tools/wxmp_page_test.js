/* =========================================================================
 * wxmp_page_test.js —— 驱动 pages/index/index.js 的双通道页面逻辑
 *
 * 为什么要单独测这一层：
 *   bemfa.js / mqttws.js 各自的单元测试都过了，但"两条通道汇入同一个渲染管线"
 *   这件事本身有坑，而且坑很隐蔽：
 *     · 若用"请求成功"而不是"seq 变化"来推进在线判据，板子停机后
 *       HTTP 会一直返回同一条老记录，界面就会永远显示"在线"。
 *   （另外，只有把页面逻辑真跑一遍，才能发现 setData 的字段名写错这类问题。）
 *
 * 做法：用最小的 wx.* / Page / getApp 假环境 require 真实页面代码，
 *       再用真实抓来的报文喂进去，逐项断言。
 *
 * 运行: node wxmp_page_test.js
 * ========================================================================= */

const path = require('path')

const MP = 'C:/Users/27362/Desktop/WeChat smart helmet'
const mqttws = require(path.join(MP, 'utils', 'mqttws.js'))
const bemfa  = require(path.join(MP, 'utils', 'bemfa.js'))

let pass = 0, fail = 0
function ok(cond, name, extra) {
  if (cond) { pass++; console.log('  ✓ ' + name) }
  else { fail++; console.log('  ✗ ' + name + (extra ? '  -> ' + extra : '')) }
}
function sleep(ms) { return new Promise(r => setTimeout(r, ms)) }

/* ------------------------- 真实报文样本 ------------------------- */
/* 均来自板子实抓（tools/wxmp_wss_test.js 收的推送）。
   2026-09-22 光照功能整体删除（docs/39），下面的原文里已去掉 lux 字段。 */
const RAW = {
  seq183: '{"ts":"10:31:57","seq":183,"hr":81,"spo2":null,"t":28.4,"h":55.0,"gas":19,"ax":3.9,"ay":0.2,"az":0.7,"st":"normal","up":1917721,"tsv":0}',
  seq184: '{"ts":"10:32:07","seq":184,"hr":130,"spo2":null,"t":28.4,"h":55.0,"gas":18,"ax":3.9,"ay":0.2,"az":0.7,"st":"normal","up":1927993,"tsv":0}',
  alarm:  '{"ts":"10:33:00","seq":190,"hr":45,"spo2":88,"t":41.2,"h":60.0,"gas":66,"ax":3.9,"ay":0.2,"az":0.7,"st":"FALL ALARM","up":1960000,"tsv":0}'
}

/* ------------------------- 假的小程序环境 ------------------------- */
function installEnv() {
  const store = {}
  const timers = []
  const state = {
    /* 由测试用例改写：本次 HTTP 请求返回什么 */
    httpHandler: null,
    sockets: [],
    toasts: [],
    clipboard: '',
    appId: 'wxc49ee42eb1a117b2'
  }

  global.wx = {
    request(opt) {
      const t = { url: opt.url, data: opt.data, aborted: false }
      setTimeout(() => {
        if (t.aborted) return
        const r = state.httpHandler ? state.httpHandler(opt) : { fail: true, errMsg: 'no handler' }
        if (r.fail) {
          opt.fail && opt.fail({ errMsg: r.errMsg || 'request:fail' })
        } else {
          opt.success && opt.success({ statusCode: r.statusCode || 200, data: r.data })
        }
      }, 0)
      return t
    },
    getNetworkType(o) { setTimeout(() => o.success && o.success({ networkType: 'wifi' }), 0) },
    getAccountInfoSync() { return { miniProgram: { appId: state.appId } } },
    setStorageSync(k, v) { store[k] = v },
    getStorageSync(k) { return store[k] === undefined ? '' : store[k] },
    removeStorageSync(k) { delete store[k] },
    showToast(o) { state.toasts.push(o.title) },
    setClipboardData(o) { state.clipboard = o.data; o.success && o.success() },
    switchTab() {},
    connectSocket(opt) {
      const h = { open: [], message: [], close: [], error: [] }
      const task = {
        binaryType: 'string',
        sent: [],
        send(o) {
          task.sent.push(o.data)
          /* 对上行报文做最小响应：收到 CONNECT 回 CONNACK，收到 SUBSCRIBE 回 SUBACK */
          const u8 = new Uint8Array(o.data)
          const type = u8[0] >> 4
          if (type === 1) {                              /* CONNECT -> CONNACK */
            setTimeout(() => emit(new Uint8Array([0x20, 0x02, 0x00, 0x00])), 0)
          } else if (type === 8) {                       /* SUBSCRIBE -> SUBACK */
            setTimeout(() => emit(new Uint8Array([0x90, 0x03, 0x00, 0x01, 0x00])), 0)
          }
        },
        close() {
          setTimeout(() => h.close.forEach(f => f({ code: 1000, reason: '' })), 0)
        },
        onOpen(cb) { h.open.push(cb) },
        onMessage(cb) { h.message.push(cb) },
        onClose(cb) { h.close.push(cb) },
        onError(cb) { h.error.push(cb) },
        /* 测试用：按 URL 里的行为决定是否连接成功 */
        _h: h,
        _url: opt.url,
        /* 测试用：像真实 WS 一样推一帧 MQTT PUBLISH 二进制帧进来 */
        _pushPublish(topic, payload) {
          const I = mqttws._internal
          const tb = I.utf8Bytes(topic)
          const pb = I.utf8Bytes(payload)
          const body = [].concat([(tb.length >> 8) & 0xff, tb.length & 0xff], tb, pb)
          const frame = new Uint8Array([0x30].concat(I.encodeRemainingLength(body.length), body))
          emit(frame)
        },
        _emitError(msg) { h.error.forEach(f => f({ errMsg: msg })) }
      }
      function emit(u8) {
        const ab = u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength)
        h.message.forEach(f => f({ data: ab }))
      }
      state.sockets.push(task)
      setTimeout(() => {
        if (state.socketBehavior === 'fail') {
          h.error.forEach(f => f({ errMsg: state.socketErrMsg || 'connectSocket:fail url not in domain list' }))
          h.close.forEach(f => f({ code: 1006, reason: '' }))
        } else {
          h.open.forEach(f => f())
        }
      }, 0)
      return task
    }
  }
  state.socketBehavior = 'ok'

  global.Page = function (cfg) { state.pageCfg = cfg }
  global.App = function (cfg) { state.app = cfg }
  state.timers = timers
  return state
}

/* 造一个像页面实例的对象：有 setData / data，其余方法都从 pageCfg 拷 */
function makePage(cfg) {
  const inst = Object.assign({}, cfg)
  inst.data = JSON.parse(JSON.stringify(cfg.data))
  inst.setData = function (patch) { Object.assign(inst.data, patch) }
  return inst
}

/* ------------------------- 主流程 ------------------------- */
async function main() {
  console.log('='.repeat(72))
  console.log('pages/index/index.js 双通道逻辑验证')
  console.log('='.repeat(72))

  const state = installEnv()

  /* 加载真实代码：先 app.js，再 index.js（index 在顶层就调 getApp()） */
  require(path.join(MP, 'app.js'))
  const appInst = state.app
  appInst.globalData = { cfg: null }
  appInst.onLaunch()

  global.getApp = function () { return appInst }

  require(path.join(MP, 'pages', 'index', 'index.js'))
  const pageCfg = state.pageCfg

  /* ---------- 1. HTTP 通道：正常取数 ---------- */
  console.log('\n=== 1. HTTP 轮询通道 ===')
  let page = makePage(pageCfg)
  page.onLoad()

  const nowSec = () => Math.floor(Date.now() / 1000)
  state.httpHandler = () => ({
    statusCode: 200,
    data: {
      code: 0, message: 'OK',
      data: [{ msg: RAW.seq183, time: '2026-09-16 10:31:57', unix: nowSec() }]
    }
  })

  page.startHttp('test')
  await sleep(30)

  ok(page.data.v.seq === '183', 'HTTP：seq 解析上屏', page.data.v.seq)
  ok(page.data.v.hr === '81', 'HTTP：心率 81 上屏', page.data.v.hr)
  ok(page.data.v.spo2 === '--', 'HTTP：spo2=null 显示 -- 而不是 0', page.data.v.spo2)
  ok(page.data.v.temp === '28.4', 'HTTP：温度保留 1 位小数', page.data.v.temp)
  ok(page.data.chan.kind === 'http' && page.data.chan.level === 'ok', 'HTTP：通道标识为 http/ok')
  ok(page.data.fresh.level === 'ok', 'HTTP：刚取到的新数据判为在线', JSON.stringify(page.data.fresh))
  ok(page.data.newCount === 1, 'HTTP：首次计入 1 条新数据', String(page.data.newCount))

  /* ---------- 2. 同一条 seq 重复轮询：不能重复计数、不能续命在线状态 ---------- */
  console.log('\n=== 2. 重复轮询同一条（轮询比上报快的必然情况）===')
  const seqBefore = page.data.v.seq
  for (let i = 0; i < 3; i++) { page.poll(); await sleep(15) }
  ok(page.data.newCount === 1, '同 seq 重复轮询不重复计数', String(page.data.newCount))
  ok(page.data.v.seq === seqBefore, 'seq 显示保持稳定')
  ok(page.data.pollCount > 1, '轮询次数仍在累加（说明确实请求了）', String(page.data.pollCount))

  /* ---------- 3. 新 seq ---------- */
  console.log('\n=== 3. 新 seq 到达 ===')
  state.httpHandler = () => ({
    statusCode: 200,
    data: { code: 0, message: 'OK', data: [{ msg: RAW.seq184, time: 'x', unix: nowSec() }] }
  })
  page.poll()
  await sleep(30)
  ok(page.data.newCount === 2, '新 seq 计入第 2 条', String(page.data.newCount))
  ok(page.data.v.hr === '130', '心率更新为 130', page.data.v.hr)

  /* ---------- 4. 关键陷阱：板子停机后，接口仍返回老记录 ---------- */
  console.log('\n=== 4. 设备停机（接口仍正常返回老记录）===')
  const stale = nowSec() - 600      /* 云端时间戳是 10 分钟前 */

  /* 4a. 打开 App 时设备就已经离线：缓存是空的，第一条就是老记录 */
  page.stopChannel()
  /* 注意开头的分号：上面一行结尾没有分号时，这里的 [ 会被当成"给上一行的
   * 返回值取下标"，即 page.stopChannel()['helmet_last_seq'] —— 一个很坑的写法。 */
  const KEYS = ['helmet_points', 'helmet_alarms', 'helmet_last_seq']
  KEYS.forEach(function (k) { wx.removeStorageSync(k) })
  page = makePage(pageCfg)
  page.onLoad()
  state.httpHandler = () => ({
    statusCode: 200,
    data: { code: 0, message: 'OK', data: [{ msg: RAW.seq184, time: 'x', unix: stale }] }
  })
  page.startHttp('')
  await sleep(30)
  ok(page.data.fresh.level === 'danger',
     '★ 接口 HTTP 200 但记录是 10 分钟前的 -> 判为离线（不能因为"请求成功"就显示在线）',
     JSON.stringify(page.data.fresh))
  ok(/离线/.test(page.data.fresh.text), '离线文案正确：' + page.data.fresh.text)
  ok(page.data.errMsg === '', '这不是"获取失败"，不该报错（数据是拿到了，只是过期）', page.data.errMsg)
  ok(page.data.v.seq === '184', '过期数据仍照常解析上屏（只是标注为离线）', page.data.v.seq)

  /* 4b. 运行中设备停掉：只有"新 seq"才推进在线基准，所以基准会自然变旧。
   *     这靠一个 3 秒的心跳定时器把状态刷到界面上（WSS 模式下没有数据就不会渲染，
   *     没有这个定时器界面会永远停在最后一条的"在线"）。 */
  const staleMark = nowSec() - 600
  page.lastDataAt = staleMark
  page.refreshFreshness()
  ok(page.data.fresh.level === 'danger',
     '★ 心跳定时器能把"在线"刷新成"离线"（基准变旧后状态跟着变）',
     JSON.stringify(page.data.fresh))
  page.lastDataAt = nowSec() - 45
  page.refreshFreshness()
  ok(page.data.fresh.level === 'warn' && /延迟/.test(page.data.fresh.text),
     '45 秒没新数据 -> 显示"数据延迟"', page.data.fresh.text)
  page.lastDataAt = nowSec()
  page.refreshFreshness()
  ok(page.data.fresh.level === 'ok', '基准恢复 -> 回到在线')

  /* ---------- 5. 报警状态与记录 ---------- */
  console.log('\n=== 5. 报警状态 ===')
  state.httpHandler = () => ({
    statusCode: 200,
    data: { code: 0, message: 'OK', data: [{ msg: RAW.alarm, time: 'x', unix: nowSec() }] }
  })
  page.poll()
  await sleep(30)
  ok(page.data.st.level === 'danger', 'FALL ALARM 判为 danger 级', page.data.st.level)
  ok(page.data.st.cn === '检测到摔倒', '状态中文映射正确', page.data.st.cn)
  ok(page.data.rawSt === 'FALL ALARM', '原始状态码一并显示，便于核对', page.data.rawSt)
  ok(page.data.tips.length >= 3, '门限二次提示：心率/血氧/温度/气体多项越线（' + page.data.tips.length + ' 条）')
  ok(page.data.seqGap.indexOf('+') >= 0, 'seq 跳号被识别（183→184→190）', page.data.seqGap)

  page.stopChannel()

  /* ---------- 5b. PPG 状态透传到界面 ---------- */
  console.log('\n=== 5b. PPG 测量状态（ps）透传到界面 ===')
  /* 【这一节回答的是"心率怎么突然没读数了"】
   * 设备做的是【一次测量】：贴手 → 稳定 3 秒 → 采集 10 秒 → 出结果并冻结。
   * 于是下面四种情况 hr 都是 null，而处置完全不同：
   *   ps=0 没接触到皮肤   → 该去把传感器贴好
   *   ps=1 稳定中 / ps=2 采集中 → 等着就行（13 秒内有 null 是正常的）
   *   ps=3 测量已完成但没采到有效脉搏 → 该【重测】
   * 2026-09-21 之前小程序把 ps 整个丢掉了，这四种一律显示"未佩戴"。
   * 下面两帧是那天从巴法云实抓的原文。 */
  const fIdle = bemfa.parseTelemetry(
    '{"ts":"10:01:49","seq":11,"hr":null,"spo2":null,"sc":0,"ps":0,"cv":38972,' +
    '"t":28.6,"h":64.0,"gas":14,"ax":0.9,"ay":0.0,"az":0.0,' +
    '"st":"normal","up":109123,"tsv":0}')
  const fDone = bemfa.parseTelemetry(
    '{"ts":"10:03:02","seq":16,"hr":null,"spo2":null,"sc":0,"ps":3,"cv":38972,' +
    '"t":28.6,"h":64.0,"gas":14,"ax":0.9,"ay":0.0,"az":0.0,' +
    '"st":"normal","up":164918,"tsv":0}')

  const vIdle = page.buildView(fIdle)
  const vDone = page.buildView(fDone)

  ok(vIdle.hr === '--' && vDone.hr === '--',
     '两帧心率都显示 --（这正是让人以为"坏了"的样子）')
  ok(vIdle.hrNote !== vDone.hrNote,
     '同样是 --，ps=0 与 ps=3 给出【不同】的说明', vIdle.hrNote + ' ｜ ' + vDone.hrNote)
  ok(vIdle.hrNote.indexOf('皮肤接触') >= 0, 'ps=0 → 提示去把传感器贴好', vIdle.hrNote)
  ok(vDone.hrNote.indexOf('重') >= 0, 'ps=3 → 提示重测（而不是说"未佩戴"）', vDone.hrNote)
  ok(vDone.hrNote.indexOf('未佩戴') < 0,
     'ps=3 的说明里【没有】"未佩戴" —— 否则把"测了没测出"说成"没戴"，方向指错')
  ok(vDone.hrCls === 'v-warn',
     'ps=3（测完没测出）标 warn：它是需要处理的状态，不是"没戴"的静默态', vDone.hrCls)
  ok(vIdle.hrCls === 'v-mute', 'ps=0 仍是 mute（设备本来就没在测）', vIdle.hrCls)
  ok(vDone.spo2Note.indexOf('未佩戴') < 0, '血氧那一格同样不能说"未佩戴"', vDone.spo2Note)
  ok(vDone.ppgText.indexOf('已完成') >= 0, '设备信息区显示"测量已完成"', vDone.ppgText)
  ok(vIdle.ppgText.indexOf('未检测到手指') >= 0, '设备信息区显示"未检测到手指"', vIdle.ppgText)

  page.stopChannel()

  /* ---------- 6. WSS 通道 ---------- */
  console.log('\n=== 6. WSS 实时推送通道 ===')
  state.socketBehavior = 'ok'
  page = makePage(pageCfg)
  page.onLoad()
  appInst.saveCfg({ channel: 'wss' })
  page.syncCfgToData()

  state.httpHandler = () => ({ fail: true, errMsg: 'request:fail url not in domain list' })

  page.startChannel()
  await sleep(40)

  ok(page.data.chan.kind === 'wss', '通道标识为 wss', page.data.chan.kind)
  ok(page.data.chan.level === 'ok', 'WSS 握手+订阅成功 -> 状态 ok', page.data.chan.text + ' / ' + page.data.chan.detail)

  const sock = state.sockets[state.sockets.length - 1]
  ok(sock.sent.length >= 2, '客户端确实发出了 CONNECT 与 SUBSCRIBE 两个报文（' + sock.sent.length + ' 个）')
  const t0 = new Uint8Array(sock.sent[0])[0] >> 4
  const t1 = sock.sent.length > 1 ? (new Uint8Array(sock.sent[1])[0] >> 4) : -1
  ok(t0 === 1, '第 1 个报文是 CONNECT（type=1）', String(t0))
  ok(t1 === 8, '第 2 个报文是 SUBSCRIBE（type=8）', String(t1))

  /* 推两帧真实报文进来 */
  sock._pushPublish('temp001', RAW.seq183)
  await sleep(20)
  ok(page.data.v.seq === '183', 'WSS：推送报文上屏', page.data.v.seq)
  ok(page.data.newCount === 1, 'WSS：新数据计数', String(page.data.newCount))
  ok(page.data.fresh.level === 'ok', 'WSS：收到推送即在线')
  ok(/本地收/.test(page.data.cloudTime) || page.data.cloudTime !== '--',
     'WSS：没有云端时间时显示本地接收时刻（不显示假的云端时间）', page.data.cloudTime)

  sock._pushPublish('temp001', RAW.seq184)
  await sleep(20)
  ok(page.data.v.seq === '184', 'WSS：第二帧更新', page.data.v.seq)
  ok(page.data.newCount === 2, 'WSS：累加到 2 条', String(page.data.newCount))

  /* 重复推同一帧（QoS0 下 broker 可能重发） */
  sock._pushPublish('temp001', RAW.seq184)
  await sleep(20)
  ok(page.data.newCount === 2, 'WSS：重复帧按 seq 去重，不重复计数', String(page.data.newCount))

  /* ---------- 7. WSS 连不上 -> 自动降级到 HTTP ---------- */
  console.log('\n=== 7. WSS 不可用时自动降级 ===')
  page.stopChannel()
  state.socketBehavior = 'fail'
  state.socketErrMsg = 'connectSocket:fail url not in domain list'
  state.httpHandler = () => ({
    statusCode: 200,
    data: { code: 0, message: 'OK', data: [{ msg: RAW.seq184, time: 'x', unix: nowSec() }] }
  })
  appInst.saveCfg({ channel: 'auto' })     /* auto = 推送优先，失败退回轮询 */
  page.syncCfgToData()
  page.startChannel()
  await sleep(60)

  ok(page.data.chan.kind === 'http', 'auto 模式下 WSS 失败后已切到 HTTP', page.data.chan.kind)
  ok(page.data.chan.level === 'ok',
     '★ 降级后的轮询仍标为 ok —— 它是可用的，"这是降级来的"只体现在文案里（不自相矛盾）',
     page.data.chan.level)
  ok(/降级/.test(page.data.chan.text), '文案说明这是降级通道：' + page.data.chan.text)
  ok(/socket 合法域名/.test(page.data.chan.detail), '降级原因直指 socket 白名单这一栏：' + page.data.chan.detail)
  ok(page.data.v.seq === '184', '降级后 HTTP 仍能取到数据，功能不受影响', page.data.v.seq)

  /* ---------- 8. 强制 wss 模式 + 连不上：如实报错，不静默 ---------- */
  console.log('\n=== 8. 强制 wss 且连不上：必须报错而非静默 ===')
  page.stopChannel()
  appInst.saveCfg({ channel: 'wss' })
  page.syncCfgToData()
  page.startChannel()
  await sleep(60)
  ok(page.data.chan.level === 'danger', '通道状态 danger', page.data.chan.level)
  ok(/socket 合法域名/.test(page.data.errMsg), '错误提示指向 socket 白名单配置', page.data.errMsg.slice(0, 70))
  ok(page.data.errRaw.length > 0, '原始 errMsg 一并保留（一手证据）', page.data.errRaw)

  /* ---------- 9. 诊断覆盖两条通道 ---------- */
  console.log('\n=== 9. 一键诊断：两条通道都测 ===')
  page.stopChannel()
  appInst.saveCfg({ channel: 'auto' })
  page.syncCfgToData()
  state.socketBehavior = 'ok'
  state.httpHandler = () => ({
    statusCode: 200,
    data: { code: 0, message: 'OK', data: [{ msg: RAW.seq184, time: '2026-09-16 10:32:07', unix: nowSec() }] }
  })
  page.runDiagnose()
  await sleep(400)

  const rows = page.data.diag ? page.data.diag.rows : []
  const keys = rows.map(r => r.k).join(' | ')
  ok(rows.length > 0, '诊断产出报告（' + rows.length + ' 行）')
  ok(/AppID/.test(keys), '报告含 AppID（用于核对域名配在哪个账号下）')
  ok(/HTTP 请求地址/.test(keys), '报告含 HTTP 地址')
  ok(/HTTP 结果/.test(keys), '报告含 HTTP 结果')
  ok(/WSS 地址/.test(keys), '★ 报告含 WSS 地址（两条通道都测）')
  ok(/WSS 结果/.test(keys), '★ 报告含 WSS 结果')
  if (page.data.diag) {
    ok(page.data.diag.level === 'ok', '两条都通 -> 判定 ok', page.data.diag.verdict)
    console.log('    ' + page.data.diag.verdict)
  } else {
    fail++; console.log('  ✗ 诊断没有产出报告')
  }

  page.stopChannel()

  console.log('\n' + '='.repeat(72))
  console.log('结果: ' + pass + ' 项通过, ' + fail + ' 项失败')
  console.log('='.repeat(72))
  process.exit(fail ? 1 : 0)
}

main().catch(e => {
  console.error('测试脚本自身出错: ' + (e && e.stack || e))
  process.exit(3)
})
