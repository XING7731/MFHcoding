// app.js —— 智能安全帽小程序 · 全局配置
//
// 这里只放"跨页面共用"的东西：巴法云连接参数 + 取数通道。
// 数据本身不存这里，每个页面自己去 utils/bemfa.js / utils/mqttws.js 拉。

const DEFAULT_CFG = {
  uid: 'd2f5cf227188d137f8171da8611a7614', // 巴法云私钥（= MQTT ClientID）
  topic: 'temp001',                          // 主题名（与固件 user_config.h 的 BEMFA_TOPIC 一致）

  /* 取数通道。两条路的白名单是【互相独立】的配置项，这也是为什么要能切换：
   *   'auto' —— 先试 WSS 实时推送，连不上再退回 HTTP 轮询（默认）
   *   'wss'  —— 只用 WSS，wss://bemfa.com:9504/wss  -> 需 socket 合法域名
   *   'http' —— 只用 HTTP 轮询，apis.bemfa.com/va/getmsg -> 需 request 合法域名
   * 只配了一条白名单时，选 'auto' 或直接指定能用的那条即可。
   *
   * 注：WSS 用私钥(UID)作 ClientID。已实测（tools/wxmp_wss_test.js）
   * 连接期间板子仍持续上报，不会把板子顶下线。 */
  channel: 'auto',

  intervalSec: 5,      // HTTP 轮询间隔：板子 10s 上报一次，取 5s 保证及时
  /* 本机趋势缓存点数。板子 10s 一条，所以 240 点 ≈ 40 分钟。
   * 从 60（10 分钟）提到 240，是因为趋势页换成了折线图 —— 10 分钟只够看
   * 一小段，看不出"温度是不是在缓慢往上爬"这类真正要看的东西。
   * 代价：每条点约 110 字节 JSON，240 点 ≈ 26KB，远低于单个 key 1MB 的上限。 */
  keepPoints: 240,

  wssUrl: 'wss://bemfa.com:9504/wss',
  wssKeepAlive: 60     // MQTT 心跳间隔（秒）
}

App({
  globalData: {
    cfg: null
  },

  onLaunch() {
    const saved = wx.getStorageSync('bemfa_cfg')
    this.globalData.cfg = Object.assign({}, DEFAULT_CFG, saved || {})
  },

  getCfg() {
    if (!this.globalData.cfg) {
      const saved = wx.getStorageSync('bemfa_cfg')
      this.globalData.cfg = Object.assign({}, DEFAULT_CFG, saved || {})
    }
    return this.globalData.cfg
  },

  saveCfg(patch) {
    const next = Object.assign({}, this.getCfg(), patch || {})
    this.globalData.cfg = next
    wx.setStorageSync('bemfa_cfg', next)
    return next
  },

  resetCfg() {
    this.globalData.cfg = Object.assign({}, DEFAULT_CFG)
    wx.removeStorageSync('bemfa_cfg')
    return this.globalData.cfg
  },

  getDefaultCfg() {
    return Object.assign({}, DEFAULT_CFG)
  }
})
