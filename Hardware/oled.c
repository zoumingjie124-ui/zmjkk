/**
  ******************************************************************************
  * @file    oled.c
  * @brief   OLED(SSD1306 兼容) 驱动：软件模拟 I2C，HAL 版
  *
  * 移植说明（相对课程例程 4-1 的改动）：
  *   1. GPIO_WriteBit  -> HAL_GPIO_WritePin，引脚从 PB8/PB9 改为 PB6/PB7
  *   2. 引脚初始化从 OLED_I2C_Init() 移到 Core/Src/gpio.c 统一管理
  *      （CubeMX 风格：所有 GPIO 集中在 MX_GPIO_Init，驱动只管读写电平）
  *   3. 上电延时从 for 空循环改为 HAL_Delay(100)（初始化阶段允许阻塞，
  *      主循环里才禁止长时间 Delay）
  *   显示逻辑（时序、命令序列、字库索引）与原驱动完全一致，未做改动。
  ******************************************************************************
  */
#include "oled.h"
#include "OLED_Font.h"

/*引脚电平宏（引脚号统一来自 main.h）*/
#define OLED_W_SCL(x)		HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, (GPIO_PinState)(x))
#define OLED_W_SDA(x)		HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, (GPIO_PinState)(x))

/**
  * @brief  I2C开始
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_W_SDA(0);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(Byte & (0x80 >> i));
		OLED_W_SCL(1);
		OLED_W_SCL(0);
	}
	OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  */
void OLED_WriteData(uint8_t Data)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x40);		//写数据
	OLED_I2C_SendByte(Data);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/**
  * @brief  OLED清屏
  */
void OLED_Clear(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	uint8_t i;

	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);	//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);		//显示上半部分内容
	}

	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);	//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Length 要显示数字的长度，范围：1~10
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @note   引脚初始化在 gpio.c 的 MX_GPIO_Init 中完成，这里只做上电延时和命令配置
  */
void OLED_Init(void)
{
	HAL_Delay(100);				//上电延时，等待 SSD1306 内部复位完成

	OLED_WriteCommand(0xAE);	//关闭显示

	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);

	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);

	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);

	OLED_WriteCommand(0x40);	//设置显示开始行

	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置

	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/倒转显示

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示

	OLED_Clear();				//OLED清屏
}

/*中文字库：好 舒 服 呀（16x16，列行式，移植自课程例程，后续按需要替换字模）*/
const uint8_t OLED_CN_Font[][32] = {
	{0x00,0x00,0x00,0x88,0x78,0x0F,0x08,0xF8,0x00,0x42,0x42,0x42,0xF2,0x4A,0x46,0x40,0x00,0x00,0x00,0x20,0x11,0x0A,0x06,0x19,0x00,0x00,0x20,0x20,0x3F,0x00,0x00,0x00},	/*好*/
	{0x00,0x00,0x00,0x10,0x48,0x54,0xF3,0x54,0x48,0x20,0x22,0x2A,0xF2,0x2A,0xA6,0x60,0x00,0x00,0x00,0x00,0x3E,0x12,0x13,0x12,0x3E,0x00,0x20,0x20,0x3F,0x00,0x00,0x00},	/*舒*/
	{0x00,0x00,0x00,0x00,0xFE,0x22,0x22,0xFE,0x00,0xFE,0x42,0xC2,0x52,0x52,0xDE,0x00,0x00,0x00,0x00,0x20,0x1F,0x01,0x21,0x3F,0x00,0x3F,0x10,0x0B,0x04,0x0A,0x11,0x20},	/*服*/
	{0x00,0x00,0x00,0xFC,0x04,0x04,0xFC,0x00,0x62,0x5A,0x42,0xC2,0x42,0xFE,0x42,0x42,0x00,0x00,0x00,0x07,0x02,0x02,0x07,0x10,0x08,0x04,0x03,0x20,0x20,0x3F,0x00,0x00}	/*呀*/
};

/**
  * @brief  OLED显示一个16x16中文字符
  * @param  Index 汉字索引，范围：0起，顺序同字库
  */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index)
{
	uint8_t i;

	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
	for (i = 0; i < 16; i++)
	{
		OLED_WriteData(OLED_CN_Font[Index][i]);
	}

	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
	for (i = 0; i < 16; i++)
	{
		OLED_WriteData(OLED_CN_Font[Index][i + 16]);
	}
}
