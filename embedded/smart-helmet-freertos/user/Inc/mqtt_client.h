#ifndef __MQTT_CLIENT_H
#define __MQTT_CLIENT_H

#include "stm32f1xx_hal.h"

/* ============================================================================
 * 极简 MQTT 3.1.1 客户端 (QoS 0, 无 TLS)
 *
 * 为什么自己拼报文而不是用 AT 的 MQTT 指令:
 *   ESP01S 常见出厂 AT 固件对 MQTT 扩展指令(AT+MQTTUSERCFG 等)的支持情况
 *   随固件版本差异较大 —— 因此改用最通用的做法: 用 AT 建立 TCP Socket,
 *   在其上发送手写的 MQTT 3.1.1 报文。只依赖 TCP 相关 AT 指令, 兼容性最好,
 *   报文格式也完全可审计。
 *
 * 实现了 MQTT 3.1.1 的 5 种报文:
 *   CONNECT (0x10) / SUBSCRIBE (0x82) / PUBLISH QoS0 (0x30)
 *   PINGREQ (0xC0) / DISCONNECT (0xE0)
 *   报文长度采用变长编码(Remaining Length, 最多 4 字节)。
 *
 * 【为什么要发 SUBSCRIBE —— 这是"数据已上云, 但控制台显示离线"的唯一原因】
 *   巴法云控制台上那个在线/离线图标, 判据既不是 TCP 是否连着, 也不是有
 *   没有在发布数据, 而是【这个主题上有没有订阅者】。巴法云设备接口
 *   (cloud.bemfa.com/docs/src/api_device.html) 对每个主题返回两个独立字段:
 *       online     该主题「有没有订阅者在线」   <-- 控制台显示的就是这个
 *       pubOnline  该主题「发布者是否在线」
 *   实测(同一私钥):不订阅时 online=false / pubOnline=true; 一旦连上去
 *   SUBSCRIBE 该主题并保持连接, online 立刻变 true; 断开又变回 false。
 *   → 只发 PUBLISH 的设备, 数据能上云、控制台也能看到最新消息, 但图标永远
 *     是灰的。补一条 SUBSCRIBE 并维持长连接即可。
 *   → 【每次重连后都要重新订阅】: CONNECT 用的是 CleanSession=1, Broker
 *     不会替设备保留订阅关系, 所以订阅必须放在"连接成功之后"这一步。
 *
 * 未实现(明确声明, 不做假设):
 *   QoS1/2、遗嘱、TLS/SSL、自动重传与 ACK 处理。
 *   因此本客户端除了周期上报, 还额外发一条 SUBSCRIBE 让巴法云控制台
 *   显示在线 —— 两者互不干扰。
 *
 * 【CONNACK 返回码 —— 这是"连不上云端"时最关键的一条信息】
 *   CONNECT 发出去后, Broker 一定回一条 CONNACK: 0x20 0x02 <会话标志> <返回码>。
 *   返回码含义(MQTT 3.1.1 规范):
 *     0x00 接受          0x01 协议版本不支持
 *     0x02 ClientID 被拒 0x03 服务不可用
 *     0x04 用户名或密码错 0x05 未授权
 *   如果只匹配"成功报文 20 02 00 00", 那么一旦 Broker 返回非 0 码, 代码只能
 *   干等到超时, 现场表现就是"怎么都连不上, 又看不出原因"。
 *   因此本客户端把返回码解析出来交给上层打印/判断。
 * ========================================================================== */

void    MQTT_Init(void);                                      /* 清空报文缓冲, 上电调用 */

/* 连接结果:
 *   0 = 成功
 *   1 = TCP 层发送失败(链路问题, 不是账号问题)
 *   2 = Broker 明确拒绝(CONNACK 返回码非 0, 用 MQTT_GetLastConnackRc 取)
 *   3 = 超时: 完全没收到 CONNACK(链路被中间设备掐断 / 报文被丢弃) */
uint8_t MQTT_Connect(void);

uint8_t MQTT_GetLastConnackRc(void);   /* 最近一次 CONNACK 返回码; 0xFF = 没收到 */
const char *MQTT_ConnackRcText(uint8_t rc);  /* 返回码的中文解释(便于打印) */

/* 订阅主题 —— 【让巴法云控制台显示"在线"的必要条件】。
 *
 * 只发 PUBLISH 的设备在云端永远是"离线": 控制台的在线判据是"该主题有没有
 * 订阅者"。因此在 MQTT_Connect 成功之后必须紧接着调用本函数, 并且:
 *   - 订阅的是【上报用的同一个裸主题】(MQTT_TOPIC_DATA), 不要加 /set、/up;
 *   - 每次重连后都要重新订阅(CleanSession=1 不保留订阅关系);
 *   - 订阅成功后只要 TCP 连接不断, 云端就一直显示在线, 不需要重复订阅。
 *
 * 返回: 0 = 订阅成功(收到 SUBACK 0x00/0x01/0x02)
 *       1 = 参数为空 / TCP 发送失败
 *       2 = Broker 拒绝订阅(SUBACK 返回码 >= 0x80, 通常是主题不存在或没权限)
 *       3 = 超时: 没收到 SUBACK
 * 订阅失败【不影响上报】—— 数据照样能发到云端, 只是控制台图标还是灰的。
 * 因此上层把它记为一条独立诊断(见 app_tasks.c 的 NET_DIAG_SUB_FAIL),
 * 不让它把整条链路判成"断线"。 */
uint8_t MQTT_Subscribe(const char *topic_filter);
uint8_t MQTT_GetLastSubackRc(void);    /* 最近一次 SUBACK 返回码; 0xFF = 没收到 */

uint8_t MQTT_Publish(const char *topic, const char *payload, uint16_t len);
uint8_t MQTT_Ping(void);
void    MQTT_Disconnect(void);

#endif /* __MQTT_CLIENT_H */
