typedef unsigned char uint8;             //数据类型定义
typedef unsigned short int  uint16;

void DelayMS(uint16 msec)
{
    uint16 i,j;
	for (i=0; i<msec; i++)
		for (j=0; j<536; j++);     //时间轴测量脉冲时间
}