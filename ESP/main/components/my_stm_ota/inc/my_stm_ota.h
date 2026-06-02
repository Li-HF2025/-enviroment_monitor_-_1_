#ifndef __MY_STM_OTA_H__
#define __MY_STM_OTA_H__

#include "esp_err.h"
#include <stdbool.h>

// 生命周期
void stm_ota_init(void);
void stm_ota_deinit(void);

// SOTA 云端交互 (type=2, MCU软件)
esp_err_t stm_ota_upload_version(void);
void stm_ota_start(void);  // 启动 STM32 OTA 任务（下载+烧写，不阻塞 UI）
esp_err_t stm_ota_bootloader_get_version(uint8_t *ver);
esp_err_t stm_ota_bootloader_get_id(uint16_t *pid);
esp_err_t stm_ota_check_task(const char *version);
esp_err_t stm_ota_upload_status(int step);

// 固件下载
esp_err_t stm_ota_download_firmware(const char *url, const char *expected_md5);

// AN3155 烧写 STM32
esp_err_t stm_ota_flash(void);
esp_err_t stm_ota_self_test(void);   // AN3155 全链路自检

// 状态查询
int  stm_ota_get_progress(void);
const char *stm_ota_get_target_version(void);
const char *stm_ota_get_stm_version(void);
int  stm_ota_get_firmware_size(void);
bool stm_ota_is_running(void);

#endif
