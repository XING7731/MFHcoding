#include "main.h"
#include "RC522.h"
#include <string.h>


/*==================== 内部常量 ====================*/
#define CARD_BLOCK_DEFAULT  0
#define CARD_KEY_DEFAULT    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}

/*==================== 内部全局变量 ====================*/
static uint8_t IC_UID[4];          // 卡片序列号
static uint8_t IC_Type[2];         // 卡类型
uint8_t Card_Data[16];             // 读取出的块数据（外部可见）
uint8_t Write_Card_Data[16] = {0}; // 写入缓冲（外部可见）

static const uint8_t card_1[4] = {0xC3,0xB2,0x37,0xC5};  // 蓝卡1
static const uint8_t card_2[4] = {0xA3,0x09,0x3C,0xFB};  // 白卡2
static const uint8_t Card_KEY[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint8_t ID_num = 0;          // 当前识别出的卡序号（1或2）

/*==================== 内部辅助函数原型 ====================*/
static void print_card_type(uint16_t cardType);
static void process_identified_card(uint8_t *uid);
static void handle_registration(void);
static void handle_deletion(void);
static void handle_parking(void);
static void show_parking_result(uint8_t idx, uint8_t inH, uint8_t inM, uint8_t inS,
                                uint8_t outH, uint8_t outM, uint8_t outS);
static void display_loading_animation(uint8_t *idx);

/*==================== 公共接口 ====================*/

/**
 * @brief RC522 初始化
 */
void RC522_Init(void)
{
    RC522_SPI_GPIO_Init();
    delay_ms(50);
    PcdReset();
    delay_ms(10);
    PcdAntennaOff();
    delay_ms(10);
    PcdAntennaOn();
    Serial_PutString("RFID-MFRC522 初始化完成\nPress KEY0 Or KEY1 To Start...\r\n");
}

/**
 * @brief RC522 操作主函数（读/写/注册/删除/停车）
 * @param block     块地址
 * @param option    readID 或 writeID
 * @param write_data 写入数据（仅当 option==writeID 时有效）
 */
void RC522_Start(uint8_t block, uint8_t option, uint8_t *write_data)
{
    static uint8_t anim_idx = 0;

    /*----- 寻卡 -----*/
    if (PcdRequest(PICC_REQALL, IC_Type) != MI_OK) {
        Serial_PutString("\r\n寻卡失败\r\n");
        if (setn == 0 && (Register == 1 || Delete == 1)) {
            display_loading_animation(&anim_idx);
        } else {
            InitDisplay = 1;
        }
        goto antenna_toggle;
    }

    /*----- 打印卡类型 -----*/
    uint16_t cardType = (IC_Type[0] << 8) | IC_Type[1];
    print_card_type(cardType);

    /*----- 防冲撞 -----*/
    if (PcdAnticoll(IC_UID) != MI_OK) {
        Serial_PutString("\r\n防冲撞失败\r\n");
        goto antenna_toggle;
    }

    /*----- 识别卡号（蓝卡/白卡） -----*/
    process_identified_card(IC_UID);

    /*----- 选卡 -----*/
    if (PcdSelect(IC_UID) != MI_OK) {
        Serial_PutString("\r\n选卡失败\r\n");
        goto antenna_toggle;
    }

    /*----- 验证密钥 -----*/
    if (PcdAuthState(PICC_AUTHENT1A, block, (uint8_t*)Card_KEY, IC_UID) != MI_OK) {
        Serial_PutString("\r\n验证失败\r\n");
        goto antenna_toggle;
    }

    /*----- 执行读/写操作 -----*/
    if (option == readID) {
        if (PcdRead(block, Card_Data) == MI_OK) {
            Serial_PutString("\n读取结果: \n");
            Serial_Printf("block %d date:\r\n", block);
            BEEP_DOING();
            for (int i = 0; i < 16; i++) {
                Serial_Printf("%02X ", Card_Data[i]);
            }
            Serial_PutString("\r\n");

            if (setn == 0 && (Register == 1 || Delete == 1)) {
                 sprintf(display,"IC:%02X%02X%02X%02X",Card_Data[0],Card_Data[1],Card_Data[2],Card_Data[3]);
									OLED_ShowStr(0,3,(uint8_t *)display,2,0);//显示卡号
									HAL_Delay(1000);
            }

            // 根据当前模式进入对应处理
            if (setn == 0) {
                if (Register == 1 && Delete == 0) {
                    handle_registration();
                } else if (Register == 0 && Delete == 1) {
                    handle_deletion();
                } else if (Register == 0 && Delete == 0) {
                    handle_parking();
                }
            }
        }
    } else if (option == writeID) {
        if (PcdWrite(block, write_data) == MI_OK) {
            delay_ms(8);
            if (PcdRead(block, Card_Data) == MI_OK) {
                Serial_PutString("\n写入完成\n");
                Serial_Printf("block %d date:\r\n", block);
                for (int i = 0; i < 16; i++) {
                    Serial_Printf("%02X ", Card_Data[i]);
                }
                Serial_PutString("\r\n");
            }
        }
    }

antenna_toggle:
    PcdAntennaOff();
    delay_ms(10);
    PcdAntennaOn();
}

/*==================== 辅助函数实现 ====================*/

static void print_card_type(uint16_t cardType)
{
    const char *typeStr;
    switch (cardType) {
        case 0x4400: typeStr = "Mifare UltraLight"; break;
        case 0x0400: typeStr = "Mifare One(S50)"; break;
        case 0x0200: typeStr = "Mifare One(S70)"; break;
        case 0x0800: typeStr = "Mifare Pro(X)"; break;
        case 0x4403: typeStr = "Mifare DESFire"; break;
        default:     typeStr = "Unknown Card"; break;
    }
    Serial_Printf("\r\n%s\r\n", typeStr);
}

static void process_identified_card(uint8_t *uid)
{
    if (memcmp(uid, card_1, 4) == 0) {
        ID_num = 1;
        Serial_PutString("The User is: 1, Blue card");
    } else if (memcmp(uid, card_2, 4) == 0) {
        ID_num = 2;
        Serial_PutString("The User is: 2, BLACK card");
    } else {
        ID_num = 0;
    }
    Serial_Printf("\r\ncard_ID: %02X:%02X:%02X:%02X\r\n", uid[0], uid[1], uid[2], uid[3]);
}

/* 注册处理（新增卡号到数组） - 修正：使用 memcpy 存储完整16字节 */
static void handle_registration(void)
{
    uint8_t zero[16] = {0};

    // 检查是否已存在
    if (memcmp(ucArray1_ID, Card_Data, 16) == 0 ||
        memcmp(ucArray2_ID, Card_Data, 16) == 0 ||
        memcmp(ucArray3_ID, Card_Data, 16) == 0) {
         OLED_ShowStr(0,3,(uint8_t *)"Save Fail  ",2,0);
        return;
    }

    // 已满，拒绝注册，并播报语音
    if (cardNum >= 3) {
        OLED_ShowStr(0,3,(uint8_t *)"Save Fail  ",2,0);
//        JQ8900PlayTrackNum(17);             // 播放：当前车位已满，请去别处停车！
        HAL_Delay(1000);                    // 等待语音播放
        Register = 0;
        OLED_CLS();
        InitDisplay = 1;
        return;
    }

    // 查找第一个全零的空位
    if (memcmp(ucArray1_ID, zero, 16) == 0) {
        memcpy(ucArray1_ID, Card_Data, 16);
        cardNum++;
    } else if (memcmp(ucArray2_ID, zero, 16) == 0) {
        memcpy(ucArray2_ID, Card_Data, 16);
        cardNum++;
    } else if (memcmp(ucArray3_ID, zero, 16) == 0) {
        memcpy(ucArray3_ID, Card_Data, 16);
        cardNum++;
    } else {
        // 理论上不会执行到这里（因为已检查 cardNum >= 3）
        OLED_ShowStr(0,3,(uint8_t *)"Save Fail  ",2,0);
        HAL_Delay(1000);
        Register = 0;
        OLED_CLS();
        InitDisplay = 1;
        return;
    }

    OLED_ShowStr(0,3,(uint8_t *)"Save OK    ",2,0);//显示卡号
    HAL_Delay(1000);
    Register = 0;
    OLED_CLS();
    InitDisplay = 1;
}

/* 删除处理（移除卡号） - 使用 memcmp 比较 */
static void handle_deletion(void)
{
    if (memcmp(ucArray1_ID, Card_Data, 16) == 0) {
        car1 = 0;
        memset(ucArray1_ID, 0, sizeof(ucArray1_ID));
        cardNum--;
        OLED_ShowStr(0,3,(uint8_t *)"Delete OK  ",2,0);
    } else if (memcmp(ucArray2_ID, Card_Data, 16) == 0) {
        car2 = 0;
        memset(ucArray2_ID, 0, sizeof(ucArray2_ID));
        cardNum--;
        OLED_ShowStr(0,3,(uint8_t *)"Delete OK  ",2,0);
    } else if (memcmp(ucArray3_ID, Card_Data, 16) == 0) {
        car3 = 0;
        memset(ucArray3_ID, 0, sizeof(ucArray3_ID));
        cardNum--;
        OLED_ShowStr(0,3,(uint8_t *)"Delete OK  ",2,0);
    } else {
        OLED_ShowStr(0,3,(uint8_t *)"Delete Fail",2,0);
			HAL_Delay(1000);
        return;
    }
    HAL_Delay(1000);
    Delete = 0;
    OLED_CLS();
    InitDisplay = 1;
}

static uint8_t prevInfra[3] = {1, 1, 1};    // 上次红外状态

// ========== 新增：红外状态更新函数（需在主循环中周期性调用） ==========
void RC522_InfraredTick(void)
{
    uint8_t infra[3] = {INFRA1, INFRA2, INFRA3};
    uint8_t *carFlags[] = { &car1, &car2, &car3 };
    uint8_t *inH[] = { &inHour1, &inHour2, &inHour3 };
    uint8_t *inM[] = { &inMin1, &inMin2, &inMin3 };
    uint8_t *inS[] = { &inSec1, &inSec2, &inSec3 };
    uint8_t *outH[] = { &outHour1, &outHour2, &outHour3 };
    uint8_t *outM[] = { &outMin1, &outMin2, &outMin3 };
    uint8_t *outS[] = { &outSec1, &outSec2, &outSec3 };

    for (int i = 0; i < 3; i++) {
        // 无车 -> 有车 (车辆进入车位)
        if (prevInfra[i] == 1 && infra[i] == 0) {
					 // 控制对应继电器打开
            if (i == 0) { RELAY(ON); }
            else if (i == 1) { RELAY1(ON); }
            else if (i == 2) { RELAY2(ON); }
						
            if (*carFlags[i] == 1) {   // 已刷卡授权
                // 记录入场时间
                *inH[i] = SysDate.hour;
                *inM[i] = SysDate.min;
                *inS[i] = SysDate.sec;
                *carFlags[i] = 2;      // 标记已入场完成
                // 可选：打开闸门？但闸门通常在刷卡时已开，这里可不再操作
            }
        }
        // 有车 -> 无车 (车辆离开车位)
        else if (prevInfra[i] == 0 && infra[i] == 1) {
					 // 控制对应继电器关闭
            if (i == 0) { RELAY(OFF); }
            else if (i == 1) { RELAY1(OFF); }
            else if (i == 2) { RELAY2(OFF); }
            if (*inH[i] != 0 && *outH[i] == 0) { // 已有入场时间且未记录出场
                // 记录出场时间
                *outH[i] = SysDate.hour;
                *outM[i] = SysDate.min;
                *outS[i] = SysDate.sec;
//                *carFlags[i] = 0;      // 标记空闲
            }
        }
        prevInfra[i] = infra[i];
    }
}


 uint16_t cost = 0;
/* 停车进出处理 - 使用 memcmp 比较 */
static void handle_parking(void)
{
    uint8_t *carFlags[] = { &car1, &car2, &car3 };
    uint8_t *inH[] = { &inHour1, &inHour2, &inHour3 };
    uint8_t *inM[] = { &inMin1, &inMin2, &inMin3 };
    uint8_t *inS[] = { &inSec1, &inSec2, &inSec3 };
    uint8_t *outH[] = { &outHour1, &outHour2, &outHour3 };
    uint8_t *outM[] = { &outMin1, &outMin2, &outMin3 };
    uint8_t *outS[] = { &outSec1, &outSec2, &outSec3 };
    uint8_t *ids[] = { ucArray1_ID, ucArray2_ID, ucArray3_ID };

    for (int i = 0; i < 3; i++) {
        if (memcmp(ids[i], Card_Data, 16) == 0) {
            // ----- 场景1：完全空闲 → 入场授权 -----
            if (*carFlags[i] == 0 && *inH[i] == 0 && *outH[i] == 0) {
                *carFlags[i] = 1;               // 等待红外确认
                PWM_Val = 25;
                Flag_15s = 0;
                doorFlag = 1;
                // JQ8900PlayTrackNum(18);
                break;
            }

            // ----- 场景2 & 3：已有入场时间 → 出场结算 -----
            if (*inH[i] != 0) {
                OLED_CLS();
								PWM_Val = 25;
                Flag_15s = 0;
                doorFlag = 1;
                // JQ8900PlayTrackNum(18);
							
                // 若红外未记录出场时间，使用当前系统时间补全（场景3）
                if (*outH[i] == 0) {
                    *outH[i] = SysDate.hour;
                    *outM[i] = SysDate.min;
                    *outS[i] = SysDate.sec;
//                    // 可显示提示：按当前时间结算
//                    sprintf(display, "NOW SET OUT");
//                    OLED_ShowStr(0, 3, (uint8_t *)display, 2, 0);
                }

                // 计算并显示费用
                show_parking_result(i, *inH[i], *inM[i], *inS[i],
                                      *outH[i], *outM[i], *outS[i]);

                // 保存费用
                if (i == 0) pay1 = cost;
                else if (i == 1) pay2 = cost;
                else if (i == 2) pay3 = cost;

                // 清空所有记录，释放车位
                *inH[i] = *inM[i] = *inS[i] = 0;
                *outH[i] = *outM[i] = *outS[i] = 0;
                *carFlags[i] = 0;

                HAL_Delay(3000);
                OLED_CLS();
                InitDisplay = 1;
                break;
            }

            // ----- 场景2（无红外）：已授权但红外从未触发 -----
            if (*carFlags[i] == 1 && *inH[i] == 0) {
                OLED_CLS();
							
								PWM_Val = 25;
                Flag_15s = 0;
                doorFlag = 1;
                // JQ8900PlayTrackNum(18);
							
                cost = 0;
                sprintf(display, "%4dRMB", cost);
                OLED_ShowStr(32, 3, (uint8_t *)display, 2, 1);
                // 清空所有记录，释放车位
                *inH[i] = *inM[i] = *inS[i] = 0;
                *outH[i] = *outM[i] = *outS[i] = 0;
                *carFlags[i] = 0;
                HAL_Delay(2000);
                OLED_CLS();
                InitDisplay = 1;
                break;
            }

            // 其他异常状态忽略
            break;
        }
    }
}

/* 显示停车费用并播报语音 */
static void show_parking_result(uint8_t idx, uint8_t inH, uint8_t inM, uint8_t inS,
                                uint8_t outH, uint8_t outM, uint8_t outS)
{
    // 计算费用（单位：分钟？这里按秒差计算，实际业务可调整）
    cost = timeDifference(inS, inM, inH, outS, outM, outH) * seccost;

    // oled显示
		sprintf(display, "%4dRMB", cost);
		OLED_ShowStr(32,2,(uint8_t *)display,2,1);//显示卡号
    sprintf(display, "START:%02d:%02d:%02d", inH, inM, inS);
		OLED_ShowStr(0,4,(uint8_t *)display,2,0);//显示卡号
    sprintf(display, "END:%02d:%02d:%02d", outH, outM, outS);
		OLED_ShowStr(0,6,(uint8_t *)display,2,0);//显示卡号
	

//		doorFlag = 1;            // 标记闸门已打开
//		JQ8900PlayTrackNum(18);              // 播放：闸门已打开
//		HAL_Delay(1500);
//    // 语音播报费用
//    JQ8900PlayTrackNum(19);          // 播放：你需要支付停车费
//    HAL_Delay(2000);
//    JQ8900PlayNumber(cost);          // 播放具体金额
//    HAL_Delay(1000);
//    JQ8900PlayTrackNum(20);          // 播放：元，欢迎下次停车！
}

/* 寻卡动画（仅用于显示等待） */
static void display_loading_animation(uint8_t *idx)
{
    static const char* frames[] = {
        "      ()        ",
        "     (())       ",
        "    ((()))      ",
        "   (((())))     ",
        "  ((((()))))    ",
        "                "
    };
    OLED_ShowStr(0, 3, (uint8_t*)frames[*idx], 2, 0);
    (*idx)++;
    if (*idx >= 6) *idx = 0;
}

/*==================== 工具函数 ====================*/
uint8_t timeDifference(uint8_t ins, uint8_t inm, uint8_t inh,
                       uint8_t outs, uint8_t outm, uint8_t outh)
{
    uint16_t total1 = inh * 3600 + inm * 60 + ins;
    uint16_t total2 = outh * 3600 + outm * 60 + outs;
    return (total1 > total2) ? (total1 - total2) : (total2 - total1);
}

/*==================== SPI 读写（带超时） ====================*/
int SPI_WriteNBytes(SPI_TypeDef* SPIx, uint8_t *p_TxData, uint32_t sendDataNum)
{
    while (sendDataNum--) {
        uint32_t retry = 0;
        while ((SPIx->SR & SPI_FLAG_TXE) == 0) {
            if (++retry > 20000) return -1;
        }
        SPIx->DR = *p_TxData++;
        retry = 0;
        while ((SPIx->SR & SPI_FLAG_RXNE) == 0) {
            SPIx->SR = SPIx->SR;
            if (++retry > 20000) return -1;
        }
        (void)SPIx->DR;  // 读空
    }
    return 0;
}

int SPI_ReadNBytes(SPI_TypeDef* SPIx, uint8_t *p_RxData, uint32_t readDataNum)
{
    while (readDataNum--) {
        uint32_t retry = 0;
        SPIx->DR = 0xFF;
        while ((SPIx->SR & SPI_FLAG_TXE) == 0) {
            if (++retry > 20000) return -1;
        }
        retry = 0;
        while ((SPIx->SR & SPI_FLAG_RXNE) == 0) {
            if (++retry > 20000) return -1;
        }
        *p_RxData++ = SPIx->DR;
    }
    return 0;
}

/*==================== RC522 硬件底层函数 ====================*/

void RC522_SPI_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;
    RC522_GPIO_Reset_CLK_ENABLE();
    RC522_GPIO_CS_CLK_ENABLE();

    GPIO_Initure.Pin = RC522_RST_Pin;
    GPIO_Initure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_RST_GPIO_Port, &GPIO_Initure);

    GPIO_Initure.Pin = RC522_GPIO_CS_PIN;
    HAL_GPIO_Init(RC522_GPIO_CS_PORT, &GPIO_Initure);
}

/* 以下函数（PcdComMF522, PcdRequest, PcdAnticoll, PcdSelect, PcdAuthState, 
   PcdRead, PcdWrite, PcdValue, PcdBakValue, PcdHalt, CalulateCRC, PcdReset,
   ReadRawRC, WriteRawRC, SetBitMask, ClearBitMask, PcdAntennaOn, PcdAntennaOff,
   RC522_Config, test_RC522）保持与之前完全相同，为避免篇幅重复，仅列出函数名，实际请保留完整实现。
   注：此处为完整代码，故补充所有函数体（与优化版一致，未作修改）*/

// 以下为底层函数完整实现（与优化版相同，仅注释说明）

char PcdComMF522(uint8_t Command, uint8_t *pInData, uint8_t InLenByte,
                 uint8_t *pOutData, uint32_t *pOutLenBit)
{
    char status = MI_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitFor = 0x00;
    uint8_t lastBits, n;
    uint32_t i;

    switch (Command) {
        case PCD_AUTHENT:
            irqEn = 0x12;
            waitFor = 0x10;
            break;
        case PCD_TRANSCEIVE:
            irqEn = 0x77;
            waitFor = 0x30;
            break;
        default:
            break;
    }

    WriteRawRC(ComIEnReg, irqEn | 0x80);
    ClearBitMask(ComIrqReg, 0x80);
    WriteRawRC(CommandReg, PCD_IDLE);
    SetBitMask(FIFOLevelReg, 0x80);

    for (i = 0; i < InLenByte; i++) {
        WriteRawRC(FIFODataReg, pInData[i]);
    }
    WriteRawRC(CommandReg, Command);
    if (Command == PCD_TRANSCEIVE) {
        SetBitMask(BitFramingReg, 0x80);
    }

    i = 800;
    do {
        n = ReadRawRC(ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitFor));

    ClearBitMask(BitFramingReg, 0x80);

    if (i != 0) {
        if (!(ReadRawRC(ErrorReg) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;
            }
            if (Command == PCD_TRANSCEIVE) {
                n = ReadRawRC(FIFOLevelReg);
                lastBits = ReadRawRC(ControlReg) & 0x07;
                if (lastBits) {
                    *pOutLenBit = (n - 1) * 8 + lastBits;
                } else {
                    *pOutLenBit = n * 8;
                }
                if (n == 0) n = 1;
                if (n > MAXRLEN) n = MAXRLEN;
                for (i = 0; i < n; i++) {
                    pOutData[i] = ReadRawRC(FIFODataReg);
                }
            }
        } else {
            status = MI_ERR;
        }
    }

    SetBitMask(ControlReg, 0x80);
    WriteRawRC(CommandReg, PCD_IDLE);
    return status;
}

char PcdRequest(uint8_t req_code, uint8_t *pTagType)
{
    char status;
    uint32_t unLen;
    uint8_t ucComMF522Buf[MAXRLEN];

    ClearBitMask(Status2Reg, 0x08);
    WriteRawRC(BitFramingReg, 0x07);
    SetBitMask(TxControlReg, 0x03);

    ucComMF522Buf[0] = req_code;
    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 1, ucComMF522Buf, &unLen);

    if ((status == MI_OK) && (unLen == 0x10)) {
        *pTagType = ucComMF522Buf[0];
        *(pTagType + 1) = ucComMF522Buf[1];
    } else {
        status = MI_ERR;
    }
    return status;
}

char PcdAnticoll(uint8_t *pSnr)
{
    char status;
    uint8_t i, snr_check = 0;
    uint32_t unLen;
    uint8_t ucComMF522Buf[MAXRLEN];

    ClearBitMask(Status2Reg, 0x08);
    WriteRawRC(BitFramingReg, 0x00);
    ClearBitMask(CollReg, 0x80);

    ucComMF522Buf[0] = PICC_ANTICOLL1;
    ucComMF522Buf[1] = 0x20;

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 2, ucComMF522Buf, &unLen);
    if (status == MI_OK) {
        for (i = 0; i < 4; i++) {
            *(pSnr + i) = ucComMF522Buf[i];
            snr_check ^= ucComMF522Buf[i];
        }
        if (snr_check != ucComMF522Buf[i]) {
            status = MI_ERR;
        }
    }
    SetBitMask(CollReg, 0x80);
    return status;
}

char PcdSelect(uint8_t *pSnr)
{
    char status;
    uint8_t i;
    uint32_t unLen;
    uint8_t ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = PICC_ANTICOLL1;
    ucComMF522Buf[1] = 0x70;
    ucComMF522Buf[6] = 0;
    for (i = 0; i < 4; i++) {
        ucComMF522Buf[i + 2] = *(pSnr + i);
        ucComMF522Buf[6] ^= *(pSnr + i);
    }
    CalulateCRC(ucComMF522Buf, 7, &ucComMF522Buf[7]);

    ClearBitMask(Status2Reg, 0x08);
    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 9, ucComMF522Buf, &unLen);
    if ((status == MI_OK) && (unLen == 0x18)) {
        status = MI_OK;
    } else {
        status = MI_ERR;
    }
    return status;
}

char PcdAuthState(uint8_t auth_mode, uint8_t addr, uint8_t *pKey, uint8_t *pSnr)
{
    char status;
    uint32_t unLen;
    uint8_t i, ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = auth_mode;
    ucComMF522Buf[1] = addr;
    for (i = 0; i < 6; i++) {
        ucComMF522Buf[i + 2] = *(pKey + i);
    }
    for (i = 0; i < 6; i++) {
        ucComMF522Buf[i + 8] = *(pSnr + i);
    }

    status = PcdComMF522(PCD_AUTHENT, ucComMF522Buf, 12, ucComMF522Buf, &unLen);
    if ((status != MI_OK) || (!(ReadRawRC(Status2Reg) & 0x08))) {
        status = MI_ERR;
    }
    return status;
}

char PcdRead(uint8_t addr, uint8_t *pData)
{
    char status;
    uint32_t unLen;
    uint8_t i, ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = PICC_READ;
    ucComMF522Buf[1] = addr;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    if ((status == MI_OK) && (unLen == 0x90)) {
        for (i = 0; i < 16; i++) {
            *(pData + i) = ucComMF522Buf[i];
        }
    } else {
        status = MI_ERR;
    }
    return status;
}

char PcdWrite(uint8_t addr, uint8_t *pData)
{
    char status;
    uint32_t unLen;
    uint8_t i, ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = PICC_WRITE;
    ucComMF522Buf[1] = addr;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }
    if (status == MI_OK) {
        for (i = 0; i < 16; i++) {
            ucComMF522Buf[i] = *(pData + i);
        }
        CalulateCRC(ucComMF522Buf, 16, &ucComMF522Buf[16]);

        status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 18, ucComMF522Buf, &unLen);
        if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
            status = MI_ERR;
        }
    }
    return status;
}

char PcdValue(uint8_t dd_mode, uint8_t addr, uint8_t *pValue)
{
    char status;
    uint32_t unLen;
    uint8_t i, ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = dd_mode;
    ucComMF522Buf[1] = addr;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }
    if (status == MI_OK) {
        for (i = 0; i < 16; i++) {
            ucComMF522Buf[i] = *(pValue + i);
        }
        CalulateCRC(ucComMF522Buf, 4, &ucComMF522Buf[4]);
        unLen = 0;
        status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 6, ucComMF522Buf, &unLen);
        if (status != MI_ERR) {
            status = MI_OK;
        }
    }
    if (status == MI_OK) {
        ucComMF522Buf[0] = PICC_TRANSFER;
        ucComMF522Buf[1] = addr;
        CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

        status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
        if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
            status = MI_ERR;
        }
    }
    return status;
}

char PcdBakValue(uint8_t sourceaddr, uint8_t goaladdr)
{
    char status;
    uint32_t unLen;
    uint8_t ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = PICC_RESTORE;
    ucComMF522Buf[1] = sourceaddr;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }
    if (status == MI_OK) {
        ucComMF522Buf[0] = 0;
        ucComMF522Buf[1] = 0;
        ucComMF522Buf[2] = 0;
        ucComMF522Buf[3] = 0;
        CalulateCRC(ucComMF522Buf, 4, &ucComMF522Buf[4]);
        status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 6, ucComMF522Buf, &unLen);
        if (status != MI_ERR) {
            status = MI_OK;
        }
    }
    if (status != MI_OK) {
        return MI_ERR;
    }

    ucComMF522Buf[0] = PICC_TRANSFER;
    ucComMF522Buf[1] = goaladdr;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }
    return status;
}

char PcdHalt(void)
{
    uint32_t unLen;
    uint8_t ucComMF522Buf[MAXRLEN];

    ucComMF522Buf[0] = PICC_HALT;
    ucComMF522Buf[1] = 0;
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    PcdComMF522(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);
    return MI_OK;
}

void CalulateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData)
{
    uint8_t i, n;
    ClearBitMask(DivIrqReg, 0x04);
    WriteRawRC(CommandReg, PCD_IDLE);
    SetBitMask(FIFOLevelReg, 0x80);
    for (i = 0; i < len; i++) {
        WriteRawRC(FIFODataReg, *(pIndata + i));
    }
    WriteRawRC(CommandReg, PCD_CALCCRC);
    i = 0xFF;
    do {
        n = ReadRawRC(DivIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x04));
    pOutData[0] = ReadRawRC(CRCResultRegL);
    pOutData[1] = ReadRawRC(CRCResultRegM);
}

char PcdReset(void)
{
    RC522_Reset_Disable();
    delay_ms(10);
    RC522_Reset_Enable();
    delay_ms(60);
    RC522_Reset_Disable();
    delay_ms(500);

    WriteRawRC(CommandReg, PCD_RESETPHASE);
    delay_ms(2);

    WriteRawRC(ModeReg, 0x3D);
    WriteRawRC(TReloadRegL, 30);
    WriteRawRC(TReloadRegH, 0);
    WriteRawRC(TModeReg, 0x8D);
    WriteRawRC(TPrescalerReg, 0x3E);
    WriteRawRC(TxAutoReg, 0x40);

    ClearBitMask(TestPinEnReg, 0x80);
    WriteRawRC(TxAutoReg, 0x40);

    return MI_OK;
}

uint8_t ReadRawRC(uint8_t Address)
{
    uint8_t ucAddr = ((Address << 1) & 0x7E) | 0x80;
    uint8_t ucResult = 0;
    delay_ms(1);
    RC522_CS_Enable();
    SPI_WriteNBytes(SPI2, &ucAddr, 1);
    SPI_ReadNBytes(SPI2, &ucResult, 1);
    RC522_CS_Disable();
    return ucResult;
}

void WriteRawRC(uint8_t Address, uint8_t value)
{
    uint8_t ucAddr = ((Address << 1) & 0x7E);
    uint8_t write_buffer[2] = {ucAddr, value};
    delay_ms(1);
    RC522_CS_Enable();
    SPI_WriteNBytes(SPI2, write_buffer, 2);
    RC522_CS_Disable();
}

void SetBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t temp = ReadRawRC(reg);
    WriteRawRC(reg, temp | mask);
}

void ClearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t temp = ReadRawRC(reg);
    WriteRawRC(reg, temp & ~mask);
}

void PcdAntennaOn(void)
{
    uint8_t i = ReadRawRC(TxControlReg);
    if (!(i & 0x03)) {
        SetBitMask(TxControlReg, 0x03);
    }
}

void PcdAntennaOff(void)
{
    ClearBitMask(TxControlReg, 0x03);
}

void RC522_Config(uint8_t Card_Type)
{
    (void)Card_Type;
    ClearBitMask(Status2Reg, 0x08);
    WriteRawRC(ModeReg, 0x3D);
    WriteRawRC(RxSelReg, 0x86);
    WriteRawRC(RFCfgReg, 0x7F);
    WriteRawRC(TReloadRegL, 30);
    WriteRawRC(TReloadRegH, 0);
    WriteRawRC(TModeReg, 0x8D);
    WriteRawRC(TPrescalerReg, 0x3E);
    delay_ms(5);
    PcdAntennaOn();
}

/* 测试函数 */
void test_RC522(void)
{
    RC522_Init();
    while (1) {
        RC522_Start(0, readID, Write_Card_Data);
    }
}

