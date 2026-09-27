#ifndef __USER_CONFIG_H
#define __USER_CONFIG_H
/* =========================================================
 * 用户配置文件 —— 只需要修改这一个文件
 * ========================================================= */

/* -------- 1. 巴法云账号信息 (https://bemfa.com 免费注册) --------
 * 登录控制台后，在"控制台 -> 私钥"处复制你的私钥(_uid)
 * 主题名可以自定义，TCP创客云主题以 001 结尾 (如 "temp001")
 * 在控制台 -> TCP创客云 处点击"添加主题"创建                */
#define BEMFA_UID       "把这里换成你的巴法云私钥"
#define BEMFA_TOPIC     "temp001"          /* 上传温湿度的主题名 */

/* -------- 2. WiFi 热点信息 --------
 * 手机热点: SSID = "0001", 密码 = "zxc145236"
 * 注意: ESP8266 只支持 2.4GHz。手机热点需在设置里把频段改为 2.4GHz,
 *       若为"5GHz 优先"或"双频合一", 模块会搜不到热点。 */
#define WIFI_SSID       "0001"
#define WIFI_PASSWORD   "zxc145236"

/* -------- 3. 硬件引脚 --------
 * DHT11 DATA  -> PA1
 * ESP8266     -> USART1 (PA9-TX / PA10-RX), 波特率115200
 * 调试串口    -> USART2 (PA2-TX / PA3-RX), 波特率115200        */
#define DHT11_PORT      GPIOA
#define DHT11_PIN       GPIO_PIN_1

/* -------- 4. 采样与心跳周期 (毫秒) -------- */
#define SAMPLE_PERIOD_MS    6000    /* 每6秒采集并上传一次 */
#define HEARTBEAT_PERIOD_MS 25000   /* 巴法云要求30秒内有心跳, 25秒发一次 */

/* -------- 5. 上传格式 -------- */
/* 巴法云消息内容为任意字符串, 这里上传 "温度,湿度" 如 "25.6,60" */
#define UPLOAD_FORMAT   "%.1f,%d"

#endif /* __USER_CONFIG_H */
