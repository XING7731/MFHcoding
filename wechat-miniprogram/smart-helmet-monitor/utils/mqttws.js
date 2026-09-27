/* =========================================================================
 * mqttws.js —— 极简 MQTT 3.1.1 over WebSocket 客户端（巴法云小程序通道）
 *
 * 【为什么手写而不装 mqtt.js】
 *   小程序里装 npm 包要额外"构建 npm"，多一道容易出错的步骤；而 MQTT 3.1.1
 *   的报文格式很小，CONNECT / SUBSCRIBE / PINGREQ / PUBLISH 四五个包就够用，
 *   手写约 200 行且完全可控 —— 出问题时能直接读代码定位，不用翻第三方库。
 *
 * 【为什么要有这条通道】
 *   小程序有两条取数路：
 *     ① HTTP 轮询  apis.bemfa.com/va/getmsg  -> 需要 request 合法域名
 *     ② WSS 推送   wss://bemfa.com:9504/wss   -> 需要 socket 合法域名
 *   这两个白名单是【互相独立】的配置项。只配了一条时，另一条必然失败 ——
 *   多一条通道就少一个单点故障，且 ② 是真正的实时推送（无需轮询）。
 *
 * 【巴法云对该端点的约定】
 *   - 地址   : wss://bemfa.com:9504/wss
 *   - ClientID = 用户的私钥(UID)，与设备端 MQTT 用的是同一个值
 *   - 订阅主题后即可收到该主题的推送，payload 就是设备发的原始报文
 *
 * ⚠️【同 UID 互踢】巴法云同一私钥只允许一个客户端在线。小程序用 uid 作
 *    ClientID 连接，【有可能把板子顶下线】。因此本模块默认不启用，只有在
 *    用户明确知道风险并接受时才打开（见 index.js 的 useWss 开关）。
 *    实际是否互踢与平台策略有关，务必以实测为准（tools/wxmp_wss_test.js）。
 * ========================================================================= */

/* ------------------------- 报文编解码基础 ------------------------- */

/* UTF-8 编码。不依赖 TextEncoder（部分基础库没有），手写保证可用。
 * ClientID 是 32 位十六进制、主题名是 ASCII，本来就落在 ASCII 区间；
 * 但完整实现可以保证主题名含中文时也不出错。 */
function utf8Bytes(str) {
  const s = String(str == null ? '' : str)
  const out = []
  for (let i = 0; i < s.length; i++) {
    let c = s.charCodeAt(i)
    if (c < 0x80) {
      out.push(c)
    } else if (c < 0x800) {
      out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f))
    } else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      /* 代理对 -> 合成一个码点 */
      const c2 = s.charCodeAt(i + 1)
      if (c2 >= 0xdc00 && c2 <= 0xdfff) {
        c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00)
        i++
        out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f),
                 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f))
      } else {
        out.push(0xef, 0xbf, 0xbd)   /* 孤立高位代理 -> U+FFFD */
      }
    } else {
      out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f))
    }
  }
  return out
}

/* UTF-8 解码（payload 解密用） */
function utf8Decode(bytes) {
  let out = ''
  let i = 0
  while (i < bytes.length) {
    const b = bytes[i]
    let cp
    if (b < 0x80) { cp = b; i += 1 }
    else if ((b & 0xe0) === 0xc0) { cp = ((b & 0x1f) << 6) | (bytes[i + 1] & 0x3f); i += 2 }
    else if ((b & 0xf0) === 0xe0) {
      cp = ((b & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f); i += 3
    } else {
      cp = ((b & 0x07) << 18) | ((bytes[i + 1] & 0x3f) << 12) |
           ((bytes[i + 2] & 0x3f) << 6) | (bytes[i + 3] & 0x3f); i += 4
    }
    if (cp > 0xffff) {
      cp -= 0x10000
      out += String.fromCharCode(0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff))
    } else {
      out += String.fromCharCode(cp)
    }
  }
  return out
}

/* MQTT 的"剩余长度"是变长编码：每字节低 7 位有效，高位表示后面还有 */
function encodeRemainingLength(len) {
  const out = []
  let n = len
  do {
    let d = n % 128
    n = Math.floor(n / 128)
    if (n > 0) d |= 0x80
    out.push(d)
  } while (n > 0)
  return out
}

function decodeRemainingLength(buf, pos) {
  let value = 0
  let mult = 1
  let p = pos
  let byte
  do {
    if (p >= buf.length) return null          /* 还没收全 */
    byte = buf[p++]
    value += (byte & 0x7f) * mult
    mult *= 128
    if (mult > 128 * 128 * 128) return null
  } while (byte & 0x80)
  return { value: value, next: p }
}

/* 把 [0x00, 0x04, 'M','Q','T','T'] 里的长度前缀 + 字节串拼出来 */
function mqttStr(str) {
  const b = utf8Bytes(str)
  return [((b.length >> 8) & 0xff), (b.length & 0xff)].concat(b)
}

/* 拼完整报文：固定头 + 剩余长度 + 内容 */
function packet(header, body) {
  return new Uint8Array([header].concat(encodeRemainingLength(body.length), body))
}

/* ------------------------- 报文构造 ------------------------- */

/* CONNECT：协议名 MQTT / 级别 4(3.1.1) / cleanSession / keepalive / ClientID */
function buildConnect(clientId, keepAliveSec) {
  const body = []
    .concat([0x00, 0x04, 0x4d, 0x51, 0x54, 0x54])   /* "MQTT" */
    .concat([0x04])                                  /* 协议级别 3.1.1 */
    .concat([0x02])                                  /* flags: clean session */
    .concat([(keepAliveSec >> 8) & 0xff, keepAliveSec & 0xff])
    .concat(mqttStr(clientId))
  return packet(0x10, body)
}

/* SUBSCRIBE：报文标识符 + 主题过滤器 + 请求 QoS */
function buildSubscribe(topic, packetId) {
  const body = []
    .concat([(packetId >> 8) & 0xff, packetId & 0xff])
    .concat(mqttStr(topic))
    .concat([0x00])                                  /* QoS 0 */
  return packet(0x82, body)
}

function buildPingReq() {
  return new Uint8Array([0xc0, 0x00])
}

function buildDisconnect() {
  return new Uint8Array([0xe0, 0x00])
}

/* ------------------------- 报文解析 ------------------------- */

const TYPE_NAMES = {
  1: 'CONNECT', 2: 'CONNACK', 3: 'PUBLISH', 4: 'PUBACK', 5: 'PUBREC',
  6: 'PUBREL', 7: 'PUBCOMP', 8: 'SUBSCRIBE', 9: 'SUBACK', 10: 'UNSUBSCRIBE',
  11: 'UNSUBACK', 12: 'PINGREQ', 13: 'PINGRESP', 14: 'DISCONNECT'
}

const CONNACK_CODES = {
  0: '连接成功',
  1: '协议版本不被服务端接受',
  2: 'ClientID 被拒绝',
  3: '服务端不可用',
  4: '用户名或密码错误',
  5: '未授权（检查私钥/UID 是否正确）'
}

/* 把累积缓冲里的完整报文一条条切出来。返回 { packets, rest } */
function splitPackets(buf) {
  const packets = []
  let pos = 0
  while (pos < buf.length) {
    const h = decodeRemainingLength(buf, pos + 1)
    if (!h) break
    const total = (h.next - pos) + h.value
    if (pos + total > buf.length) break           /* 半包，等下次 */
    packets.push({
      header: buf[pos],
      type: buf[pos] >> 4,
      body: buf.slice(h.next, h.next + h.value)
    })
    pos += total
  }
  return { packets: packets, rest: buf.slice(pos) }
}

/* PUBLISH 的 body = 主题(2字节长度前缀) + 载荷 */
function parsePublish(body) {
  const tlen = (body[0] << 8) | body[1]
  const topic = utf8Decode(body.slice(2, 2 + tlen))
  const payload = utf8Decode(body.slice(2 + tlen))
  return { topic: topic, payload: payload }
}

/* ------------------------- 客户端 ------------------------- */

/**
 * 建立一条到巴法云的 MQTT-over-WSS 连接。
 *
 * @param {Object} o
 *   url           连接地址，默认 wss://bemfa.com:9504/wss
 *   clientId      MQTT ClientID，巴法云要求填私钥(UID)
 *   topic         要订阅的主题（设备往这个主题上报）
 *   keepAliveSec  心跳间隔，默认 60
 *   socketFactory 可选。签名 (url) => socket 适配器。不传则用小程序 wx.connectSocket。
 *                 适配器需实现: send(data) / close() /
 *                 以及 onOpen / onMessage / onClose / onError 四个回调注册。
 *   onStatus      (statusText, detail) 连接过程上报，用于界面显示
 *   onMessage     (payload, topic) 收到设备报文
 *   onError       (errText)
 *   reconnect     断线是否自动重连，默认 true
 *
 * @returns 客户端对象，含 close()
 */
function connect(o) {
  const opt = o || {}
  const url = opt.url || 'wss://bemfa.com:9504/wss'
  const clientId = opt.clientId || ''
  const topic = opt.topic || ''
  const keepAlive = opt.keepAliveSec || 60
  const onStatus = opt.onStatus || function () {}
  const onMessage = opt.onMessage || function () {}
  const onError = opt.onError || function () {}
  const wantReconnect = opt.reconnect !== false

  let sock = null
  let buf = new Uint8Array(0)
  let pingTimer = null
  let reconnectTimer = null
  let closedByUser = false
  let connected = false

  function concat(a, b) {
    const out = new Uint8Array(a.length + b.length)
    out.set(a, 0)
    out.set(b, a.length)
    return out
  }

  /* 收到字节流 -> 拆包 -> 分发 */
  function feed(bytes) {
    buf = concat(buf, bytes)
    const r = splitPackets(buf)
    buf = r.rest
    for (let i = 0; i < r.packets.length; i++) {
      handlePacket(r.packets[i])
    }
  }

  function handlePacket(p) {
    if (p.type === 2) {                                   /* CONNACK */
      const rc = p.body[1]
      if (rc === 0) {
        connected = true
        onStatus('MQTT 已连接', TYPE_NAMES[2])
        sock.send(buildSubscribe(topic, 1))
        startPing()
      } else {
        onError('MQTT 连接被拒绝：' + (CONNACK_CODES[rc] || ('返回码 ' + rc)))
      }
    } else if (p.type === 9) {                            /* SUBACK */
      onStatus('已订阅 ' + topic, 'SUBACK')
    } else if (p.type === 3) {                            /* PUBLISH */
      const m = parsePublish(p.body)
      onMessage(m.payload, m.topic)
    } else if (p.type === 13) {                           /* PINGRESP */
      /* 心跳有回应即说明链路活着，无需额外处理 */
    }
  }

  function startPing() {
    stopPing()
    pingTimer = setInterval(function () {
      if (sock && connected) {
        try { sock.send(buildPingReq()) } catch (e) { /* 下一轮重连兜底 */ }
      }
    }, Math.max(10, keepAlive - 10) * 1000)
  }

  function stopPing() {
    if (pingTimer) { clearInterval(pingTimer); pingTimer = null }
  }

  function scheduleReconnect() {
    if (!wantReconnect || closedByUser || reconnectTimer) return
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null
      if (!closedByUser) open()
    }, 5000)
  }

  function teardown() {
    connected = false
    stopPing()
    if (sock) {
      try { sock.close() } catch (e) {}
      sock = null
    }
  }

  function open() {
    teardown()
    buf = new Uint8Array(0)
    onStatus('正在连接 ' + url, 'CONNECTING')

    const factory = opt.socketFactory || defaultFactory
    sock = factory(url)

    sock.onOpen(function () {
      onStatus('WebSocket 已打开，发送 MQTT CONNECT', 'WS_OPEN')
      try {
        sock.send(buildConnect(clientId, keepAlive))
      } catch (e) {
        onError('发送 CONNECT 失败：' + ((e && e.message) || e))
      }
    })

    sock.onMessage(function (ev) {
      /* 【必须兼容两种形态】小程序 SocketTask.onMessage 回调收到的是
       * { data: ... } 这样的对象；而 Node 的 WebSocket 直接把数据给你。
       * 第一版只按后者的形态写，结果真机上整个协议层静默不动 ——
       * 帧收到了却一直不解析，表现为"连接中"卡住。这里统一拆一层。 */
      const data = (ev && typeof ev === 'object' && ev.data !== undefined) ? ev.data : ev

      if (data instanceof ArrayBuffer) {
        feed(new Uint8Array(data))
      } else if (data && data.byteLength !== undefined) {
        feed(new Uint8Array(data))
      } else if (typeof data === 'string') {
        onError('收到文本帧（应为二进制 MQTT 帧），忽略：' + data.slice(0, 60))
      } else {
        onError('收到无法识别的帧类型：' + Object.prototype.toString.call(data))
      }
    })

    sock.onClose(function (reason) {
      onStatus('连接关闭，5 秒后重连', String(reason || ''))
      connected = false
      stopPing()
      scheduleReconnect()
    })

    sock.onError(function (err) {
      const msg = (err && (err.errMsg || err.message)) || String(err)
      onError('WebSocket 错误：' + msg)
    })
  }

  /* 小程序环境的 socket 适配器：把 wx.connectSocket 的 SocketTask 包成统一接口 */
  function defaultFactory(u) {
    const task = wx.connectSocket({ url: u })
    return {
      send: function (data) {
        task.send({
          data: data.buffer ? data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) : data
        })
      },
      close: function () { task.close({ code: 1000 }) },
      onOpen: function (cb) { task.onOpen(cb) },
      onMessage: function (cb) {
        /* 关键：必须显式要求二进制帧，否则 MQTT 帧会被当文本处理而乱码 */
        try { task.binaryType = 'arraybuffer' } catch (e) {}
        task.onMessage(cb)
      },
      onClose: function (cb) { task.onClose(cb) },
      onError: function (cb) { task.onError(cb) }
    }
  }

  open()

  return {
    close: function () {
      closedByUser = true
      if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null }
      if (sock && connected) {
        try { sock.send(buildDisconnect()) } catch (e) {}
      }
      teardown()
      onStatus('已主动断开', 'CLOSED')
    },
    isConnected: function () { return connected },
    url: url,
    topic: topic,
    /* 供离线环境（Node 测试）注入自己的 socket 适配器 */
    _refeed: feed
  }
}

module.exports = {
  connect: connect,
  DEFAULT_URL: 'wss://bemfa.com:9504/wss',
  /* 导出内部函数便于单元测试：报文编解码是最容易写错的地方 */
  _internal: {
    utf8Bytes: utf8Bytes,
    utf8Decode: utf8Decode,
    encodeRemainingLength: encodeRemainingLength,
    decodeRemainingLength: decodeRemainingLength,
    buildConnect: buildConnect,
    buildSubscribe: buildSubscribe,
    buildPingReq: buildPingReq,
    splitPackets: splitPackets,
    parsePublish: parsePublish,
    CONNACK_CODES: CONNACK_CODES
  }
}
