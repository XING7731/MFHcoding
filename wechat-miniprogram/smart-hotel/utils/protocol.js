/**
 * utils/protocol.js
 * 「未来酒店·智慧监控」下位机通信协议编解码 + 告警判定。
 *
 * ── 传感器上报（单片机 -> 云端，主题 = infoTopic）────────────────
 *   格式：  #温度#湿度#烟雾值#加湿器开关
 *   示例：  #25.3#58.1#132#1
 *   说明：  以 # 包裹/分割；下标 1=温度(℃)，2=湿度(%)，3=烟雾值，4=加湿器开关(1开/0关)
 *
 * ── 控制下发（小程序 -> 云端，主题 = ctrlTopic）────────────────
 *   灯光（单路灯带，0=全灭 1=红灯 2=黄灯）：
 *     #0   关闭所有灯
 *     #1   打开红灯（黄灯随之熄灭）
 *     #2   打开黄灯（红灯随之熄灭）
 *   加湿器（字段1 = 模式：0 手动 / 1 自动；字段2 = 开关：0 关 / 1 开）：
 *     #0#0 手动模式 + 关闭      #0#1 手动模式 + 开启
 *     #1#0 自动模式 + 关闭      #1#1 自动模式 + 开启
 */

/** 灯光指令值 */
const LIGHT = {
  OFF: 0,
  RED: 1,
  YELLOW: 2,
};

/** 场景模式 */
const MODE = {
  MANUAL: 1,
  AUTO: 2,
};

/* ------------------------------------------------------------------ */
/* 编码                                                                */
/* ------------------------------------------------------------------ */

/** 生成灯光指令，light ∈ {0,1,2} */
function buildLightCmd(light) {
  const value = light === LIGHT.RED || light === LIGHT.YELLOW ? light : LIGHT.OFF;
  return `#${value}`;
}

/** 生成加湿器指令，auto 为 true 表示自动模式 */
function buildHumiCmd(auto, on) {
  return `#${auto ? 1 : 0}#${on ? 1 : 0}`;
}

/* ------------------------------------------------------------------ */
/* 解码                                                                */
/* ------------------------------------------------------------------ */

/**
 * 解析传感器上报消息
 * @param {string} msg
 * @returns {{temp:number, humi:number, smoke:number, humiOn:boolean}|null}
 */
function parseSensor(msg) {
  if (typeof msg !== 'string' || msg.indexOf('#') === -1) {
    return null;
  }
  const tokens = msg.split('#');
  // 期望形如 ['', 温度, 湿度, 烟雾, 加湿器开关]
  if (tokens.length < 4) {
    return null;
  }
  const temp = parseFloat(tokens[1]);
  const humi = parseFloat(tokens[2]);
  if (isNaN(temp) || isNaN(humi)) {
    return null;
  }
  const smoke = tokens[3] === undefined || tokens[3] === '' ? null : parseFloat(tokens[3]);
  return {
    temp,
    humi,
    smoke: isNaN(smoke) ? null : smoke,
    humiOn: tokens[4] === '1',
  };
}

/**
 * 解析控制主题上的回读消息（用于把界面状态同步为下位机真实状态）
 * 只返回消息中确实携带的字段，便于与现有界面状态做「局部合并」。
 * @param {string} msg
 * @returns {{light?:number, mode?:number, humiOn?:boolean}|null}
 */
function parseCtrl(msg) {
  if (typeof msg !== 'string' || msg.indexOf('#') === -1) {
    return null;
  }
  const tokens = msg.split('#');

  // '#x' -> 灯光
  if (tokens.length === 2) {
    const value = parseInt(tokens[1], 10);
    if (value === LIGHT.OFF || value === LIGHT.RED || value === LIGHT.YELLOW) {
      return { light: value };
    }
    return null;
  }

  // '#a#b' -> 加湿器模式 + 开关
  if (tokens.length === 3) {
    return {
      mode: tokens[1] === '1' ? MODE.AUTO : MODE.MANUAL,
      humiOn: tokens[2] === '1',
    };
  }

  return null;
}

/* ------------------------------------------------------------------ */
/* 告警                                                                */
/* ------------------------------------------------------------------ */

/**
 * 判断当前告警项
 * @param {object} p
 * @param {number} p.temp 实测温度
 * @param {number} p.humi 实测湿度
 * @param {number} p.smoke 实测烟雾值（可为 null）
 * @param {number} p.setHumi 设定湿度
 * @param {object} p.threshold 阈值配置 { tempMin, tempMax, humiOffset, smokeMax }
 * @returns {Array<{key:string, text:string, level:string, image:string}>}
 */
function judgeAlarms({ temp, humi, smoke, setHumi, threshold }) {
  const cfg = threshold || {};
  const tempMin = num(cfg.tempMin, 16);
  const tempMax = num(cfg.tempMax, 35);
  const humiOffset = num(cfg.humiOffset, 5);
  const smokeMax = num(cfg.smokeMax, 0);
  const alarms = [];

  if (!isNaN(temp)) {
    if (temp < tempMin) {
      alarms.push(alarm('temp-low', '温度过低', `温度过低（${temp}℃ < ${tempMin}℃）`, 'danger'));
    } else if (temp > tempMax) {
      alarms.push(alarm('temp-high', '温度过高', `温度过高（${temp}℃ > ${tempMax}℃）`, 'danger'));
    }
  }

  if (!isNaN(humi) && !isNaN(setHumi)) {
    if (humi < setHumi - humiOffset) {
      alarms.push(
        alarm('humi-low', '湿度过低', `湿度过低（${humi}% < 设定 ${setHumi}%）`, 'warn')
      );
    } else if (humi > setHumi + humiOffset) {
      alarms.push(
        alarm('humi-high', '湿度过高', `湿度过高（${humi}% > 设定 ${setHumi}%）`, 'warn')
      );
    }
  }

  // 烟雾阈值设为 0 时表示不启用烟雾告警
  if (smokeMax > 0 && !isNaN(smoke) && smoke > smokeMax) {
    alarms.push(
      alarm('smoke-high', '烟雾超标', `烟雾浓度超标（${smoke} > ${smokeMax}）`, 'danger')
    );
  }

  return alarms;
}

function alarm(key, short, text, level) {
  return {
    key,
    short,
    text,
    level: level || 'warn',
    image: '/utils/img/wran.png',
  };
}

function num(value, fallback) {
  const n = parseFloat(value);
  return isNaN(n) ? fallback : n;
}

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

/** 把云端的 '2026-09-15 16:08:50' 截成 '16:08:50'；为空时回退到本地时间 */
function toClockText(time, date) {
  const now = date || new Date();
  const local = pad(now.getHours()) + ':' + pad(now.getMinutes()) + ':' + pad(now.getSeconds());
  if (typeof time === 'string' && time.length >= 8) {
    const part = time.split(' ')[1] || time;
    if (part.length >= 8) {
      return part.slice(0, 8);
    }
  }
  return local;
}

function pad(n) {
  return n < 10 ? `0${n}` : `${n}`;
}

module.exports = {
  LIGHT,
  MODE,
  buildLightCmd,
  buildHumiCmd,
  parseSensor,
  parseCtrl,
  judgeAlarms,
  toClockText,
  pad,
};
