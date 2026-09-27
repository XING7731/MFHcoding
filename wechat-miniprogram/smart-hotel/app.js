// app.js
// 「未来酒店·智慧监控」小程序入口
const config = require('./utils/config');

App({
  onLaunch() {
    // 读取本地保存的巴法云配置（首次运行写入默认值）
    this.globalData.config = config.load();
    // 登录（如需与自有后台打通，可在此处把 code 发给服务端换取 openid）
    wx.login({
      success: () => {
        // 当前项目直接使用巴法云 HTTP 接口，无需业务后台
      },
    });
  },

  onShow() {
    // 从设置页返回时同步最新配置
    this.globalData.config = config.load();
  },

  globalData: {
    /** @type {ReturnType<typeof config.load>|null} */
    config: null,
  },
});
