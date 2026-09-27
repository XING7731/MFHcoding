#!/usr/bin/env node
/* =========================================================================
 * wxmp_bemfa_test.js —— 校验小程序 utils/bemfa.js 的解析与判定逻辑
 *
 * 为什么要有这个：小程序的逻辑一旦写错，只能靠"跑到手机上翻车"才发现。
 * 这里用 Node 模拟小程序的 wx.* 环境，拿【真实抓到的报文】把纯逻辑部分
 * 跑一遍，包括最容易写错的"按 seq 去重"。
 *
 * 用法（Node 任意较新版本）：
 *   node wxmp_bemfa_test.js "C:/Users/xxx/Desktop/WeChat smart helmet/utils/bemfa.js"
 * 不传参数则用默认路径。
 * ========================================================================= */

const path = require('path')

const target = process.argv[2] ||
  'C:/Users/27362/Desktop/WeChat smart helmet/utils/bemfa.js'

/* ---------- 模拟小程序的全局对象 ---------- */
const store = {}
global.wx = {
  getStorageSync(k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : '' },
  setStorageSync(k, v) { store[k] = JSON.parse(JSON.stringify(v)) },
  removeStorageSync(k) { delete store[k] },
  request() { throw new Error('不该在纯逻辑测试里发网络请求') }
}

const bemfa = require(path.resolve(target))

let pass = 0, fail = 0
function ok(cond, name, extra) {
  if (cond) { pass++; console.log('  ✓ ' + name) }
  else { fail++; console.log('  ✗ ' + name + (extra ? '   → ' + extra : '')) }
}
function eq(a, b, name) {
  ok(a === b, name, 'got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b))
}

/* ---------- 真实报文（2026-09-16 从巴法云接口实抓） ---------- */
const REAL = '{"ts":"10:02:28","seq":14,"hr":null,"spo2":null,"t":26.4,"h":58.0,' +
             '"gas":21,"ax":3.9,"ay":0.2,"az":0.6,"st":"normal","up":148245,"tsv":0}'

console.log('\n=== 1. 解析真实报文 ===')
const d = bemfa.parseTelemetry(REAL)
eq(d.seq, 14, 'seq = 14')
eq(d.temp, 26.4, '温度 t = 26.4')
eq(d.humi, 58.0, '湿度 h = 58.0')
eq(d.gas, 21, '气体 gas = 21')
/* 2026-09-22 光照整条链路已删除（docs/39），d.lux 必须不存在 */
eq(d.lux, undefined, '光照 lux 已删除 → 解析结果为 undefined')
eq(d.hr, null, '心率 hr = null（未佩戴）')
eq(d.spo2, null, '血氧 spo2 = null')
eq(d.up, 148245, '运行时长 up = 148245')
eq(d.st, 'normal', '状态 st = normal')
ok(Math.abs(d.accel - Math.sqrt(3.9 * 3.9 + 0.2 * 0.2 + 0.6 * 0.6)) < 1e-6,
   '合加速度 |a| 由三轴算出', String(d.accel))

console.log('\n=== 2. 异常报文必须被挡住（不能静默显示错值） ===')
try { bemfa.parseTelemetry('{坏JSON'); ok(false, '非 JSON 应抛错') }
catch (e) { ok(true, '非 JSON 抛错：' + e.message.slice(0, 34) + '…') }

try { bemfa.parseTelemetry('{"seq":9,"ovf":1}'); ok(false, 'ovf 降级报文应抛错') }
catch (e) { ok(true, 'ovf 降级报文抛错：' + e.message.slice(0, 30) + '…') }

console.log('\n=== 3. 十二种状态都要认得（对应固件 alarm_text_ascii） ===')
const STATES = [
  ['normal',      'ok'],
  ['FALL ALARM',  'danger'],
  ['GAS ALARM',   'danger'],
  ['SOS ALARM',   'danger'],
  ['HR ABNORMAL', 'warn'],
  ['SPO2 LOW',    'warn'],
  ['TEMP HIGH',   'warn'],
  ['TEMP LOW',    'warn'],
  ['WIFI FAIL',   'warn'],
  ['SENSOR ERR',  'warn'],
  ['unknown',     'info'],
  ['',            'warn']
]
STATES.forEach(function (p) {
  const r = bemfa.classifyStatus(p[0])
  eq(r.level, p[1], '「' + (p[0] || '(空)') + '」→ ' + p[1] + ' / ' + r.cn)
})

console.log('\n=== 4. 在线/离线判定（阈值：≤30s 在线，≤90s 延迟，>90s 离线） ===')
const now = 1789522050
eq(bemfa.evalFreshness(now - 5, now).level, 'ok',      '5 秒前 → 在线')
eq(bemfa.evalFreshness(now - 30, now).level, 'ok',     '30 秒前 → 仍算在线（边界）')
eq(bemfa.evalFreshness(now - 31, now).level, 'warn',   '31 秒前 → 数据延迟')
eq(bemfa.evalFreshness(now - 90, now).level, 'warn',   '90 秒前 → 仍算延迟（边界）')
eq(bemfa.evalFreshness(now - 91, now).level, 'danger', '91 秒前 → 离线')
eq(bemfa.evalFreshness(0, now).level, 'unknown',       '无 unix → unknown')
ok(bemfa.evalFreshness(now - 7200, now).text.indexOf('2 小时') >= 0,
   '离线时长人类可读', bemfa.evalFreshness(now - 7200, now).text)

console.log('\n=== 5. 阈值提示（与固件 user_config.h 一致） ===')
/* 【这一组是"镜像常量"，必须与固件逐项一致】固件改了而这里没改，表现就是
 * "设备在响、小程序说正常"（或反过来）。2026-09-18 靠 tools/wxmp_thresh_test.js
 * 的逐项核对抓到过两个真错：accelHigh 停在旧的 2.5（固件已是 1.8），以及
 * 光照门限用错口径。2026-09-22 光照门限已随功能整体删除（docs/39）。 */
const T = bemfa.THRESH
eq(T.gas, 50, '气体门限 50')
eq(T.tempHigh, 40, '高温门限 40')
eq(T.tempLow, 10, '低温门限 10')
eq(T.humiHigh, 90, '高湿门限 90')
eq(T.humiLow, 20, '低湿门限 20')
eq(T.hrMin, 50, '心率下限 50')
eq(T.hrMax, 120, '心率上限 120')
eq(T.spo2Min, 90, '血氧下限 90')
eq(T.luxLow, undefined, '光照门限已删除 → THRESH.luxLow 不存在（docs/39）')
eq(T.luxHigh, undefined, '光照门限已删除 → THRESH.luxHigh 不存在（docs/39）')
eq(T.accelHigh, 1.8, '撞击门限 1.8g（= FALL_ACCEL_HIGH_G）')

/* 理想正常报文：静止平放，重力全在 Z 轴 */
const IDEAL = bemfa.parseTelemetry(
  '{"ts":"10:00:00","seq":1,"hr":72,"spo2":98,"t":26.0,"h":55.0,"gas":10,' +
  '"ax":0.0,"ay":0.0,"az":1.0,"st":"normal","up":5000,"tsv":1}'
)
ok(bemfa.checkThresholds(IDEAL).length === 0, '理想正常报文（|a|=1g）不产生任何提示')

/* 实抓报文里 |a| = 3.95g，会触发加速度提示。这不是小程序的 bug，
   而是它在如实反映板子给的数据 —— 顺带把这个现象固化下来：
   |a|/4 ≈ 1.0g，强烈提示 MPU6050 的"量程换算"被放大了 4 倍（详见 docs/21）。 */
const tipsReal = bemfa.checkThresholds(d)
/* 【契约】checkThresholds 返回的是 {key, text} 数组而不是字符串数组 ——
 * key 给 wxml 当 wx:key，也让"这条提示对应哪个量"可被程序判断（阈值设置页
 * 与预警横幅都靠它）。曾经这里断言的是 tipsReal[0].indexOf(...)。 */
ok(tipsReal.length === 1 && tipsReal[0].text.indexOf('合加速度') >= 0,
   '实抓报文能识别出合加速度越限', JSON.stringify(tipsReal))
ok(tipsReal[0] && tipsReal[0].key === 'accel',
   '每条提示都带 key（供 wx:key 与界面定位）', JSON.stringify(tipsReal[0]))
ok(Math.abs(d.accel / 4 - 1.0) < 0.1,
   '实抓 |a| / 4 ≈ 1g → 量程换算疑似放大 4 倍', String(d.accel / 4))
/* 5 项越限：温度 45>40、气体 88>50、心率 150>120、血氧 80<90、|a|=3g>1.8
   （2026-09-22 去掉 lux 后从 6 项变 5 项，见 docs/39） */
ok(bemfa.checkThresholds(bemfa.parseTelemetry(
  '{"seq":1,"t":45,"gas":88,"hr":150,"spo2":80,"ax":0,"ay":0,"az":3.0,"st":"GAS ALARM","up":1000}'
)).length === 5, '超标报文能同时报出 5 项')

/* 温湿度门限也能判 —— 湿度此前只有读数、没有任何判据（2026-09-18 新增） */
const humiHigh = bemfa.parseTelemetry(
  '{"seq":2,"t":26,"h":95,"gas":10,"ax":0,"ay":0,"az":1,"st":"normal","up":1000}')
const hTips = bemfa.checkThresholds(humiHigh)
ok(hTips.length === 1 && hTips[0].key === 'hHi',
   '湿度 95 > 高湿门限 90 → 产生一条湿度提示', JSON.stringify(hTips))

console.log('\n=== 5b. PPG 测量状态（ps）：四种"没读数"必须分开 ===')
/* 【为什么单开一节】设备做的是"一次测量"（贴手 → 稳定 3 秒 → 采集 10 秒 →
 * 出结果并冻结），这 13 秒里上行 hr 必然是 null；再加上"测完了但没采到有效
 * 脉搏"也是 null —— 一共【四种】情况 hr 都是 null，而处置完全不同：
 *   等着就行 / 该去戴好 / 该重测
 * 固件为此每帧都带 ps（app_events.h 的 ppg_st）。但 2026-09-21 之前小程序
 * 把它整个丢掉了，界面一律显示"未佩戴" —— 测了没测出也被说成没戴，方向指错。 */

/* 下面两帧是 2026-09-21 从巴法云实抓的原文，不是编的 */
const psIdle = bemfa.parseTelemetry(
  '{"ts":"10:01:49","seq":11,"hr":null,"spo2":null,"sc":0,"ps":0,"cv":38972,' +
  '"t":28.6,"h":64.0,"gas":14,"ax":0.9,"ay":0.0,"az":0.0,' +
  '"st":"normal","up":109123,"tsv":0}')
const psDone = bemfa.parseTelemetry(
  '{"ts":"10:03:02","seq":16,"hr":null,"spo2":null,"sc":0,"ps":3,"cv":38972,' +
  '"t":28.6,"h":64.0,"gas":14,"ax":0.9,"ay":0.0,"az":0.0,' +
  '"st":"normal","up":164918,"tsv":0}')
eq(psIdle.ps, 0, 'ps 被解析出来：实抓帧 ps=0（没手指）')
eq(psDone.ps, 3, 'ps 被解析出来：实抓帧 ps=3（测量完成）')

const notes = [0, 1, 2, 3].map(function (v) { return bemfa.ppgNote({ ps: v }) })
eq(new Set(notes).size, 4, 'ps=0/1/2/3 给出【四句互不相同】的说明（相同就等于没接）')
ok(notes[0].indexOf('皮肤接触') >= 0, 'ps=0 → "没检测到皮肤接触"', notes[0])
ok(notes[1].indexOf('稳定') >= 0,     'ps=1 → "正在稳定"', notes[1])
ok(notes[2].indexOf('采集') >= 0,     'ps=2 → "正在采集"', notes[2])
ok(notes[3].indexOf('重') >= 0,       'ps=3 → "没采到有效脉搏，重新贴一次"', notes[3])

/* 【本轮问题的根，必须钉死】ps=3 + hr=null = 测量已跑完但判定没测到。
 * 这时候说"未佩戴"会让用户去查佩戴，而真正该做的是重测。 */
ok(bemfa.ppgNote(psDone).indexOf('未佩戴') < 0,
   'ps=3 且没读数时，文案里【不能】出现"未佩戴"', bemfa.ppgNote(psDone))

/* 老固件不带 ps → 退回笼统文案，不能把"缺字段"说成"状态 0（没检测到手指）" */
const noPs = bemfa.parseTelemetry('{"seq":1,"hr":null,"st":"normal","up":100}')
eq(noPs.ps, null, '老固件不带 ps → 解析为 null（关键：不能默认成 0）')
ok(bemfa.ppgNote(noPs).indexOf('未佩戴') >= 0,
   '没有 ps 时退回笼统文案（不能无中生有地说"没检测到手指"）', bemfa.ppgNote(noPs))
ok(bemfa.ppgStateText(null).indexOf('未知') >= 0,
   '状态标签对缺失 ps 说"未知"', bemfa.ppgStateText(null))
eq(bemfa.ppgStateText(2), '设备：采集中', '状态标签 ps=2')

console.log('\n=== 6. 单位换算 / 格式化 ===')
eq(bemfa.fmtUptime(148245), '00:02:28', '148245ms → 00:02:28')
eq(bemfa.fmtUptime(90061000), '1天 01:01:01', '90061000ms → 1天 01:01:01')
eq(bemfa.fmtUptime(null), '--', 'null 运行时长 → --')
ok(bemfa.fmtCloudTime(1789522050).length === 14, '云端时间格式 MM-DD HH:MM:SS',
   bemfa.fmtCloudTime(1789522050))

console.log('\n=== 7. 按 seq 去重（轮询 5s / 上报 10s，绝不能让同一个 seq 记两次） ===')
bemfa.clearRecords()
const p1 = { ts: '10:00:00', seq: 100, hr: 70, spo2: 98, temp: 26, humi: 55, gas: 10, ax: 0, ay: 0, az: 1, accel: 1, st: 'normal', up: 1000 }
let r = bemfa.recordPoint(p1, now, 60)
eq(r.appended, true, '第一次收到 seq=100 → 新增一点')
r = bemfa.recordPoint(p1, now, 60)
eq(r.appended, false, '又轮询到同一个 seq=100 → 不重复记录')
eq(bemfa.loadPoints().length, 1, '缓存里仍然只有 1 个点')

const p2 = Object.assign({}, p1, { seq: 101, st: 'GAS ALARM' })
r = bemfa.recordPoint(p2, now + 10, 60)
eq(r.appended, true, 'seq=101 → 新增一点')
eq(bemfa.loadPoints().length, 2, '缓存里有 2 个点')
eq(bemfa.loadAlarms().length, 1, 'GAS ALARM 记入报警记录')
bemfa.recordPoint(p2, now + 10, 60)
eq(bemfa.loadAlarms().length, 1, '同一个报警报文不重复记流水')

console.log('\n=== 8. 缓存上限与设备重启 ===')
bemfa.clearRecords()
for (let i = 0; i < 80; i++) {
  bemfa.recordPoint(Object.assign({}, p1, { seq: i }), now + i, 60)
}
eq(bemfa.loadPoints().length, 60, '超过 60 点后自动丢最旧的')

bemfa.clearRecords()
bemfa.recordPoint(Object.assign({}, p1, { seq: 500 }), now, 60)
const rr = bemfa.recordPoint(Object.assign({}, p1, { seq: 3 }), now + 10, 60)
eq(rr.restarted, true, 'seq 从 500 掉回 3 → 能识别出设备重启')

console.log('\n=== 9. 清空 ===')
bemfa.clearRecords()
eq(bemfa.loadPoints().length, 0, '趋势已清空')
eq(bemfa.loadAlarms().length, 0, '报警记录已清空')

console.log('\n' + '='.repeat(62))
console.log('  通过 ' + pass + ' 项，失败 ' + fail + ' 项')
console.log('='.repeat(62) + '\n')
process.exit(fail === 0 ? 0 : 1)
