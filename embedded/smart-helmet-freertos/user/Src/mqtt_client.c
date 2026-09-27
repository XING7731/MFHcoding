/* =========================================================================
 * 极简 MQTT 3.1.1 客户端 (QoS 0, 明文 TCP, 无 TLS)
 *
 * 报文长度编码、CONNECT / PUBLISH / PINGREQ / DISCONNECT 的字段布局
 * 均按 MQTT 3.1.1 规范实现, 便于逐字节核对。
 *
 * 明确未实现(不做任何假设):
 *   - QoS 1/2 与 PUBACK/PUBREC 流程 (因此设备侧无法确认云端已收到)
 *   - 下行控制报文的解析 (已订阅主题以保证控制台在线, 但收到的下行
 *     报文不做任何处理 —— 本设备不需要被远端控制)
 *   - 遗嘱消息 (Will)
 *   - TLS: STM32F103C8T6 的 RAM/Flash 与 ESP01S 的 AT 透传模式下都不适合
 *     直接做 TLS, 本固件不声称具备加密传输能力。
 * 线程安全: 全部报文缓冲为文件级静态区(不占任务栈), 只允许 WiFi_Task 调用。
 * ========================================================================= */
#include "mqtt_client.h"
#include "esp01s.h"
#include "user_config.h"
#include "app_events.h"   /* 提供 MQTT_BUF_SIZE(报文缓冲长度), 缺它会报"未定义" */
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/* 等 CONNACK 的超时。
 * 巴法云正常在几十毫秒内就回; 给 10s 是为了容忍信号差时的重传。
 * 若一直等不到, 说明报文根本没到/被回程掐断, 与账号无关。 */
#define MQTT_CONNACK_TIMEOUT_MS   10000

/* 等 SUBACK 的超时。订阅是一问一答的短交互, 巴法云正常几十毫秒就回;
 * 给 6s 足够覆盖信号差的情况。超时只意味着"控制台还是离线", 不影响上报,
 * 所以不需要像 CONNACK 那样给 10s。 */
#define MQTT_SUBACK_TIMEOUT_MS    6000

static uint8_t s_pkt[MQTT_BUF_SIZE];
static uint8_t s_connack_rc = 0xFF;    /* 0xFF = 尚未收到 */
static uint8_t s_suback_rc  = 0xFF;    /* 0xFF = 尚未收到(见 MQTT_Subscribe) */

void MQTT_Init(void)
{
    memset(s_pkt, 0, sizeof(s_pkt));
    s_connack_rc = 0xFF;
    s_suback_rc  = 0xFF;
}

uint8_t MQTT_GetLastSubackRc(void)
{
    return s_suback_rc;
}

uint8_t MQTT_GetLastConnackRc(void)
{
    return s_connack_rc;
}

const char *MQTT_ConnackRcText(uint8_t rc)
{
    switch (rc)
    {
    case 0x00: return "accepted";
    case 0x01: return "protocol version not supported";
    case 0x02: return "client id rejected";
    case 0x03: return "server unavailable";
    case 0x04: return "bad username or password";
    case 0x05: return "not authorized";
    case 0xFF: return "no CONNACK received (timeout)";
    default:   return "unknown return code";
    }
}

/* 剩余长度变长编码: 每字节 7 bit 有效, 最高位为延续标志 */
static uint8_t enc_remaining_len(uint32_t len, uint8_t *out)
{
    uint8_t n = 0;

    do
    {
        uint8_t d = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0) d |= 0x80;
        out[n++] = d;
    } while (len > 0 && n < 4);

    return n;
}

static uint16_t put_str(uint8_t *buf, uint16_t idx, const char *s)
{
    uint16_t len = (uint16_t)strlen(s);
    buf[idx++] = (uint8_t)(len >> 8);
    buf[idx++] = (uint8_t)(len & 0xFF);
    memcpy(&buf[idx], s, len);
    return (uint16_t)(idx + len);
}

uint8_t MQTT_Connect(void)
{
    /* CONNACK 固定头的两个字节: 报文类型=0x20, 剩余长度=0x02。
     * 先按这两个字节定位, 再读它后面的"会话标志"和"返回码"——
     * 这样无论返回码是 0 还是非 0 都能拿到。 */
    static const uint8_t connack_hdr[2] = { 0x20, 0x02 };
    uint8_t  vh[10];
    /* MQTT 3.1.1 的 CONNECT 可变报头固定 10 字节:
     *   协议名长度(2) + "MQTT"(4) + 协议级别(1) + 连接标志(1) + 保持连接(2)
     * 注意: 这里若写成 vh[8], 后面写保持连接的两个字节会越界 2 字节,
     * armcc 会给出 C4488E "write to variable 'vh' with offset out of bounds"
     * 的告警 —— 这是必须修的实打实的缓冲区溢出, 不是无害提示。 */
    uint8_t  flags = 0x02;                 /* 清除会话 */
    uint8_t  n;
    uint16_t vh_len = 0;
    uint16_t remain;
    uint16_t idx;
    uint32_t t0;
    uint16_t pos;

    s_connack_rc = 0xFF;

    /* ---- 可变报头: 协议名 + 协议级别 + 连接标志 + 保持连接时间 ---- */
    vh[vh_len++] = 0x00;
    vh[vh_len++] = 0x04;
    memcpy(&vh[vh_len], "MQTT", 4); vh_len += 4;
    vh[vh_len++] = 0x04;                   /* 协议级别 4 = MQTT 3.1.1 */

    /* 巴法云"方式一"是私钥当 ClientID、用户名密码留空, 因此下面两个标志
     * 默认都不置位。若某天需要改用"方式二"(用户名=appID, 密码=secretKey),
     * 只要在 user_config.h 里给 MQTT_USERNAME / MQTT_PASSWORD 填上值即可,
     * 这里会自动带上对应字段。 */
    if (MQTT_USERNAME[0] != '\0') flags |= 0x80;   /* bit7 用户名标志 */
    if (MQTT_PASSWORD[0] != '\0') flags |= 0x40;   /* bit6 密码标志 */
    vh[vh_len++] = flags;

    vh[vh_len++] = (uint8_t)(MQTT_KEEPALIVE_S >> 8);
    vh[vh_len++] = (uint8_t)(MQTT_KEEPALIVE_S & 0xFF);

    /* ---- 剩余长度 = 可变报头 + 载荷 ---- */
    remain = vh_len + (uint16_t)(2 + strlen(MQTT_CLIENT_ID));
    if (flags & 0x80) remain = (uint16_t)(remain + 2 + strlen(MQTT_USERNAME));
    if (flags & 0x40) remain = (uint16_t)(remain + 2 + strlen(MQTT_PASSWORD));

    if (remain > (MQTT_BUF_SIZE - 6)) return 1;   /* 客户端 ID/账号过长, 明确失败 */

    s_pkt[0] = 0x10;                       /* CONNECT 报文类型 */
    n = enc_remaining_len(remain, &s_pkt[1]);
    idx = (uint16_t)(1 + n);

    memcpy(&s_pkt[idx], vh, vh_len); idx = (uint16_t)(idx + vh_len);
    idx = put_str(s_pkt, idx, MQTT_CLIENT_ID);
    if (flags & 0x80) idx = put_str(s_pkt, idx, MQTT_USERNAME);
    if (flags & 0x40) idx = put_str(s_pkt, idx, MQTT_PASSWORD);

    ESP01S_RxClear();
    if (ESP01S_TcpSend(s_pkt, idx) != 0) return 1;

    /* ---- 等 CONNACK 并取出返回码 ---- */
    t0 = HAL_GetTick();
    do
    {
        if (ESP01S_RxFind(connack_hdr, 2, &pos))
        {
            uint8_t sf, rc;

            /* [pos] = 0x20, [pos+1] = 0x02, [pos+2] = 会话标志, [pos+3] = 返回码 */
            if (ESP01S_RxPeek((uint16_t)((pos + 2) % ESP_RXBUF_SIZE), &sf) &&
                ESP01S_RxPeek((uint16_t)((pos + 3) % ESP_RXBUF_SIZE), &rc))
            {
                (void)sf;                      /* 会话标志当前用不到, 取出仅为跨过它 */
                s_connack_rc = rc;
                return (rc == 0) ? 0 : 2;      /* 非 0 = Broker 明确拒绝 */
            }
        }

        /* Broker 若不接受就直接掐断 TCP, 模块会报 CLOSED —— 这时没必要空等。
         * 注意此时 s_connack_rc 仍是 0xFF, 表示"没收到任何 CONNACK"。 */
        if (ESP01S_RxContains("CLOSED", 6)) return 3;

        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((HAL_GetTick() - t0) < MQTT_CONNACK_TIMEOUT_MS);

    return 3;
}

/* ============================================================================
 * SUBSCRIBE (0x82) —— 让巴法云控制台把设备显示为"在线"
 *
 * 【为什么必须发这一条】
 *   实测巴法云设备接口(apis.bemfa.com/vb/api/v2/allTopic)对每个主题返回:
 *       online     -> 该主题有没有【订阅者】在线   (控制台图标看的是它)
 *       pubOnline  -> 该主题的【发布者】是否在线
 *   同一私钥下: 只连接+发布时 online=false; 一旦 SUBSCRIBE 该主题并保持
 *   连接, online 立刻变 true; 断开又变 false。
 *   所以"数据已经上云、pubOnline=true, 但控制台显示离线"不是链路故障,
 *   而是漏掉了订阅这一步。
 *
 * 报文布局(MQTT 3.1.1):
 *   固定头  : 0x82 <剩余长度>
 *   可变报头: 报文标识符(2 字节)
 *   载荷    : 主题长度(2) + 主题 + 请求的 QoS(1)
 * 应答 SUBACK:
 *   0x90 <剩余长度=3> <报文标识符 2 字节> <返回码 1 字节>
 *   返回码 0x00/0x01/0x02 = 按该 QoS 订阅成功; 0x80 = 失败(主题不存在等)。
 * ========================================================================== */
uint8_t MQTT_Subscribe(const char *topic_filter)
{
    /* SUBACK 固定头: 类型 0x90, 剩余长度 3。只订阅一个主题, 长度固定,
     * 因此可以用这两字节精确定位到 SUBACK 的起点。 */
    static const uint8_t suback_hdr[2] = { 0x90, 0x03 };
    uint16_t tlen;
    uint16_t remain;
    uint16_t idx;
    uint8_t  n;
    uint32_t t0;
    uint16_t pos;

    s_suback_rc = 0xFF;

    if (topic_filter == 0 || topic_filter[0] == '\0') return 1;

    tlen   = (uint16_t)strlen(topic_filter);
    remain = (uint16_t)(2 + 2 + tlen + 1);      /* 报文标识符 + 主题长度+主题+QoS */
    if (remain > (MQTT_BUF_SIZE - 5)) return 1;

    s_pkt[0] = 0x82;                            /* SUBSCRIBE */
    n   = enc_remaining_len(remain, &s_pkt[1]);
    idx = (uint16_t)(1 + n);

    s_pkt[idx++] = 0x00;                        /* 报文标识符 = 1 */
    s_pkt[idx++] = 0x01;                        /* (设备只订阅一次, 固定值即可) */

    s_pkt[idx++] = (uint8_t)(tlen >> 8);
    s_pkt[idx++] = (uint8_t)(tlen & 0xFF);
    memcpy(&s_pkt[idx], topic_filter, tlen); idx = (uint16_t)(idx + tlen);

    s_pkt[idx++] = 0x00;                        /* 请求 QoS 0, 与上报保持一致 */

    ESP01S_RxClear();
    if (ESP01S_TcpSend(s_pkt, idx) != 0) return 1;

    t0 = HAL_GetTick();
    do
    {
        if (ESP01S_RxFind(suback_hdr, 2, &pos))
        {
            uint8_t rc;

            /* [pos]=0x90, [pos+1]=剩余长度, [pos+2..3]=报文标识符, [pos+4]=返回码 */
            if (ESP01S_RxPeek((uint16_t)((pos + 4) % ESP_RXBUF_SIZE), &rc))
            {
                s_suback_rc = rc;
                return (rc <= 0x02) ? 0 : 2;    /* 0x80 = 订阅被拒绝 */
            }
        }

        /* 若 Broker 直接掐断 TCP, 模块会报 CLOSED, 没必要空等 */
        if (ESP01S_RxContains("CLOSED", 6)) return 3;

        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((HAL_GetTick() - t0) < MQTT_SUBACK_TIMEOUT_MS);

    return 3;
}

uint8_t MQTT_Publish(const char *topic, const char *payload, uint16_t len)
{
    uint16_t idx = 1;
    uint8_t  n;
    uint16_t tlen = (uint16_t)strlen(topic);
    uint16_t remain = (uint16_t)(2 + tlen + len);

    if (remain > (MQTT_BUF_SIZE - 5)) return 1;    /* 缓冲区不足, 明确失败而不是截断 */

    s_pkt[0] = 0x30;                               /* PUBLISH, QoS 0 */
    n = enc_remaining_len(remain, &s_pkt[1]);
    idx = (uint16_t)(1 + n);

    s_pkt[idx++] = (uint8_t)(tlen >> 8);
    s_pkt[idx++] = (uint8_t)(tlen & 0xFF);
    memcpy(&s_pkt[idx], topic, tlen); idx = (uint16_t)(idx + tlen);
    memcpy(&s_pkt[idx], payload, len); idx = (uint16_t)(idx + len);

    return ESP01S_TcpSend(s_pkt, idx);
}

uint8_t MQTT_Ping(void)
{
    s_pkt[0] = 0xC0;      /* PINGREQ */
    s_pkt[1] = 0x00;
    return ESP01S_TcpSend(s_pkt, 2);
}

void MQTT_Disconnect(void)
{
    s_pkt[0] = 0xE0;      /* DISCONNECT */
    s_pkt[1] = 0x00;
    (void)ESP01S_TcpSend(s_pkt, 2);
}
