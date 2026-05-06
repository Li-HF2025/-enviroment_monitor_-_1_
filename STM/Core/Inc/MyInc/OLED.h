#ifndef __OLED_H
#define __OLED_H
#include "main.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Write_Char(uint8_t Line, uint8_t Column, char Char);
void OLED_WriteString(uint8_t Line, uint8_t Column, const char *String);
void OLED_Update(void);
void OLED_WriteHex(uint8_t Line, uint8_t Column, uint32_t Value);
void OLED_WriteNum(uint8_t Line, uint8_t Column, int32_t Value);
void OLED_UpdateRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void OLED_DisplayImage(uint8_t BitLine, uint8_t BitColumn,const uint8_t *Image, uint8_t ImageWidth, uint8_t ImageHeight);
void OLED_ClearImage(uint8_t BitLine, uint8_t BitColumn, uint8_t ImageWidth, uint8_t ImageHeight);
#endif /* __OLED_H */
