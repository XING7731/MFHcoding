/**
 * utils/bemfa.js
 * 巴法云（bemfa）HTTP 接口封装。
 * 接口文档：https://cloud.bemfa.com/docs/
 *
 * 使用到的三个接口：
 *   1. GET  https://apis.bemfa.com/va/online       查询设备在线状态
 *   2. GET  https://apis.bemfa.com/va/getmsg       读取主题最新一条消息
 *   3. POST https://apis.bemfa.com/va/postJsonMsg  向主题下发消息（控制指令）
 *
 * 注意：正式发布前需在「微信公众平台 - 开发管理 - 服务器域名」中把
 *      https://apis.bemfa.com 加入 request 合法域名；开发工具中可临时勾选
 *      「不校验合法域名」。
 */

const BASE_URL = 'https://apis.bemfa.com/va';
const DEFAULT_TYPE = 3; // 3 = TCP 设备
const TIMEOUT = 8000;

/** 统一的 wx.request Promise 封装 */
function request(options) {
  return new Promise((resolve, reject) => {
    wx.request({
      url: options.url,
      method: options.method || 'GET',
      data: options.data,
      timeout: options.timeout || TIMEOUT,
      header: options.header || {
        'content-type': 'application/x-www-form-urlencoded',
      },
      success(res) {
        const body = res.data;
        if (res.statusCode !== 200) {
          reject(
            new Error(
              `网络异常（HTTP ${res.statusCode}），请检查服务器域名配置`
            )
          );
          return;
        }
        if (!body || typeof body !== 'object') {
          reject(new Error('云端返回数据格式异常'));
          return;
        }
        if (body.code !== undefined && body.code !== 0) {
          reject(new Error(body.message || `云端返回错误码 ${body.code}`));
          return;
        }
        resolve(body);
      },
      fail(err) {
        reject(new Error((err && err.errMsg) || '网络请求失败'));
      },
    });
  });
}

/**
 * 查询设备是否在线
 * @returns {Promise<boolean>}
 */
function getOnline({ uid, topic, type = DEFAULT_TYPE }) {
  return request({
    url: `${BASE_URL}/online`,
    data: { uid, topic, type },
  }).then((body) => !!body.data);
}

/**
 * 读取主题最新消息（num=1 表示只取最新一条）
 * @returns {Promise<{msg:string, time:string, unix:number}|null>}
 */
function getMsg({ uid, topic, type = DEFAULT_TYPE, num = 1 }) {
  return request({
    url: `${BASE_URL}/getmsg`,
    data: { uid, topic, type, num },
  }).then((body) => {
    const list = body.data;
    if (!Array.isArray(list) || list.length === 0) {
      return null;
    }
    const latest = list[0] || {};
    return {
      msg: latest.msg === undefined || latest.msg === null ? '' : String(latest.msg),
      time: latest.time || '',
      unix: latest.unix || 0,
    };
  });
}

/**
 * 向主题下发消息
 * @returns {Promise<boolean>}
 */
function postMsg({ uid, topic, msg, type = DEFAULT_TYPE }) {
  return request({
    url: `${BASE_URL}/postJsonMsg`,
    method: 'POST',
    data: { uid, topic, msg, type },
    header: { 'content-type': 'application/json; charset=utf-8' },
  }).then(() => true);
}

module.exports = {
  BASE_URL,
  DEFAULT_TYPE,
  getOnline,
  getMsg,
  postMsg,
};
