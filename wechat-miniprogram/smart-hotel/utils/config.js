/**
 * utils/config.js
 * 本地配置读写：巴法云 uid、房间列表、轮询周期、告警阈值。
 * 数据保存在 wx.setStorageSync 中，首次运行写入默认值。
 */

const STORAGE_KEY = 'future_hotel_config_v1';

/** 默认配置：与《智边缘-未来酒店-微信小程序》文档一致 */
const DEFAULT_CONFIG = {
  // 巴法云控制台「个人中心 - 私钥」获取
  uid: 'd2f5cf227188d137f8171da8611a7614',
  // 巴法云设备协议类型：3 = TCP
  deviceType: 3,
  // 轮询周期（毫秒）
  pollInterval: 3000,
  // 温度告警下限 / 上限（℃）
  tempMin: 16,
  tempMax: 35,
  // 湿度告警偏差（%）：实测湿度偏离设定值超过该值即告警
  humiOffset: 5,
  // 烟雾告警阈值；设为 0 表示关闭烟雾告警
  smokeMax: 0,
  // 趋势曲线保留的最大点数
  trendMaxPoints: 20,
  // 舒适度设定（℃ / %）
  setTemp: 24,
  setHumi: 40,
  // 同一类告警的提示间隔（毫秒），避免定时器反复弹提示
  alarmCooldown: 30000,
  // 房间列表：infoTopic 传温湿度，ctrlTopic 收控制指令
  rooms: [
    {
      id: 'room-8801',
      name: '客房 8801',
      infoTopic: 'Smart',
      ctrlTopic: 'SmartCtrl',
    },
  ],
  activeRoomId: 'room-8801',
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

/** 补齐缺失字段，兼容旧版本本地数据 */
function normalize(raw) {
  const base = clone(DEFAULT_CONFIG);
  if (!raw || typeof raw !== 'object') {
    return base;
  }
  const cfg = Object.assign(base, raw);
  if (!Array.isArray(cfg.rooms) || cfg.rooms.length === 0) {
    cfg.rooms = clone(DEFAULT_CONFIG.rooms);
  }
  cfg.rooms = cfg.rooms.map((room, index) => ({
    id: room.id || `room-${Date.now()}-${index}`,
    name: room.name || `房间 ${index + 1}`,
    infoTopic: room.infoTopic || DEFAULT_CONFIG.rooms[0].infoTopic,
    ctrlTopic: room.ctrlTopic || DEFAULT_CONFIG.rooms[0].ctrlTopic,
  }));
  if (!cfg.rooms.some((room) => room.id === cfg.activeRoomId)) {
    cfg.activeRoomId = cfg.rooms[0].id;
  }
  return cfg;
}

/** 读取配置 */
function load() {
  let raw = null;
  try {
    raw = wx.getStorageSync(STORAGE_KEY);
  } catch (err) {
    raw = null;
  }
  const cfg = normalize(raw);
  if (!raw) {
    save(cfg);
  }
  return cfg;
}

/** 保存配置 */
function save(cfg) {
  const next = normalize(cfg);
  try {
    wx.setStorageSync(STORAGE_KEY, next);
  } catch (err) {
    console.error('[config] 保存失败', err);
  }
  return next;
}

/** 局部更新 */
function update(patch) {
  return save(Object.assign(load(), patch));
}

/** 恢复默认 */
function reset() {
  return save(clone(DEFAULT_CONFIG));
}

/** 取当前选中房间 */
function getActiveRoom(cfg) {
  const conf = cfg || load();
  return (
    conf.rooms.find((room) => room.id === conf.activeRoomId) || conf.rooms[0]
  );
}

/** 生成房间 id */
function createRoomId() {
  return `room-${Date.now().toString(36)}`;
}

module.exports = {
  STORAGE_KEY,
  DEFAULT_CONFIG,
  load,
  save,
  update,
  reset,
  getActiveRoom,
  createRoomId,
};
