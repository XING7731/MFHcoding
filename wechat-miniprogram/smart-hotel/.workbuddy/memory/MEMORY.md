# 项目长期笔记 — WeChat smart hotel

## 项目定位
「未来酒店·智慧监控」微信小程序。配套下位机为 STM32 + DHT11 + 烟雾传感器 + 一路双色
灯带 + 加湿器，通过**巴法云（bemfa）HTTP 接口**做数据中转，小程序作为上位机展示与控制。

- AppID：`wxc49ee42eb1a117b2`
- 巴法云私钥（默认填在 `utils/config.js`）：`614b23ad1b6a4fa183406f6833007cb0`
- 主题：温湿度 `Smart`，控制 `SmartCtrl`，协议类型 `type=3`（TCP）
- 接口：`apis.bemfa.com/va/online`、`/va/getmsg`、`/va/postJsonMsg`

## 目录约定（重要，不要改回去）
- `dist/` 直接放 vant-weapp 组件，**不要**再套一层 `dist/dist`（引用路径是 `/dist/xxx/index`）。
- 图片统一放 `utils/img/`（wxml 里写 `/utils/img/xxx.png`）。
- 页面只在 `app.json` 的 `usingComponents` 注册真正被用到的 vant 组件，减少打包体积。
- `_tools/` 是开发脚本目录，已在 `project.config.json` 的 `packOptions.ignore` 中排除。

## 通信协议（下位机固件必须一致）
- 上报（infoTopic）：`#温度#湿度#烟雾值#加湿器开关`，例 `#25.3#58.1#132#1`
- 下发（ctrlTopic）：
  - 灯光 `#0` 全灭 / `#1` 红 / `#2` 黄 —— **单路灯带，红黄互斥**
  - 加湿器 `#模式#开关`，模式 0 手动 1 自动，例 `#1#1` = 自动开

## 工程约定
- 业务代码一律 CommonJS（`require`），不写 ESM，方便 `node --check` 与单元测试。
- 时间戳/时间字符串不要自己算，统一走 `utils/protocol.js` 的 `toClockText`。
- 所有云端请求走 `utils/bemfa.js`，不要在页面里直接 `wx.request`。
- 改完代码后跑一遍自检：
  - `python _tools/validate.py <node.exe 路径>`
  - `node _tools/test_protocol.js`

## 已知待办
- 巴法云需要在微信公众平台加 request 合法域名（开发阶段已在 `project.private.config.json`
  关闭 `urlCheck`）。
- vant 的 icon 字体走 `img01.yzcdn.cn` CDN，本项目刻意只用「图片路径当 icon name」的用法，
  不依赖字体，因此断网也不会出现空白图标。
