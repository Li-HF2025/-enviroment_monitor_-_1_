#include "db.h"
#include "OLED.h"
#include "mySerial.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#define DB_Port GPIOB
#define DB_Pin GPIO_PIN_1

#define ADC_BUF_SIZE 50
uint16_t adc_dma_buf[ADC_BUF_SIZE];

#define FILTER_SIZE 5
uint16_t filter_buf[FILTER_SIZE];
uint8_t filter_idx = 0;

#define ADC_REF 3.3f
#define ADC_RESOLUTION 4096.0f

#define ADC_QUIET  200
#define ADC_MAX    3000
#define DB_MIN     0
#define DB_MAX     180

#define DB_REPORT_INTERVAL_MS 150U
#define DB_SMOOTH_ALPHA 0.2f

uint16_t DB_raw_value = 0;

extern ADC_HandleTypeDef hadc1;

osThreadId_t decibelTaskHandle;

static volatile bool db_running = false; //运行标志位

#define DB_FLAG_HALF  (1U << 0)
#define DB_FLAG_FULL  (1U << 1)

static const osThreadAttr_t db_task_attr = {
    .name = "Decibel",
    .stack_size = 128U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal,
};

static float db_smooth = 0.0f;
static uint32_t last_report_tick = 0U;

static uint16_t DB_Filter(uint16_t *data) { 
    filter_buf[filter_idx] = *data;
    filter_idx = (filter_idx + 1) % FILTER_SIZE;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < FILTER_SIZE; i++) {
        sum += filter_buf[i];
    }
    *data = sum / FILTER_SIZE;
    return *data;
}

static float calculate_db(uint16_t adc_value) { 
    if(adc_value < ADC_QUIET) adc_value = ADC_QUIET;
    if(adc_value > ADC_MAX) adc_value = ADC_MAX;

    float ratio = (float)(adc_value - ADC_QUIET) / (float)(ADC_MAX - ADC_QUIET);
    if(ratio < 0.0f) ratio = 0.0f;
    if(ratio > 1.0f) ratio = 1.0f;

    float db = DB_MIN + (DB_MAX - DB_MIN) * ratio;
    return db;
}

static void StartDecibelTask(void *argument)
{
    // OLED_WriteString(0, 0, "DB :");
    while(1){
        uint32_t flags = osThreadFlagsWait(DB_FLAG_HALF | DB_FLAG_FULL, osFlagsWaitAny, 1000U);
        if (flags == osFlagsErrorTimeout) {
            continue;
        }
        if (!db_running) {
            continue;
        }

        OLED_WriteString(2, 0, "            ");
        uint16_t *target_buf;

        if(flags & DB_FLAG_HALF)
            target_buf = &adc_dma_buf[0];
        else if(flags & DB_FLAG_FULL)
            target_buf = &adc_dma_buf[ADC_BUF_SIZE/2];
        else
            continue;

        uint32_t sum = 0;
        for(int i=0; i<ADC_BUF_SIZE/2; i++)
            sum += target_buf[i];
        uint16_t avg_adc = sum / (ADC_BUF_SIZE/2);

        uint16_t filtered = DB_Filter(&avg_adc);

        float current_decibel = calculate_db(filtered);
        db_smooth = db_smooth * (1.0f - DB_SMOOTH_ALPHA) + current_decibel * DB_SMOOTH_ALPHA;
        uint32_t now = osKernelGetTickCount();
        if ((now - last_report_tick) >= DB_REPORT_INTERVAL_MS) {
            uint16_t db_x10 = (uint16_t)(db_smooth * 10.0f + 0.5f);
            sensor_report(CMD_DB, 0x02, (int16_t)db_x10, 0x00);
            last_report_tick = now;
        }
        osDelay(100); // 避免任务过于频繁地运行
    }
}

void DB_RTOS_Init(void)
{
    if (decibelTaskHandle == NULL) {
        decibelTaskHandle = osThreadNew(StartDecibelTask, NULL, &db_task_attr);
    }
    // 默认自启：上电或重启后自动开始采集
    DB_Init();
}

void DB_Init(void) { 
    if (db_running) {
        return;
    }

    last_report_tick = 0U;
    db_smooth = 0.0f;
    db_running = true;
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_BUF_SIZE);
}

void DB_DeInit(void){
    if (!db_running) {
        return;
    }

    db_running = false;
    HAL_ADC_Stop_DMA(&hadc1);
    last_report_tick = 0U;
    db_smooth = 0.0f;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc == &hadc1)
    {
        if (decibelTaskHandle != NULL && db_running) {
            osThreadFlagsSet(decibelTaskHandle, DB_FLAG_HALF);
        }
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc == &hadc1)
    {
        if (decibelTaskHandle != NULL && db_running) {
            osThreadFlagsSet(decibelTaskHandle, DB_FLAG_FULL);
        }
    }
}