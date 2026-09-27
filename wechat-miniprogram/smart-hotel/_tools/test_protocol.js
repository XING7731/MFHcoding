/**
 * _tools/test_protocol.js
 * 用 Node 直接验证 utils/protocol.js 的编解码与告警逻辑（该文件不依赖任何 wx API）。
 * 运行：node _tools/test_protocol.js
 */
const path = require('path');
const p = require(path.join(__dirname, '..', 'utils', 'protocol.js'));

let pass = 0;
let fail = 0;

function eq(actual, expected, label) {
  const a = JSON.stringify(actual);
  const b = JSON.stringify(expected);
  if (a === b) {
    pass += 1;
    console.log(`  ok   ${label}`);
  } else {
    fail += 1;
    console.log(`  FAIL ${label}\n       期望 ${b}\n       实际 ${a}`);
  }
}

console.log('— 传感器上报解析 —');
eq(
  p.parseSensor('#25.3#58.1#132#1'),
  { temp: 25.3, humi: 58.1, smoke: 132, humiOn: true },
  '标准 4 段报文'
);
eq(
  p.parseSensor('#26#60#0#0'),
  { temp: 26, humi: 60, smoke: 0, humiOn: false },
  '整数报文 + 加湿器关闭'
);
eq(p.parseSensor('25.3#58.1'), null, '缺少 # 前缀视为无效');
eq(p.parseSensor('#abc#58#1#0'), null, '温度非数字视为无效');
eq(p.parseSensor(''), null, '空串视为无效');

console.log('— 控制回读解析 —');
eq(p.parseCtrl('#0'), { light: 0 }, '全关灯');
eq(p.parseCtrl('#1'), { light: 1 }, '红灯');
eq(p.parseCtrl('#2'), { light: 2 }, '黄灯');
eq(p.parseCtrl('#3'), null, '越界灯光值视为无效');
eq(p.parseCtrl('#0#0'), { mode: 1, humiOn: false }, '手动 + 关');
eq(p.parseCtrl('#0#1'), { mode: 1, humiOn: true }, '手动 + 开');
eq(p.parseCtrl('#1#0'), { mode: 2, humiOn: false }, '自动 + 关');
eq(p.parseCtrl('#1#1'), { mode: 2, humiOn: true }, '自动 + 开');
eq(p.parseCtrl('#25.3#58.1#132#1'), null, '传感器报文不会被当成控制指令');

console.log('— 指令编码 —');
eq(p.buildLightCmd(p.LIGHT.OFF), '#0', '关灯指令');
eq(p.buildLightCmd(p.LIGHT.RED), '#1', '红灯指令');
eq(p.buildLightCmd(p.LIGHT.YELLOW), '#2', '黄灯指令');
eq(p.buildLightCmd(99), '#0', '非法值回退为关灯');
eq(p.buildHumiCmd(true, true), '#1#1', '自动 + 开');
eq(p.buildHumiCmd(false, false), '#0#0', '手动 + 关');

console.log('— 告警判定 —');
const threshold = { tempMin: 16, tempMax: 35, humiOffset: 5, smokeMax: 0 };
eq(
  p.judgeAlarms({ temp: 15, humi: 40, smoke: 0, setHumi: 40, threshold }).map((a) => a.key),
  ['temp-low'],
  '温度过低'
);
eq(
  p.judgeAlarms({ temp: 36, humi: 40, smoke: 0, setHumi: 40, threshold }).map((a) => a.key),
  ['temp-high'],
  '温度过高'
);
eq(
  p.judgeAlarms({ temp: 25, humi: 34, smoke: 0, setHumi: 40, threshold }).map((a) => a.key),
  ['humi-low'],
  '湿度过低'
);
eq(
  p.judgeAlarms({ temp: 25, humi: 46, smoke: 0, setHumi: 40, threshold }).map((a) => a.key),
  ['humi-high'],
  '湿度过高'
);
eq(
  p.judgeAlarms({ temp: 25, humi: 40, smoke: 9999, setHumi: 40, threshold }).length,
  0,
  '烟雾阈值为 0 时不告警'
);
eq(
  p.judgeAlarms({
    temp: 25,
    humi: 40,
    smoke: 900,
    setHumi: 40,
    threshold: Object.assign({}, threshold, { smokeMax: 500 }),
  }).map((a) => a.key),
  ['smoke-high'],
  '烟雾超标'
);
eq(
  p.judgeAlarms({ temp: 25, humi: 40, smoke: null, setHumi: 40, threshold }).length,
  0,
  '数据正常时无告警'
);

console.log('— 时间格式化 —');
eq(p.toClockText('2026-09-15 16:08:50'), '16:08:50', '云端时间取时分秒');
eq(p.toClockText(''), new Date().toTimeString().slice(0, 8), '空时间回落本地时间');

console.log(`\n通过 ${pass} 项，失败 ${fail} 项`);
process.exit(fail ? 1 : 0);
