#include "OLED.h"
#include "OLED_Font.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#define OLED_ADDRESS 0x78
#define OLED_WIDTH   128
#define OLED_PAGES   8

extern I2C_HandleTypeDef hi2c1;

// ===================== 【关键】已删除：uint8_t OLED_Page[OLED_PAGES][OLED_WIDTH]; =====================

static void OLED_WriteCommand(uint8_t command)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDRESS, 0x00, I2C_MEMADD_SIZE_8BIT, &command, 1, HAL_MAX_DELAY);
}

static void OLED_SetCursor(uint8_t page, uint8_t column)
{
    OLED_WriteCommand((uint8_t)(0xB0 + page));
    OLED_WriteCommand((uint8_t)(0x00 + (column & 0x0F)));
    OLED_WriteCommand((uint8_t)(0x10 + ((column >> 4) & 0x0F)));
}

static void OLED_WriteData(uint8_t data)
{
    // 无缓存：直接单字节写屏
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDRESS, 0x40, I2C_MEMADD_SIZE_8BIT, &data, 1, HAL_MAX_DELAY);
}

// 整段数据写屏（内部用）
static void OLED_WriteDataBlock(const uint8_t *data, uint16_t size)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDRESS, 0x40, I2C_MEMADD_SIZE_8BIT, data, size, HAL_MAX_DELAY);
}

// ===================== 全屏刷新（无缓存，直接写屏） =====================
void OLED_Update(void)
{
    // 无缓存版本：无需操作
}

// ===================== OLED 初始化 =====================
void OLED_Init(void)
{
    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0x20);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x2E);
    OLED_WriteCommand(0xAF);
    OLED_Clear();
}

// ===================== 清屏（直接写屏，无缓存） =====================
void OLED_Clear(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        OLED_SetCursor(page, 0);
        for (uint8_t i = 0; i < OLED_WIDTH; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

// ===================== 写字符（直接写屏，无缓存） =====================
void OLED_Write_Char(uint8_t Line, uint8_t Column, char Char)
{
    if (Line >= OLED_PAGES || Column > (OLED_WIDTH - 6))
        return;

    if (Char < 32 || Char > 126)
        Char = '?';

    uint8_t index = Char - 32;
    OLED_SetCursor(Line, Column);

    // 直接写 6 个像素字节到屏
    for (uint8_t i = 0; i < 6; i++)
    {
        OLED_WriteData(OLED_Font6x8[index][i]);
    }
}

// ===================== 写字符串（直接写屏） =====================
void OLED_WriteString(uint8_t Line, uint8_t Column, const char *String)
{
    uint8_t currentColumn = Column * 6;

    while (*String != '\0' && Line < OLED_PAGES)
    {
        if (currentColumn >= OLED_WIDTH)
        {
            currentColumn = 0;
            Line++;
            if (Line >= OLED_PAGES) break;
        }

        OLED_Write_Char(Line, currentColumn, *String);
        currentColumn += 6;
        String++;
    }
}

// ===================== 写十六进制数 =====================
void OLED_WriteHex(uint8_t Line, uint8_t Column, uint32_t Value)
{
    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)Value);
    OLED_WriteString(Line, Column, buffer);
}

// ===================== 写十进制数 =====================
void OLED_WriteNum(uint8_t Line, uint8_t Column, int32_t Value)
{
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%ld", (long)Value);
    OLED_WriteString(Line, Column, buffer);
}

// ===================== 显示图片（直接写屏） =====================
void OLED_DisplayImage(uint8_t BitLine, uint8_t BitColumn, const uint8_t *Image, uint8_t ImageWidth, uint8_t ImageHeight)
{
    uint8_t rowBytes = (ImageWidth + 7) / 8;

    for (uint8_t y = 0; y < ImageHeight; y++)
    {
        for (uint8_t x = 0; x < ImageWidth; x++)
        {
            uint16_t byteIdx = y * rowBytes + (x / 8);
            uint8_t bitIdx = x % 8;
            uint8_t page = (BitLine + y) / 8;
            uint8_t col = BitColumn + x;
            uint8_t bit = (BitLine + y) % 8;

            if (col >= OLED_WIDTH || page >= OLED_PAGES) continue;

            // 直接单点写屏
            OLED_SetCursor(page, col);
            uint8_t data = 0;
            HAL_I2C_Mem_Read(&hi2c1, OLED_ADDRESS, 0x40, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);

            if (Image[byteIdx] & (1 << (7 - bitIdx)))
                data |= (1 << bit);
            else
                data &= ~(1 << bit);

            OLED_WriteData(data);
        }
    }
}

// ===================== 局部刷新（无缓存，直接写屏） =====================
void OLED_UpdateRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    // 无缓存版本：无需操作
}

// ===================== 清除图片区域 =====================
void OLED_ClearImage(uint8_t BitLine, uint8_t BitColumn, uint8_t ImageWidth, uint8_t ImageHeight)
{
    for (uint8_t y = 0; y < ImageHeight; y++)
    {
        for (uint8_t x = 0; x < ImageWidth; x++)
        {
            uint8_t page = (BitLine + y) / 8;
            uint8_t col = BitColumn + x;
            uint8_t bit = (BitLine + y) % 8;

            if (col >= OLED_WIDTH || page >= OLED_PAGES) continue;

            OLED_SetCursor(page, col);
            uint8_t data = 0;
            HAL_I2C_Mem_Read(&hi2c1, OLED_ADDRESS, 0x40, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
            data &= ~(1 << bit);
            OLED_WriteData(data);
        }
    }
}
