// pages/rooms/rooms.js
// 多房间总览：并发拉取每个房间的温湿度 / 烟雾与在线状态，点选后回到监控页查看详情。
const config = require('../../utils/config');
const bemfa = require('../../utils/bemfa');
const protocol = require('../../utils/protocol');

function fmt(value) {
  const n = Number(value);
  if (!isFinite(n)) {
    return '--';
  }
  return Number.isInteger(n) ? String(n) : n.toFixed(1);
}

function displayTime(raw) {
  if (typeof raw !== 'string' || !raw) {
    return '';
  }
  return raw.length >= 19 ? raw.slice(5, 19) : raw;
}

function createCard(room) {
  return {
    id: room.id,
    name: room.name,
    infoTopic: room.infoTopic,
    ctrlTopic: room.ctrlTopic,
    online: false,
    temp: '--',
    humi: '--',
    smoke: '--',
    humiOn: false,
    time: '',
    warnText: '',
  };
}

Page({
  data: {
    rooms: [],
    cards: [],
    activeRoomId: '',
    onlineCount: 0,
    warnCount: 0,
  },

  onLoad() {
    this.cfg = config.load();
    this.refreshing = false;
    this.setData({
      rooms: this.cfg.rooms,
      cards: this.cfg.rooms.map(createCard),
      activeRoomId: this.cfg.activeRoomId,
    });
    this.refresh();
  },

  onShow() {
    const latest = config.load();
    if (JSON.stringify(latest) !== JSON.stringify(this.cfg)) {
      this.cfg = latest;
      this.setData({
        rooms: latest.rooms,
        cards: latest.rooms.map(createCard),
        activeRoomId: latest.activeRoomId,
        onlineCount: 0,
        warnCount: 0,
      });
      this.refresh();
    }
  },

  onPullDownRefresh() {
    this.refresh();
  },

  /** 并发拉取所有房间的数据 */
  refresh() {
    if (this.refreshing) {
      wx.stopPullDownRefresh();
      return Promise.resolve();
    }
    const cfg = this.cfg;
    const rooms = cfg.rooms;
    if (!rooms.length) {
      wx.stopPullDownRefresh();
      return Promise.resolve();
    }

    this.refreshing = true;

    const tasks = rooms.map((room, index) =>
      Promise.all([
        bemfa
          .getMsg({ uid: cfg.uid, topic: room.infoTopic, type: cfg.deviceType })
          .catch(() => null),
        bemfa
          .getOnline({ uid: cfg.uid, topic: room.infoTopic, type: cfg.deviceType })
          .catch(() => null),
      ]).then(([msgRes, online]) => {
        const data = msgRes ? protocol.parseSensor(msgRes.msg) : null;
        let warnText = '';
        if (data) {
          const alarms = protocol.judgeAlarms({
            temp: data.temp,
            humi: data.humi,
            smoke: data.smoke,
            setHumi: cfg.setHumi,
            threshold: cfg,
          });
          warnText = alarms.map((item) => item.text).join('；');
        }
        this.setData({
          [`cards[${index}]`]: {
            id: room.id,
            name: room.name,
            infoTopic: room.infoTopic,
            ctrlTopic: room.ctrlTopic,
            online: !!online,
            temp: data ? fmt(data.temp) : '--',
            humi: data ? fmt(data.humi) : '--',
            smoke: data && data.smoke !== null ? fmt(data.smoke) : '--',
            humiOn: data ? data.humiOn : false,
            time: msgRes ? displayTime(msgRes.time) : '',
            warnText,
          },
        });
      })
    );

    return Promise.all(tasks)
      .catch(() => null)
      .then(() => {
        this.refreshing = false;
        this.updateSummary();
        wx.stopPullDownRefresh();
      });
  },

  updateSummary() {
    const cards = this.data.cards || [];
    let onlineCount = 0;
    let warnCount = 0;
    cards.forEach((card) => {
      if (card.online) {
        onlineCount += 1;
      }
      if (card.warnText) {
        warnCount += 1;
      }
    });
    this.setData({ onlineCount, warnCount });
  },

  /** 选中房间：更新默认房间并返回监控页 */
  onSelectRoom(event) {
    const index = Number(event.currentTarget.dataset.index);
    const room = this.data.rooms[index];
    if (!room) {
      return;
    }
    config.update({ activeRoomId: room.id });
    const pages = getCurrentPages();
    if (pages.length > 1) {
      wx.navigateBack();
    } else {
      wx.redirectTo({ url: '/pages/index/index' });
    }
  },

  goSetting() {
    wx.navigateTo({ url: '/pages/setting/setting' });
  },
});
