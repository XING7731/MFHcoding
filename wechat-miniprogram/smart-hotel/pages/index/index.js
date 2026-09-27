// pages/index/index.js
// 「未来酒店·智慧监控」主页面：温湿度 / 烟雾实时数据展示，加湿器与灯光控制，趋势曲线与阈值告警。
const config = require('../../utils/config');
const bemfa = require('../../utils/bemfa');
const protocol = require('../../utils/protocol');

const { LIGHT, MODE } = protocol;

/** 本地图标资源统一在这里维护，避免散落在业务代码中 */
const ICON = {
  humiOn: '/utils/img/humi-on.png',
  humiOff: '/utils/img/humi-off.png',
  lampOff: '/utils/img/lightoff.png',
  lampYellowOn: '/utils/img/light-yellow-on.png',
  lampRedOn: '/utils/img/light-red-on.png',
  cellOff: '/utils/img/light.png',
  cellYellow: '/utils/img/light-yellow.png',
  cellRed: '/utils/img/light-red.png',
};

/** 曲线初始系列（改为函数，避免多个页面实例共享同一份数据） */
function createTrendSeries() {
  return [
    { name: '温度', unit: '℃', color: '#ee6c3a', data: [] },
    { name: '湿度', unit: '%', color: '#1976d2', data: [] },
  ];
}

/** 数值展示：整数不带小数点，其它保留一位 */
function fmt(value) {
  const n = Number(value);
  if (!isFinite(n)) {
    return '--';
  }
  return Number.isInteger(n) ? String(n) : n.toFixed(1);
}

/** '2026-09-15 16:08:50' -> '09-15 16:08:50' */
function displayTime(raw) {
  if (typeof raw !== 'string' || !raw) {
    return '';
  }
  return raw.length >= 19 ? raw.slice(5, 19) : raw;
}

Page({
  data: {
    /* 房间 */
    rooms: [],
    activeRoomIndex: 0,
    activeRoom: null,

    /* 实时数据 */
    deviceOnline: false,
    wendu: '--',
    shidu: '--',
    smoke: '--',
    dataTime: '',

    /* 舒适度设定 */
    setTemp: 24,
    setHumi: 40,

    /* 加湿器 */
    humiStatus: false,
    humiIcon: ICON.humiOff,
    currentMode: MODE.MANUAL,
    btnDisabled: false,

    /* 灯光 */
    yellowChecked: false,
    redChecked: false,
    ledYellowIcon: ICON.lampOff,
    ledRedIcon: ICON.lampOff,
    yellowIcon: ICON.cellOff,
    redIcon: ICON.cellOff,

    /* 告警 */
    alarms: [],
    alarmText: '',

    /* 趋势曲线 */
    trendX: [],
    trendSeries: createTrendSeries(),
  },

  /* ================= 生命周期 ================= */

  onLoad() {
    this.cfg = config.load();
    this.timer = null;
    this.tick = 0;
    this.requesting = false;
    this.lastSensorRaw = '';
    this.lastAutoCmd = '';
    this.ctrlLockUntil = 0;
    this.alarmFiredAt = {};

    this.applyConfig({ resetTrend: true });
  },

  onShow() {
    // 设置页 / 房间总览可能修改了 uid、房间或阈值，回到首页时同步一次
    const latest = config.load();
    if (JSON.stringify(latest) !== JSON.stringify(this.cfg)) {
      const roomChanged = latest.activeRoomId !== this.cfg.activeRoomId;
      this.cfg = latest;
      this.lastSensorRaw = '';
      this.lastAutoCmd = '';
      this.ctrlLockUntil = 0;
      this.applyConfig({ resetTrend: roomChanged });
    }
    this.startPolling();
  },

  onHide() {
    this.stopPolling();
  },

  onUnload() {
    this.stopPolling();
  },

  onPullDownRefresh() {
    this.refresh({ forceAll: true }).then(() => wx.stopPullDownRefresh());
  },

  /* ================= 配置 / 房间 ================= */

  applyConfig({ resetTrend }) {
    const cfg = this.cfg;
    const rooms = cfg.rooms;
    let index = rooms.findIndex((room) => room.id === cfg.activeRoomId);
    if (index < 0) {
      index = 0;
    }
    const room = rooms[index];

    const patch = {
      rooms,
      activeRoomIndex: index,
      activeRoom: room,
      setTemp: cfg.setTemp,
      setHumi: cfg.setHumi,
    };
    if (resetTrend) {
      patch.trendX = [];
      patch.trendSeries = createTrendSeries();
      this.lastSensorRaw = '';
    }
    this.setData(patch, () => {
      wx.setNavigationBarTitle({ title: `${room.name} · 智慧监控` });
    });
  },

  onRoomTabChange(event) {
    const index = event.detail.index;
    const room = this.data.rooms[index];
    if (!room || room.id === (this.data.activeRoom || {}).id) {
      return;
    }
    this.cfg = config.update({ activeRoomId: room.id });
    this.lastSensorRaw = '';
    this.lastAutoCmd = '';
    this.ctrlLockUntil = 0;
    this.applyConfig({ resetTrend: true });
    this.refresh({ forceAll: true });
  },

  /* ================= 轮询 ================= */

  startPolling() {
    this.stopPolling();
    const interval = Math.max(1000, Number(this.cfg.pollInterval) || 3000);
    this.timer = setInterval(() => this.refresh(), interval);
    this.tick = 0;
    this.refresh({ forceAll: true });
  },

  stopPolling() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  },

  /**
   * 拉取一轮数据。
   * 传感器数据每轮都取；在线状态与控制回读按更低频率取（3 轮 / 2 轮一次），
   * forceAll 用于下拉刷新、切换房间等需要立刻拿到全部状态的场景。
   */
  refresh(options) {
    if (this.requesting) {
      return Promise.resolve();
    }
    const room = this.data.activeRoom;
    if (!room) {
      return Promise.resolve();
    }

    const forceAll = !!(options && options.forceAll);
    this.requesting = true;
    this.tick = forceAll ? 6 : this.tick + 1; // 6 同时是 2 和 3 的倍数

    const tasks = [this.fetchSensor()];
    if (this.tick % 3 === 0) {
      tasks.push(this.fetchOnline());
    }
    if (this.tick % 2 === 0) {
      tasks.push(this.fetchCtrlState());
    }

    return Promise.all(tasks.map((task) => task.catch(() => null))).then(() => {
      this.requesting = false;
    });
  },

  /** 读取传感器主题（infoTopic）的最新一条数据 */
  fetchSensor() {
    const room = this.data.activeRoom;
    const cfg = this.cfg;
    return bemfa
      .getMsg({ uid: cfg.uid, topic: room.infoTopic, type: cfg.deviceType })
      .then((res) => {
        if (!res || !res.msg) {
          this.markOffline();
          return;
        }
        const data = protocol.parseSensor(res.msg);
        if (!data) {
          this.markOffline();
          return;
        }

        const clock = protocol.toClockText(res.time);
        this.setData({
          deviceOnline: true,
          wendu: fmt(data.temp),
          shidu: fmt(data.humi),
          smoke: data.smoke === null ? '--' : fmt(data.smoke),
          dataTime: displayTime(res.time) || clock,
        });

        // 同一条上报只入图一次，避免曲线出现重复点
        if (this.lastSensorRaw !== res.msg) {
          this.lastSensorRaw = res.msg;
          this.appendTrend(data.temp, data.humi, clock);
        }

        this.evaluate(data);
      })
      .catch(() => this.markOffline());
  },

  /** 读取设备在线状态 */
  fetchOnline() {
    const room = this.data.activeRoom;
    const cfg = this.cfg;
    return bemfa
      .getOnline({ uid: cfg.uid, topic: room.infoTopic, type: cfg.deviceType })
      .then((online) => this.setData({ deviceOnline: !!online }));
  },

  /** 回读控制主题（ctrlTopic），把界面状态与下位机实际状态对齐 */
  fetchCtrlState() {
    if (Date.now() < this.ctrlLockUntil) {
      return Promise.resolve();
    }
    const room = this.data.activeRoom;
    const cfg = this.cfg;
    return bemfa
      .getMsg({ uid: cfg.uid, topic: room.ctrlTopic, type: cfg.deviceType })
      .then((res) => {
        if (!res || !res.msg) {
          return;
        }
        const state = protocol.parseCtrl(res.msg);
        if (!state) {
          return;
        }
        if (state.light !== undefined) {
          this.applyLightState(state.light);
        }
        const patch = {};
        if (state.mode !== undefined) {
          patch.currentMode = state.mode;
          patch.btnDisabled = state.mode === MODE.AUTO;
        }
        if (state.humiOn !== undefined) {
          patch.humiStatus = state.humiOn;
          patch.humiIcon = state.humiOn ? ICON.humiOn : ICON.humiOff;
        }
        if (Object.keys(patch).length) {
          this.setData(patch);
        }
      });
  },

  markOffline() {
    if (this.data.deviceOnline) {
      this.setData({ deviceOnline: false });
    }
  },

  /* ================= 数据评估：告警 + 自动策略 ================= */

  evaluate(data) {
    const alarms = protocol.judgeAlarms({
      temp: data.temp,
      humi: data.humi,
      smoke: data.smoke,
      setHumi: this.data.setHumi,
      threshold: this.cfg,
    });
    const alarmText = alarms.map((item) => item.text).join('　');
    if (alarmText !== this.data.alarmText) {
      this.setData({ alarms, alarmText });
    }
    this.raiseAlarms(alarms);

    // 自动模式：湿度低于设定值则开启加湿器（仅在下发指令变化时发送，避免刷屏）
    if (this.data.currentMode === MODE.AUTO) {
      const want = data.humi < this.data.setHumi;
      const cmd = protocol.buildHumiCmd(true, want);
      if (cmd !== this.lastAutoCmd) {
        this.lastAutoCmd = cmd;
        this.sendCtrl(cmd, { silent: true });
        this.setData({
          humiStatus: want,
          humiIcon: want ? ICON.humiOn : ICON.humiOff,
        });
      }
    }
  },

  /** 告警提示：同类告警在冷却时间内只弹一次，界面上始终保留告警条 */
  raiseAlarms(alarms) {
    if (!alarms.length) {
      return;
    }
    const cooldown = Number(this.cfg.alarmCooldown) || 30000;
    const now = Date.now();
    const fresh = alarms.find(
      (item) => now - (this.alarmFiredAt[item.key] || 0) > cooldown
    );
    if (!fresh) {
      return;
    }
    this.alarmFiredAt[fresh.key] = now;
    wx.showToast({
      title: fresh.short,
      image: fresh.image,
      duration: 2000,
    });
  },

  /* ================= 趋势曲线 ================= */

  appendTrend(temp, humi, label) {
    const max = Math.max(5, Number(this.cfg.trendMaxPoints) || 20);
    const x = this.data.trendX.concat([label]);
    const series = this.data.trendSeries.map((item, index) =>
      Object.assign({}, item, { data: item.data.concat([index === 0 ? temp : humi]) })
    );

    while (x.length > max) {
      x.shift();
      series.forEach((item) => item.data.shift());
    }
    this.setData({ trendX: x, trendSeries: series });
  },

  /* ================= 舒适度设定 ================= */

  onHumiChange(event) {
    const value = Number(event.detail);
    if (!isFinite(value)) {
      return;
    }
    // van-stepper 使用 async-change，需要父级回写数值
    this.setData({ setHumi: value });
    this.cfg = config.update({ setHumi: value });
    this.lastAutoCmd = ''; // 设定值变化后允许自动策略重新下发
  },

  onTempChange(event) {
    const value = Number(event.detail);
    if (!isFinite(value)) {
      return;
    }
    this.setData({ setTemp: value });
    this.cfg = config.update({ setTemp: value });
  },

  /* ================= 加湿器控制 ================= */

  onPowerTap() {
    if (this.data.btnDisabled) {
      return;
    }
    const next = !this.data.humiStatus;
    this.setData({
      humiStatus: next,
      humiIcon: next ? ICON.humiOn : ICON.humiOff,
    });
    this.sendCtrl(protocol.buildHumiCmd(this.data.currentMode === MODE.AUTO, next));
  },

  setScene(event) {
    const scene = event.currentTarget.dataset.scene;
    const toAuto = scene === 'auto';
    this.lastAutoCmd = '';

    if (!toAuto) {
      this.setData({ currentMode: MODE.MANUAL, btnDisabled: false });
      this.sendCtrl(protocol.buildHumiCmd(false, this.data.humiStatus));
      return;
    }

    const humi = parseFloat(this.data.shidu);
    const want = isNaN(humi) ? this.data.humiStatus : humi < this.data.setHumi;
    const cmd = protocol.buildHumiCmd(true, want);
    this.lastAutoCmd = cmd;
    this.setData({
      currentMode: MODE.AUTO,
      btnDisabled: true,
      humiStatus: want,
      humiIcon: want ? ICON.humiOn : ICON.humiOff,
    });
    this.sendCtrl(cmd, { silent: true });
  },

  /* ================= 灯光控制 ================= */

  onYellowChange(event) {
    const on = !!event.detail;
    const light = on ? LIGHT.YELLOW : LIGHT.OFF;
    this.applyLightState(light);
    this.sendCtrl(protocol.buildLightCmd(light));
  },

  onRedChange(event) {
    const on = !!event.detail;
    const light = on ? LIGHT.RED : LIGHT.OFF;
    this.applyLightState(light);
    this.sendCtrl(protocol.buildLightCmd(light));
  },

  /**
   * 一路灯带只能点亮一种颜色，所以「开黄灯」会同时把红灯置为关闭，
   * 反之亦然；两个开关始终保持互斥，与下位机 #0/#1/#2 指令语义一致。
   */
  applyLightState(light) {
    this.setData({
      yellowChecked: light === LIGHT.YELLOW,
      redChecked: light === LIGHT.RED,
      ledYellowIcon: light === LIGHT.YELLOW ? ICON.lampYellowOn : ICON.lampOff,
      ledRedIcon: light === LIGHT.RED ? ICON.lampRedOn : ICON.lampOff,
      yellowIcon: light === LIGHT.YELLOW ? ICON.cellYellow : ICON.cellOff,
      redIcon: light === LIGHT.RED ? ICON.cellRed : ICON.cellOff,
    });
  },

  /* ================= 指令下发 ================= */

  sendCtrl(msg, options) {
    const opts = options || {};
    const room = this.data.activeRoom;
    if (!room) {
      return Promise.resolve();
    }
    return bemfa
      .postMsg({
        uid: this.cfg.uid,
        topic: room.ctrlTopic,
        msg,
        type: this.cfg.deviceType,
      })
      .then(() => {
        // 指令下发后的短时间内不做过期回读，避免云端数据未刷新导致界面回跳
        this.ctrlLockUntil = Date.now() + 3000;
        if (!opts.silent) {
          wx.showToast({ title: '指令已下发', icon: 'none', duration: 1200 });
        }
      })
      .catch((err) => {
        wx.showToast({
          title: (err && err.message) || '指令下发失败',
          icon: 'none',
          duration: 2000,
        });
      });
  },

  /* ================= 跳转 ================= */

  goRooms() {
    wx.navigateTo({ url: '/pages/rooms/rooms' });
  },

  goSetting() {
    wx.navigateTo({ url: '/pages/setting/setting' });
  },
});
