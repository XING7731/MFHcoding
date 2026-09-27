// pages/setting/setting.js
// 参数设置：巴法云私钥、轮询周期、告警阈值、多房间主题配置。修改后统一由底部「保存配置」写盘。
const config = require('../../utils/config');

/** 告警阈值：行配置驱动，避免页面里堆重复结构 */
const ALARM_ROWS = [
  { field: 'tempMin', label: '温度下限', unit: '℃', min: 0, max: 40, step: 1 },
  { field: 'tempMax', label: '温度上限', unit: '℃', min: 0, max: 60, step: 1 },
  { field: 'humiOffset', label: '湿度偏差', unit: '%', min: 1, max: 30, step: 1 },
  { field: 'smokeMax', label: '烟雾阈值', unit: 'ADC', min: 0, max: 5000, step: 50 },
];

/** 数据展示相关 */
const TREND_ROWS = [
  { field: 'trendMaxPoints', label: '曲线保留点数', unit: '个', min: 5, max: 60, step: 5 },
];

Page({
  data: {
    form: null,
    alarmRows: ALARM_ROWS,
    trendRows: TREND_ROWS,
    editing: false,
    editingIsNew: false,
    editingIndex: -1,
    roomForm: { name: '', infoTopic: '', ctrlTopic: '' },
  },

  onLoad() {
    this.loadForm();
  },

  /** 把磁盘上的配置读进页面表单（深拷贝，避免直接改到缓存对象） */
  loadForm() {
    const cfg = config.load();
    this.setData({
      form: JSON.parse(JSON.stringify(cfg)),
      editing: false,
      editingIndex: -1,
      editingIsNew: false,
    });
  },

  /* ================= 表单录入 ================= */

  onTextChange(event) {
    const field = event.currentTarget.dataset.field;
    if (!field) {
      return;
    }
    this.setData({ [`form.${field}`]: event.detail });
  },

  onNumberChange(event) {
    const field = event.currentTarget.dataset.field;
    const value = Number(event.detail);
    if (!field || !isFinite(value)) {
      return;
    }
    // van-stepper 使用 async-change，需要父级回写数值
    this.setData({ [`form.${field}`]: value });
  },

  /* ================= 房间增删改 ================= */

  startAddRoom() {
    this.setData({
      editing: true,
      editingIsNew: true,
      editingIndex: -1,
      roomForm: { name: '', infoTopic: '', ctrlTopic: '' },
    });
  },

  startEditRoom(event) {
    const index = Number(event.currentTarget.dataset.index);
    const room = this.data.form.rooms[index];
    if (!room) {
      return;
    }
    this.setData({
      editing: true,
      editingIsNew: false,
      editingIndex: index,
      roomForm: {
        name: room.name,
        infoTopic: room.infoTopic,
        ctrlTopic: room.ctrlTopic,
      },
    });
  },

  onRoomFormChange(event) {
    const field = event.currentTarget.dataset.field;
    if (!field) {
      return;
    }
    this.setData({ [`roomForm.${field}`]: event.detail });
  },

  cancelEditRoom() {
    this.setData({ editing: false, editingIndex: -1, editingIsNew: false });
  },

  saveRoom() {
    const { name, infoTopic, ctrlTopic } = this.data.roomForm;
    if (!String(name).trim()) {
      this.toast('请填写房间名称');
      return;
    }
    if (!String(infoTopic).trim()) {
      this.toast('请填写温湿度主题');
      return;
    }
    if (!String(ctrlTopic).trim()) {
      this.toast('请填写控制主题');
      return;
    }

    const rooms = this.data.form.rooms.slice();
    const room = {
      id: this.data.editingIsNew
        ? config.createRoomId()
        : rooms[this.data.editingIndex].id,
      name: String(name).trim(),
      infoTopic: String(infoTopic).trim(),
      ctrlTopic: String(ctrlTopic).trim(),
    };

    if (this.data.editingIsNew) {
      rooms.push(room);
    } else {
      rooms[this.data.editingIndex] = room;
    }

    this.setData({
      'form.rooms': rooms,
      editing: false,
      editingIndex: -1,
      editingIsNew: false,
    });
  },

  removeRoom(event) {
    const index = Number(event.currentTarget.dataset.index);
    const room = this.data.form.rooms[index];
    if (!room) {
      return;
    }
    wx.showModal({
      title: '删除房间',
      content: `确定删除「${room.name}」吗？保存后生效。`,
      confirmText: '删除',
      confirmColor: '#e54545',
      success: (res) => {
        if (!res.confirm) {
          return;
        }
        const rooms = this.data.form.rooms.slice();
        rooms.splice(index, 1);
        this.setData({
          'form.rooms': rooms,
          editing: false,
          editingIndex: -1,
          editingIsNew: false,
        });
      },
    });
  },

  /* ================= 保存 / 重置 ================= */

  saveConfig() {
    const form = this.data.form;
    if (!String(form.uid || '').trim()) {
      this.toast('请填写巴法云私钥');
      return;
    }
    if (Number(form.tempMin) >= Number(form.tempMax)) {
      this.toast('温度下限需小于上限');
      return;
    }
    if (!form.rooms || !form.rooms.length) {
      this.toast('至少保留一个房间');
      return;
    }

    config.save(form);
    this.loadForm();
    const app = getApp();
    if (app) {
      app.globalData.config = config.load();
    }
    wx.showToast({ title: '配置已保存', icon: 'success' });
  },

  resetConfig() {
    wx.showModal({
      title: '恢复默认配置',
      content: '将把私钥、阈值与房间列表全部恢复为出厂默认值。',
      confirmText: '恢复',
      confirmColor: '#e54545',
      success: (res) => {
        if (!res.confirm) {
          return;
        }
        config.reset();
        this.loadForm();
        const app = getApp();
        if (app) {
          app.globalData.config = config.load();
        }
        wx.showToast({ title: '已恢复默认', icon: 'success' });
      },
    });
  },

  toast(title) {
    wx.showToast({ title, icon: 'none', duration: 1800 });
  },
});
