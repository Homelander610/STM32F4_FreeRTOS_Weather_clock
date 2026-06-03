#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"	
#include "st7789.h"
#include "font.h"
#include "image.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

//SPI2_SCK   PB13
//SPI2_MOSI  PB15
//CS		PB12	片选，低电平有效，拉低哪个设备就是在选择与哪个设备进行传输
//RESET		PD1		注意！！！原理图中的pin5 NRST属于stm32 mcu本身的复位键，并不是单独给st7789复位使用的，需要另外接一个普通的GPIO来控制它
//DC(RS)	PD13	Data/Command 选择是传递数据还是传递命令
//BL		PD12	背光

#define CS_PORT 	GPIOB
#define CS_PIN		GPIO_Pin_12
#define DC_PORT		GPIOD
#define DC_PIN		GPIO_Pin_13
#define BL_PORT		GPIOD
#define BL_PIN		GPIO_Pin_12
#define RESET_PORT	GPIOD
#define RESET_PIN	GPIO_Pin_1

static SemaphoreHandle_t write_gram_semaphore;
static void st7789_init_display(void);

//st7789初始化
//GPIO
static void st7789_io_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_StructInit(&GPIO_InitStruct);
	
	GPIO_ResetBits(BL_PORT, BL_PIN);		//初始化时先关闭背光
	
	GPIO_InitStruct.GPIO_Mode=GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType=GPIO_OType_PP;
	GPIO_InitStruct.GPIO_Pin=GPIO_Pin_1 | GPIO_Pin_12  | GPIO_Pin_13;
	GPIO_InitStruct.GPIO_PuPd=GPIO_PuPd_DOWN;
	GPIO_InitStruct.GPIO_Speed=GPIO_High_Speed;
	GPIO_Init(GPIOD,&GPIO_InitStruct);
	
	GPIO_InitStruct.GPIO_Pin=GPIO_Pin_12;
	GPIO_Init(GPIOB,&GPIO_InitStruct);

	//GPIO复用
	GPIO_PinAFConfig(GPIOB,GPIO_PinSource13,GPIO_AF_SPI2);
	GPIO_PinAFConfig(GPIOB,GPIO_PinSource15,GPIO_AF_SPI2);
	
	GPIO_InitStruct.GPIO_Mode=GPIO_Mode_AF;
	GPIO_InitStruct.GPIO_OType=GPIO_OType_PP;
	GPIO_InitStruct.GPIO_Pin=GPIO_Pin_13  | GPIO_Pin_15;
	GPIO_InitStruct.GPIO_PuPd=GPIO_PuPd_DOWN;
	GPIO_InitStruct.GPIO_Speed=GPIO_High_Speed;
	GPIO_Init(GPIOB,&GPIO_InitStruct);
}
//spi
static void st7789_spi_init(void)
{
	SPI_InitTypeDef SPI_InitStruct;
	SPI_StructInit(&SPI_InitStruct);
	SPI_InitStruct.SPI_CPOL=SPI_CPOL_High;			//时钟极性
	SPI_InitStruct.SPI_CPHA=SPI_CPHA_2Edge;			//时钟相位
	SPI_InitStruct.SPI_FirstBit=SPI_FirstBit_MSB;
	SPI_InitStruct.SPI_BaudRatePrescaler=SPI_BaudRatePrescaler_4;
	SPI_InitStruct.SPI_Mode=SPI_Mode_Master;
	SPI_InitStruct.SPI_DataSize=SPI_DataSize_8b;
	SPI_InitStruct.SPI_CRCPolynomial=7;
	SPI_InitStruct.SPI_Direction=SPI_Direction_2Lines_FullDuplex;
	SPI_InitStruct.SPI_NSS=SPI_NSS_Soft;
	SPI_Init(SPI2,&SPI_InitStruct);
	
	//使能SPI2
	SPI_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
	SPI_Cmd(SPI2,ENABLE);
}
//DMA
static void st7789_dma_init(void)
{
	DMA_InitTypeDef DMA_Structure;
	DMA_StructInit(&DMA_Structure);
	DMA_Structure.DMA_Channel = DMA_Channel_0;								//SPI2使用通道0
	DMA_Structure.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;				//DR寄存器地址
	DMA_Structure.DMA_DIR = DMA_DIR_MemoryToPeripheral;						//传输方向 内存->外设
	DMA_Structure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;    		//外设地址是否自增	false
	DMA_Structure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; //外设数据单元大小
	DMA_Structure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;         //内存读取字节大小
	DMA_Structure.DMA_Mode = DMA_Mode_Normal;                    			//循环/单次模式		单次
	DMA_Structure.DMA_Priority = DMA_Priority_High;                		//优先级				
	DMA_Structure.DMA_FIFOMode = DMA_FIFOMode_Enable;               	 	//启用
	DMA_Structure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	DMA_Structure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
	DMA_Structure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_ITConfig(DMA1_Stream4, DMA_IT_TC, ENABLE);
	DMA_Init(DMA1_Stream4, &DMA_Structure);									//SPI2_TX->DMA1_Stream4
}
//中断
static void st7789_int_init(void)
{
	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel=DMA1_Stream4_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority=5;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority=0;
	NVIC_InitStruct.NVIC_IRQChannelCmd=ENABLE;
	NVIC_Init(&NVIC_InitStruct);
	NVIC_SetPriority(DMA1_Stream4_IRQn, 5);
}

void st7789_init(void)
{
	write_gram_semaphore = xSemaphoreCreateBinary();
	configASSERT(write_gram_semaphore);
	
	st7789_spi_init();
	st7789_dma_init();
	st7789_int_init();
	st7789_io_init();
	
	st7789_init_display();
}

//写st7789寄存器
static void st7789_write_register(uint8_t reg, uint8_t data[], uint16_t length)
{
	SPI_DataSizeConfig(SPI2, SPI_DataSize_8b);
	
	GPIO_ResetBits(CS_PORT, CS_PIN);		//先拉低片选，再拉DC
	
	GPIO_ResetBits(DC_PORT, DC_PIN);		//DC拉低表示即将发送命令
	SPI_SendData(SPI2,reg);
	while(SPI_GetFlagStatus(SPI2,SPI_FLAG_TXE) == RESET);		//等待发送完成
	while(SPI_GetFlagStatus(SPI2,SPI_FLAG_BSY) != RESET);		//等待总线空闲
			
	GPIO_SetBits(DC_PORT, DC_PIN);			//DC拉高表示即将发送数据
	for(uint16_t i=0;i<length;i++){
		SPI_SendData(SPI2,data[i]);
		while(!SPI_GetFlagStatus(SPI2,SPI_FLAG_TXE));
	}
	while(SPI_GetFlagStatus(SPI2,SPI_FLAG_BSY) != RESET);		//等待总线空闲
	
	GPIO_SetBits(CS_PORT, CS_PIN);			//发送完数据后将片选拉高，完成整个发送过程
}

//加入DMA
static void st7789_write_gram(uint8_t data[], uint32_t length, bool singlecolor)
{
	SPI_DataSizeConfig(SPI2, SPI_DataSize_16b);
	
	GPIO_ResetBits(CS_PORT, CS_PIN);		//先拉低片选，再拉DC
	GPIO_SetBits(DC_PORT, DC_PIN);			//DC拉高表示即将发送数据
	
	length /= 2;
	do
	{
		uint32_t chunk_size = length < 65535 ? length : 65535;
		
		if(singlecolor) DMA1_Stream4->CR &= ~DMA_SxCR_MINC;
		else 			DMA1_Stream4->CR |= DMA_SxCR_MINC;
		DMA1_Stream4->M0AR = (uint32_t)data;
		DMA1_Stream4->NDTR = chunk_size;
		
		DMA_Cmd(DMA1_Stream4, ENABLE);
		xSemaphoreTake(write_gram_semaphore, portMAX_DELAY);
		
		length -= chunk_size;
		if (!singlecolor) data += chunk_size * 2;
	}while(length > 0);
		
	while(SPI_GetFlagStatus(SPI2,SPI_FLAG_BSY) != RESET);
	
	GPIO_SetBits(CS_PORT, CS_PIN);			//发送完数据后将片选拉高，完成整个发送过程	
}


//显示屏复位
static void st7789_reset(void)
{
	GPIO_ResetBits(RESET_PORT,RESET_PIN);
	vTaskDelay(pdMS_TO_TICKS(2));		// 20us at least
	GPIO_SetBits(RESET_PORT,RESET_PIN);
	vTaskDelay(pdMS_TO_TICKS(120));				//延迟是根据st7789的芯片手册确定的
}

//打开背光
static void st7789_set_backlight(bool on)
{
	GPIO_WriteBit(BL_PORT, BL_PIN, on?Bit_SET:Bit_RESET);
}


//屏幕初始化
static void st7789_init_display(void)
{
	st7789_reset();
	
	
	st7789_write_register(0x11,NULL,0);					//退出睡眠模式，进入工作模式
	vTaskDelay(pdMS_TO_TICKS(5));										//等待一段时间，让电源稳定

	st7789_write_register(0x3A, (uint8_t[]){0x05}, 1);	//设置颜色模式（RGB565），每个像素数据都需要2个字节(高字节在前：R4 R3 R2 R1 R0 G5 G4 G3，低字节在前：G2 G1 G0 B4 B3 B2 B1 B0)
	st7789_write_register(0xC5, (uint8_t[]){0x1A}, 1);	//设置VCOM电压值
	st7789_write_register(0x36, (uint8_t[]){0x00}, 1);	//控制屏幕的扫描方向、颜色顺序、行列地址顺序、屏幕刷新方向等（0x00位默认设置）
	
	//-------------ST7789V Frame rate setting-----------//
	st7789_write_register(0xB2, (uint8_t[]){0x05,0x05,0x00,0x33,0x33}, 5);
	st7789_write_register(0xB7, (uint8_t[]){0x05}, 1);
	//--------------ST7789V Power setting---------------//
	st7789_write_register(0xBB, (uint8_t[]){0x3F}, 1);
	st7789_write_register(0xC0, (uint8_t[]){0x2C}, 1);
	st7789_write_register(0xC2, (uint8_t[]){0x01}, 1);
	st7789_write_register(0xC3, (uint8_t[]){0x0F}, 1);
	st7789_write_register(0xC4, (uint8_t[]){0x20}, 1);
	st7789_write_register(0xC6, (uint8_t[]){0x01}, 1);
	st7789_write_register(0xD0, (uint8_t[]){0xA4,0xA1}, 2);
	st7789_write_register(0xE8, (uint8_t[]){0x03}, 1);
	st7789_write_register(0xE9, (uint8_t[]){0x09,0x09,0x08}, 3);
	//---------------ST7789V gamma setting-------------//
	st7789_write_register(0xE0, (uint8_t[]){0xD0,0x05,0x09,0x09,0x08,0x14,0x28,0x33,0x3F,0x07,0x13,0x14,0x28,0x30}, 14);
	st7789_write_register(0xE1, (uint8_t[]){0xD0,0x05,0x09,0x09,0x08,0x03,0x24,0x32,0x32,0x3B,0x14,0x13,0x28,0x2F}, 14);
	st7789_write_register(0x21,NULL,0);
	st7789_write_register(0x29,NULL,0);					//开启屏幕显示
	
	st7789_fill_color(0, 0, ST7789_WIDTH-1, ST7789_HEIGHT-1, 0xffff);
	st7789_set_backlight(true);
}

//封装函数
static bool in_screen_range(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	if(x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT)
		return false;
	if(x2 >= ST7789_WIDTH || y2 >= ST7789_HEIGHT)
		return false;
	if(x1 > x2 || y1 > y2)
		return false;
	
	return true;
}

//选框函数。告诉 ST7789 控制器，接下来要操作（写入或读取）屏幕上的哪一个矩形区域。它通过设置屏幕的列地址和行地址范围来实现。
static void st7789_set_range_and_prepare_gram(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	/* ST7789 的列地址和行地址寄存器都是16位的（最大支持 65535 像素）。SPI 接口一次只能发送 8 位数据。
	因此，需要将一个 16 位的地址拆分成两个8位（高字节和低字节）分别发送。*/
	st7789_write_register(0x2A,(uint8_t[]){(x1 >> 8) & 0xff, x1 & 0xff, (x2 >> 8) & 0xff, x2 & 0xff}, 4);	//设置列地址范围（X坐标范围）
	st7789_write_register(0x2B,(uint8_t[]){(y1 >> 8) & 0xff, y1 & 0xff, (y2 >> 8) & 0xff, y2 & 0xff}, 4);	//设置行地址范围（Y坐标范围）
	/*设置内存写入模式：告诉st7789，接下来要发送的数据是像素颜色值，请把它们写入到你内部 GRAM 的当前位置*/
	st7789_write_register(0x2C, NULL, 0);
}


//刷屏函数:用指定的单一颜色 color填充屏幕上从 (x1, y1)到 (x2, y2)的矩形区域.
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
	//安全性检查
	if(!in_screen_range(x1, y1, x2, y2)){
		return;
	}

	//选框及设置内存写入模式
	st7789_set_range_and_prepare_gram(x1, y1, x2, y2);
	
	uint32_t pixels = (x2-x1+1) * (y2-y1+1);			//计算像素点个数
	st7789_write_gram((uint8_t *)&color, pixels * 2, true);
}

static void st7789_draw_font(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *model, uint16_t color, uint16_t bg_color)
{
	uint16_t bytes_per_row = (width + 7) / 8;
	
	static uint8_t buff[76 * 76 * 2];
	uint8_t *pbuff = buff;
	for(uint16_t row = 0; row < height; row ++){
		const uint8_t *row_data = model + row * bytes_per_row;
		for(uint16_t col = 0; col < width; col++){
			uint8_t pixel = row_data[col / 8] & (1 << (7 - col %8));
			uint16_t pixel_color = pixel ? color : bg_color;
			*pbuff++ = pixel_color & 0xFF;        // 低字节
			*pbuff++ = (pixel_color >> 8) & 0xFF; // 高字节
		}
	}
	
	st7789_set_range_and_prepare_gram(x, y, x + width - 1, y + height - 1);
	st7789_write_gram(buff, pbuff - buff, false);
}

//新增函数：model的取模
static const uint8_t *ascii_get_model(const char ch, const font_t *font)
{
	uint16_t bytes_per_row = (font->size / 2 + 7) / 8;
	uint16_t bytes_per_char = font->size * bytes_per_row;
	if(font->ascii_map)
	{
		const char *map = font->ascii_map;
		do
		{
			if(*map == ch)
			{
				return font->ascii_model + (map - font->ascii_map) * bytes_per_char;
			}
		}while(*(++map) != '\0');
	}
	else
	{
		return font->ascii_model + (ch - ' ') * bytes_per_char;
	}
	
	return NULL;
}

//写单一ascii码字符
static void st7789_write_ascii(uint16_t x, uint16_t y, const char ch, uint16_t color, uint16_t bg_color ,const font_t *font)
{
	uint16_t fheight = font->size, fwidth = font->size/2;
	
	
	if(font == NULL)
		return;
	
	if(ch < 0x20 || ch > 0x7E)
		return;
	
	if(!in_screen_range(x, y, x + fwidth - 1, y + fheight - 1)){
		return;
	}
	
	const uint8_t *model = ascii_get_model(ch, font);
	if(model)
		st7789_draw_font(x, y, fwidth, fheight, model, color, bg_color);
	
}

//写中文
static void st7789_write_chinese(uint16_t x, uint16_t y, const char *ch, uint16_t color, uint16_t bg_color ,const font_t *font)
{
	if(font == NULL || ch == NULL)
		return;
	
	uint16_t fheight = font->size, fwidth = font->size;
	if(!in_screen_range(x, y, x + fwidth - 1, y + fheight - 1)){
		return;
	}
	
	const font_chinese_t *c = font->chinese;
	for(; c->name != NULL;c++){
		if(strcmp(c->name, ch) == 0)	//一致则返回0
			break;
	}
	if(c->name == NULL)
			return;
	
	st7789_draw_font(x, y, fwidth, fheight, c->model, color, bg_color);
}
 
//识别是否是GB2312
static bool is_gb2312(char ch)
{
	return ((unsigned char)ch >= 0xA1 && (unsigned char)ch <= 0xF7);
}

//识别是否是UTF-8
//static int utf8_char_length(const char *str)
//{
//	if((*str & 0x80) == 0)		return 1;	 //1 byte
//	if((*str & 0xE0) == 0xC0)	return 2;    //2 byte
//	if((*str & 0xF0) == 0xE0)	return 3;    //3 byte
//	if((*str & 0xF8) == 0xF0)	return 4;    //4 byte
//	return -1; 	//	Invalid UTF-8
//	
//}

void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color ,const font_t *font)
{

	while(*str){
		//int len = utf8_char_length(*str);
		int len = is_gb2312(*str) ? 2 : 1;
		if(len <= 0){
			str++;
			continue;
		}
		else if(len == 1){
			st7789_write_ascii(x, y, *str, color, bg_color, font);
			str ++;
			x += font->size / 2;
		}
		else{
			char ch[5];
			strncpy(ch, str, len);
			st7789_write_chinese(x, y, ch, color, bg_color, font);
			str += len;
			x += font->size;
		}
	}
}
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
	if(x >= ST7789_WIDTH || y >= ST7789_HEIGHT || x + image->width-1 >= ST7789_WIDTH || y + image->height-1 >= ST7789_HEIGHT){
		return;
	}
	
	st7789_set_range_and_prepare_gram(x, y, x + image->width - 1, y + image->height - 1);
	
	st7789_write_gram((uint8_t *)image->data, image->height * image->width * 2, false);
}

void DMA1_Stream4_IRQHandler(void)
{
	if(DMA_GetITStatus(DMA1_Stream4, DMA_IT_TCIF4) == SET)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		xSemaphoreGiveFromISR(write_gram_semaphore, &pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
		
		DMA_ClearITPendingBit(DMA1_Stream4, DMA_IT_TCIF4);
	}
}
