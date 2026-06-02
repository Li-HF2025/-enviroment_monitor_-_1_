#ifndef __MY_OTA_H
#define __MY_OTA_H
#include "esp_err.h"
#include <stdbool.h>

void ato_init(void);
void ato_start(void);
void ato_validate_app(void);  // 固件启动后验证存活，取消自动回滚

esp_err_t onenet_ota_upload_version(void);
esp_err_t onenet_ota_upload_version_separate(const char *fw_ver, const char *sw_ver);
esp_err_t onenet_ota_check_task(const char *type, const char *version);
esp_err_t onenet_ota_upload_status(int tid, int step);
esp_err_t onenet_ota_fetch_firmware(const char *url, uint8_t *buf, size_t buf_size, size_t *out_len);

const char *ota_get_current_version(void);
const char *ota_get_target_version(void);
const char *ota_get_download_url(void);
const char *ota_get_firmware_md5(void);
int  ota_get_task_id(void);
int  ota_get_firmware_size(void);
int  ota_get_progress(void);
bool ota_is_running(void);

// 手动回滚（Priority 2B）
const char *ota_get_previous_version(void);
esp_err_t ota_rollback_to_previous(void);

#endif
