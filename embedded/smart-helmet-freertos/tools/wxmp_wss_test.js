/* =========================================================================
 * wxmp_wss_test.js —— 验证 utils/mqttws.js（小程序 WSS 通道）
 *
 * 做三件事：
 *   ① 报文编解码单元测试（最容易写错的地方，先证明字节级正确）
 *   ② 用 Node 22 原生 WebSocket 真连 wss://bemfa.com:9504/wss，订阅 temp001，
 *      看能不能收到板子实时推送的报文
 *   ③ 探测"同 UID 互踢"：连接前后各用 HTTP 接口读一次板子的 seq，
 *      若期间 seq 仍在增长 -> 板子没被顶下线；若卡住不动 -> 确认互踢
 *
 * 运行: node wxmp_wss_test.js
 * ========================================================================= */

const path = require('path')

const M = 'C:/Users/27362/Desktop/WeChat smart helmet'
const mqttws = require(path.join(M, 'utils', 'mqttws.js'))
const I = mqttws._internal

const UID = 'd2f5cf227188d137f8171da8611a7614'
const TOPIC = 'temp001'
const WSS_URL = mqttws.DEFAULT_URL
const HTTP_URL = 'https://apis.bemfa.com/va/getmsg?uid=' + UID + '&topic=' + TOPIC + '&type=1&num=1'

let pass = 0, fail = 0
function ok(cond, name, extra) {
  if (cond) { pass++; console.log('  ✓ ' + name) }
  else { fail++; console.log('  ✗ ' + name + (extra ? '  -> ' + extra : '')) }
}

/* -------------------- ① 报文编解码 -------------------- */
function unitTests() {
  console.log('\n=== ① 报文编解码 ===')

  /* 剩余长度变长编码：MQTT 规范里的经典边界值 */
  const rl = [
    [0, [0x00]],
    [127, [0x7f]],
    [128, [0x80, 0x01]],
    [16383, [0xff, 0x7f]],
    [16384, [0x80, 0x80, 0x01]],
    [2097151, [0xff, 0xff, 0x7f]]
  ]
  rl.forEach(function (c) {
    const got = I.encodeRemainingLength(c[0])
    ok(JSON.stringify(got) === JSON.stringify(c[1]),
       '剩余长度 ' + c[0] + ' 编码正确', JSON.stringify(got))
    const dec = I.decodeRemainingLength(new Uint8Array(c[1]), 0)
    ok(dec && dec.value === c[0], '剩余长度 ' + c[0] + ' 解码回自身')
  })

  /* 不完整输入必须返回 null（不能假装解出来了） */
  ok(I.decodeRemainingLength(new Uint8Array([0x80]), 0) === null,
     '剩余长度只有续字节时返回 null（等后续数据）')

  /* UTF-8：ASCII / 中文 / 边界 */
  const enc = I.utf8Bytes('temp001')
  ok(enc.length === 7 && enc[0] === 0x74, 'ASCII 编码长度与首字节正确')
  ok(I.utf8Decode(enc) === 'temp001', 'ASCII 往返一致')
  const cn = I.utf8Bytes('安全帽')
  ok(cn.length === 9, '中文 3 字 -> 9 字节（含主题名带中文的情况）')
  ok(I.utf8Decode(cn) === '安全帽', '中文往返一致')

  /* CONNECT 报文逐字节核对 */
  const c = I.buildConnect('a1b2', 60)
  ok(c[0] === 0x10, 'CONNECT 固定头 0x10')
  /* 真实结构：0x10,RL,0x00,0x04,'M','Q','T','T',0x04,flags,ka_hi,ka_lo,... */
  const h = I.decodeRemainingLength(c, 1)
  ok(c[2] === 0x00 && c[3] === 0x04 &&
     String.fromCharCode(c[4], c[5], c[6], c[7]) === 'MQTT',
     '协议名 "MQTT" 位置正确', String.fromCharCode(c[4], c[5], c[6], c[7]))
  ok(c[8] === 0x04, '协议级别 = 4（MQTT 3.1.1）')
  ok(c[9] === 0x02, '连接标志 = 0x02（clean session）')
  ok(((c[10] << 8) | c[11]) === 60, 'keepalive = 60s')
  const cidLen = (c[12] << 8) | c[13]
  ok(cidLen === 4 && I.utf8Decode(c.slice(14, 18)) === 'a1b2', 'ClientID 正确写入')
  ok(h.value === c.length - (h.next), '剩余长度字段与实际载荷长度自洽')

  /* SUBSCRIBE 报文 */
  const s = I.buildSubscribe(TOPIC, 1)
  ok(s[0] === 0x82, 'SUBSCRIBE 固定头 0x82')
  const sh = I.decodeRemainingLength(s, 1)
  ok(s[2] === 0x00 && s[3] === 0x01, '报文标识符 = 1')
  const tlen = (s[4] << 8) | s[5]
  ok(tlen === 7 && I.utf8Decode(s.slice(6, 13)) === TOPIC, '订阅主题写入正确')
  ok(s[s.length - 1] === 0x00, '请求 QoS = 0')
  ok(sh.value === s.length - sh.next, 'SUBSCRIBE 剩余长度自洽')

  /* PINGREQ / DISCONNECT */
  ok(I.buildPingReq().length === 2 && I.buildPingReq()[0] === 0xc0, 'PINGREQ = C0 00')

  /* 粘包/半包切分 —— 真实 socket 上必然遇到 */
  const p1 = I.buildPingReq()
  const p2 = I.buildPingReq()
  const merged = new Uint8Array(p1.length + p2.length)
  merged.set(p1, 0); merged.set(p2, p1.length)
  const sp = I.splitPackets(merged)
  ok(sp.packets.length === 2 && sp.rest.length === 0, '两个粘在一起的包被正确切开')
  const half = I.splitPackets(p1.slice(0, 1))
  ok(half.packets.length === 0 && half.rest.length === 1, '半包不吐出、留在缓冲等后续')

  /* PUBLISH 解析：造一个真实的 PUBLISH 包 */
  const topicBytes = I.utf8Bytes(TOPIC)
  const payloadBytes = I.utf8Bytes('{"seq":7,"hr":72}')
  const body = []
    .concat([(topicBytes.length >> 8) & 0xff, topicBytes.length & 0xff])
    .concat(topicBytes)
    .concat(payloadBytes)
  const pub = I.splitPackets(new Uint8Array([0x30].concat(I.encodeRemainingLength(body.length), body)))
  ok(pub.packets.length === 1 && pub.packets[0].type === 3, 'PUBLISH 包被识别为 type=3')
  const parsed = I.parsePublish(pub.packets[0].body)
  ok(parsed.topic === TOPIC, 'PUBLISH 主题解析正确')
  ok(parsed.payload === '{"seq":7,"hr":72}', 'PUBLISH 载荷解析正确', parsed.payload)
}

/* -------------------- ② ③ 真连 WSS -------------------- */
function httpSeq() {
  return new Promise(function (resolve) {
    const ctrl = new AbortController()
    const t = setTimeout(function () { ctrl.abort() }, 12000)
    fetch(HTTP_URL, { signal: ctrl.signal })
      .then(function (r) { return r.text() })
      .then(function (txt) {
        clearTimeout(t)
        try {
          const j = JSON.parse(txt)
          const it = j.data && j.data[0]
          if (!it) return resolve(null)
          const m = JSON.parse(it.msg)
          resolve({ seq: m.seq, unix: it.unix, msg: it.msg })
        } catch (e) { resolve(null) }
      })
      .catch(function () { clearTimeout(t); resolve(null) })
  })
}

function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms) }) }

/* Node 环境的 socket 适配器：用 Node 22 内置的 WebSocket，接口与小程序适配器一致 */
function nodeSocketFactory(url) {
  const ws = new WebSocket(url)
  ws.binaryType = 'arraybuffer'
  const handlers = { open: [], message: [], close: [], error: [] }
  ws.addEventListener('open', function () { handlers.open.forEach(function (f) { f() }) })
  ws.addEventListener('message', function (ev) {
    handlers.message.forEach(function (f) { f(ev.data) })
  })
  ws.addEventListener('close', function (ev) {
    handlers.close.forEach(function (f) { f('code=' + ev.code + ' ' + (ev.reason || '')) })
  })
  ws.addEventListener('error', function (ev) {
    handlers.error.forEach(function (f) { f((ev && ev.message) || 'websocket error') })
  })
  return {
    send: function (data) { ws.send(data) },
    close: function () { try { ws.close() } catch (e) {} },
    onOpen: function (cb) { handlers.open.push(cb) },
    onMessage: function (cb) { handlers.message.push(cb) },
    onClose: function (cb) { handlers.close.push(cb) },
    onError: function (cb) { handlers.error.push(cb) }
  }
}

async function liveTest() {
  console.log('\n=== ② 真连 WSS 收实时推送 ===')
  console.log('  地址: ' + WSS_URL)
  console.log('  主题: ' + TOPIC + '   ClientID(=UID): ' + UID.slice(0, 8) + '…')

  const before = await httpSeq()
  console.log('  连接前 HTTP 侧读到: ' + (before ? ('seq=' + before.seq) : '(读不到)'))

  const got = []
  const statusLog = []
  let errText = ''

  const cli = mqttws.connect({
    url: WSS_URL,
    clientId: UID,
    topic: TOPIC,
    keepAliveSec: 60,
    socketFactory: nodeSocketFactory,
    onStatus: function (s, d) { statusLog.push(s); console.log('    [status] ' + s + (d ? ' (' + d + ')' : '')) },
    onMessage: function (payload, topic) {
      got.push({ t: Date.now(), topic: topic, payload: payload })
      console.log('    [push ] ' + topic + ' -> ' + String(payload).slice(0, 150))
    },
    onError: function (e) { errText += e + ' | '; console.log('    [error] ' + e) },
    reconnect: false
  })

  /* 板子 10 秒一条，等 35 秒应至少收到 3 条 */
  const WAIT_MS = 35000
  console.log('  等待 ' + (WAIT_MS / 1000) + ' 秒接收推送…')
  await sleep(WAIT_MS)

  const after = await httpSeq()
  console.log('  接收期间 HTTP 侧读到: ' + (after ? ('seq=' + after.seq) : '(读不到)'))

  cli.close()

  console.log('\n  --- 判定 ---')
  ok(got.length > 0, 'WSS 通道能收到板子推送（收到 ' + got.length + ' 条）',
     errText.slice(0, 160) || '无数据')

  if (got.length > 0) {
    const m = JSON.parse(got[0].payload)
    ok(typeof m.seq === 'number', '推送载荷是固件的紧凑 JSON（能解析出 seq=' + m.seq + '）')
    const seqs = got.map(function (g) { return JSON.parse(g.payload).seq }).filter(function (v) { return typeof v === 'number' })
    const mono = seqs.every(function (v, i) { return i === 0 || v >= seqs[i - 1] })
    ok(mono, '推送 seq 单调递增: ' + seqs.join(' → '))
  }

  /* 互踢判定：连接前 → 连接后 HTTP 侧 seq 仍在长，说明板子没被顶掉 */
  if (before && after) {
    const grew = after.seq > before.seq
    ok(grew, '【互踢探测】连接 WSS 期间板子仍在正常上报（seq ' + before.seq + ' → ' + after.seq + '）',
       'seq 没有增长，说明 WSS 用同 UID 会把板子顶下线')
    console.log('    注：seq ' + before.seq + ' → ' + after.seq +
                '，间隔约 ' + (WAIT_MS / 1000) + 's，板子周期 10s，理论上应 +3 左右')
  } else {
    console.log('    ⚠ HTTP 侧读数不全，无法判定互踢，需再测一次')
  }

  return { got: got, statusLog: statusLog, errText: errText }
}

async function main() {
  console.log('='.repeat(72))
  console.log('utils/mqttws.js 验证')
  console.log('='.repeat(72))

  if (typeof WebSocket === 'undefined') {
    console.log('✗ 当前 Node 没有内置 WebSocket，无法做真连测试（需 Node 21+）')
    process.exit(2)
  }

  unitTests()
  await liveTest()

  console.log('\n' + '='.repeat(72))
  console.log('结果: ' + pass + ' 项通过, ' + fail + ' 项失败')
  console.log('='.repeat(72))
  process.exit(fail ? 1 : 0)
}

main().catch(function (e) {
  console.error('测试脚本自身出错: ' + (e && e.stack || e))
  process.exit(3)
})
