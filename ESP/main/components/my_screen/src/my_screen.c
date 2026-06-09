#include "my_screen.h" 
#include "esp_heap_caps.h"

static const char *TAG = "MY_SCREEN";

#define LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000) // 40MHz像素时钟
#define TOUCH_SPI_CLOCK_HZ      (200 * 1000)       // 触摸SPI时钟
#define LCD_BK_LIGHT_ON_LEVEL   1                   // 背光开启电平
#define LCD_BK_LIGHT_OFF_LEVEL  0                   // 背光关闭电平
//SPI引脚定义
#define PIN_NUM_SCLK            6                   // SPI时钟引脚
#define PIN_NUM_MOSI            7                   // SPI主输出引脚
#define PIN_NUM_MISO            8                   // SPI主输入引脚
//LCD控制引脚定义
#define PIN_NUM_LCD_DC          5                   // LCD数据/命令选择引脚
#define PIN_NUM_LCD_RST         9                   // LCD复位引脚
#define PIN_NUM_LCD_CS          4                   // LCD片选引脚
#define PIN_NUM_LCD_BK_LIGHT    10                   // LCD背光控制引脚
//触摸控制引脚定义
#define PIN_NUM_TOUCH_CS        11                  // 触摸片选引脚

#define LCD_H_RES               240                 // LCD水平分辨率
#define LCD_V_RES               320                 // LCD垂直分辨率
#define LCD_DRAW_BUF_LINES      40                  // LVGL局部刷新缓冲行数

#define HSPI_HOST                SPI2_HOST           // 使用SPI2_HOST（HSPI）作为LCD的SPI总线

#ifndef CONFIG_LCD_MIRROR_Y
#define CONFIG_LCD_MIRROR_Y      0
#endif

#define LVGL_TASK_MAX_DELAY 500 // LVGL任务最大延迟时间，单位为毫秒
#define LVGL_TASK_MIN_DELAY 1000 / CONFIG_FREERTOS_HZ  // LVGL任务最小延迟时间，单位为毫秒


static _lock_t lvgl_api_lock; // LVGL API调用锁，确保线程安全

/**
 * @brief 为了适应LVGL的界面设置，有些场景会有横屏和竖屏的切换
 * @note LVGL 修改屏幕旋转角度后，只会更新自己的内部坐标，不会自动修改 LCD 硬件的 mirror/swap_xy等参数，所以需要在这个回调函数中根据 LVGL 的旋转设置来更新 LCD 硬件的显示参数，以确保显示内容正确显示
 */
static void lcd_port_update_callback(lv_display_t *dis){
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(dis); // 从LVGL显示对象中获取之前关联的LCD面板句柄
    lv_display_rotation_t rotation = lv_display_get_rotation(dis); // 获取LVGL当前的旋转设置
    switch(rotation){
        case LV_DISPLAY_ROTATION_0:
            esp_lcd_panel_mirror(panel_handle, true, false); // 水平翻转
            esp_lcd_panel_swap_xy(panel_handle, false); // 不交换X/Y轴
            break;
        case LV_DISPLAY_ROTATION_90:
            esp_lcd_panel_mirror(panel_handle, false, false); // 不翻转
            esp_lcd_panel_swap_xy(panel_handle, true); // 交换X/Y轴
            break;
        case LV_DISPLAY_ROTATION_180:
            esp_lcd_panel_mirror(panel_handle, false, true); // 垂直翻转
            esp_lcd_panel_swap_xy(panel_handle, false); // 不交换X/Y轴
            break;
        case LV_DISPLAY_ROTATION_270:
            esp_lcd_panel_mirror(panel_handle, false, false); // 不翻转
            esp_lcd_panel_swap_xy(panel_handle, true); // 交换X/Y轴
            break;
    }
}

/**
 * @brief LVGL显示刷新回调函数,将LVGL渲染的图像数据传递给LCD面板进行显示
 * @note 这个函数会被LVGL在需要刷新显示时调用,通过lv_display_set_flush_cb()设置,在这个函数中可以通过lv_display_get_user_data()获取之前关联的LCD面板句柄,然后使用esp_lcd_panel_draw_bitmap()等函数将渲染结果传递给LCD面板显示
 */
static void lcd_flush_cb(lv_display_t *dis, const lv_area_t *area, uint8_t *px_map){
    //先跟新显示参数(处理旋转)
    lcd_port_update_callback(dis);
    
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(dis); // 从LVGL显示对象中获取之前关联的LCD面板句柄

    int offset_x1 = area->x1; // 刷新区域的起始X坐标
    int offset_x2 = area->x2; // 刷新区域的结束X坐标
    int offset_y1 = area->y1; // 刷新区域的起始Y坐标
    int offset_y2 = area->y2; // 刷新区域的结束Y坐标
    int width = offset_x2 - offset_x1 + 1; // 刷新区域的宽度
    int height = offset_y2 - offset_y1 + 1; // 刷新区域的高度

    lv_draw_sw_rgb565_swap(px_map, width * height); // 将LVGL的RGB565颜色数据转换为LCD面板需要的格式(如果需要)

    // esp_lcd_panel_draw_bitmap 的结束坐标是“开区间”，因此这里需要 +1
    esp_lcd_panel_draw_bitmap(panel_handle, offset_x1, offset_y1, offset_x2 + 1, offset_y2 + 1, px_map); // 将渲染结果传递给LCD面板显示
}
/**
 * @brief LVGL tick定时器回调函数,用于定时调用LVGL的tick处理函数
 * @note LVGL内部没有任何定时相关功能，需要外部绑定一个计时器调控LVGL的tick,
 * tick管理基础的时间信息,比如动画、输入设备的去抖动、定时器等都依赖于tick的定时调用,所以需要确保这个函数被定期调用,通常是每隔1-10毫秒调用一次,以保持LVGL内部时间的正确流逝
 */
static void lvgl_tick_task(void *arg){
    lv_tick_inc(2);
}

/**
 * @brief LCD面板完成数据传输后的回调函数,用于通知LVGL继续渲染下一帧
 * @note 当LCD面板完成某一帧数据的传输后,这个回调函数会被调用,可以在这个回调中执行一些操作,比如释放相关资源、通知LVGL继续渲染下一帧等,具体实现可以根据实际需求来定制,比如可以使用一个信号量或者事件组来通知LVGL任务继续执行
 */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx){
    lv_display_t *disp = (lv_display_t *)user_ctx; // 从用户数据中获取LVGL显示对象
    lv_display_flush_ready(disp); // 通知LVGL当前帧的刷新已经完成
    return false;
}

#if CONFIG_LCD_TOUCH_ENABLED
// 触摸原始范围（根据实际测试结果填写）
#define TOUCH_X_MIN 10
#define TOUCH_X_MAX 225
#define TOUCH_Y_MIN 10
#define TOUCH_Y_MAX 310

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data){
    esp_lcd_touch_point_data_t points[1] = {0};
    uint8_t point_cnt = 0;

    esp_lcd_touch_handle_t touch_handle = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev); // 从LVGL输入设备对象中获取之前关联的触摸屏句柄

    esp_lcd_touch_read_data(touch_handle);

    esp_err_t err = esp_lcd_touch_get_data(touch_handle, points, &point_cnt, 1);
    bool touched = (err == ESP_OK) && (point_cnt > 0);

    if(touched){
        int raw_x = points[0].x;
        int raw_y = points[0].y;
        // 线性映射到屏幕坐标
        int mapped_x = (raw_x - TOUCH_X_MIN) * (LCD_H_RES - 1) / (TOUCH_X_MAX - TOUCH_X_MIN);
        int mapped_y = (raw_y - TOUCH_Y_MIN) * (LCD_V_RES - 1) / (TOUCH_Y_MAX - TOUCH_Y_MIN);
        // 边界保护
        if (mapped_x < 0) mapped_x = 0;
        if (mapped_x >= LCD_H_RES) mapped_x = LCD_H_RES - 1;
        if (mapped_y < 0) mapped_y = 0;
        if (mapped_y >= LCD_V_RES) mapped_y = LCD_V_RES - 1;

        data->point.x = mapped_x;
        data->point.y = mapped_y;
        data->state = LV_INDEV_STATE_PRESSED;
        screen_idle_lock_mark_activity(); // 标记有活动,重置屏幕空闲锁的计时
        ESP_LOGD(TAG, "Touch at mapped(%d, %d) raw(%d, %d), strength: %d", mapped_x, mapped_y, raw_x, raw_y, points[0].strength);
    }else{
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void lvgl_port_task(void *arg){
    ESP_LOGI(TAG,"创建LVGL示例界面");
    uint32_t count = 0;
    while(1){
        _lock_acquire(&lvgl_api_lock);
        count = lv_timer_handler(); // 调用LVGL的定时器处理函数,执行LVGL的定时任务,比如动画、输入设备的去抖动、定时器等
        
        screen_idle_lock_poll(); // 调用屏幕空闲锁的轮询函数

        _lock_release(&lvgl_api_lock);

        count = MAX(count, LVGL_TASK_MIN_DELAY); // 确保最小延迟时间
        count = MIN(count, LVGL_TASK_MAX_DELAY); // 确保最大延迟时间

        usleep(count * 1000); // 延迟一段时间,以避免LVGL任务占用过多CPU资源,同时保持LVGL的流畅运行
    }
}

void screen_init(){
    ESP_LOGI(TAG, "屏幕初始化开始");
    gpio_config_t bk_light_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_LCD_BK_LIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };// 配置背光控制引脚为输出
    ESP_ERROR_CHECK(gpio_config(&bk_light_conf));
    ESP_LOGI(TAG, "背光控制引脚配置完成");

    spi_bus_config_t bus_cfg={
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 160 * sizeof(uint16_t),
    };// 配置SPI总线
    ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI总线初始化完成");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };// 配置LCD面板IO
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(HSPI_HOST, &io_cfg, &io_handle));// 创建LCD面板IO句柄(通讯接口,绑定到SPI总线)
    ESP_LOGI(TAG, "LCD面板IO配置完成");

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };// 配置LCD面板参数
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &panel_handle));// 创建LCD面板句柄(控制接口,绑定到面板IO)
    ESP_LOGI(TAG, "LCD面板配置完成");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle)); // 复位LCD面板
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));  // 初始化LCD面板

    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false)); // 镜像显示(水平翻转)

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // 打开lcd显示(有些面板初始化后会默认关闭显示)

    gpio_set_level(PIN_NUM_LCD_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL); // 打开背光
    ESP_LOGI(TAG, "屏幕初始化完成");

    ESP_LOGI(TAG,"LVGL初始化开始");
    lv_init(); // 初始化LVGL库

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES); // 创建LVGL显示对象

    size_t fb_size = LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(lv_color16_t); // 使用局部刷新缓冲,避免整屏DMA内存不足

    void *buf1 = spi_bus_dma_memory_alloc(HSPI_HOST,fb_size,0);
    void *buf2 = spi_bus_dma_memory_alloc(HSPI_HOST,fb_size,0); // 分配双缓冲区内存,确保内存可被DMA访问,提高性能
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG,
                 "LVGL DMA buffer alloc failed (size=%u, free_dma=%u, free_internal=%u)",
                 (unsigned)fb_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        if (buf1) {
            free(buf1);
        }
        if (buf2) {
            free(buf2);
        }
        return;
    }

    lv_display_set_buffers(disp, buf1, buf2, fb_size,LV_DISPLAY_RENDER_MODE_PARTIAL); // 设置LVGL显示缓冲区(disp句柄)

    lv_display_set_user_data(disp, panel_handle); // 将LCD面板句柄作为用户数据关联到LVGL显示对象,以便在刷新回调中使用(可以通过lv_display_get_user_data(disp)拿到panel_handle)

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565); // 设置LVGL显示颜色格式为RGB565(与LCD面板匹配)

    lv_display_set_flush_cb(disp, lcd_flush_cb); // 设置LVGL显示刷新回调函数,之前的LVGL是在缓冲区中渲染,渲染结果通过刷新回调函数传递给LCD面板进行显示

    ESP_LOGI(TAG,"LVGL初始化完成");

    ESP_LOGI(TAG,"安装LVGL tick定时器");
    const esp_timer_create_args_t lv_tick_timer_args = {
        .callback = &lvgl_tick_task, // 定时器回调函数,调用LVGL的tick处理函数
        .name = "lv_tick_timer"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lv_tick_timer_args, &lvgl_tick_timer)); // 创建定时器
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2 * 1000)); // 启动定时器,每2毫秒调用一次回调函数,以保持LVGL内部时间的正确流逝

    ESP_LOGI(TAG,"注册IO事件回调");
    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };//当LCD面板完成某一帧数据的传输后,这个回调函数会被调用,可以在这个回调中执行一些操作,比如释放相关资源、通知LVGL继续渲染下一帧等

    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, disp)); // 注册IO事件回调,将之前创建的LVGL显示对象作为用户数据传递给回调函数,以便在回调中通知LVGL当前帧的刷新已经完成

#if CONFIG_LCD_TOUCH_ENABLED
    ESP_LOGI(TAG,"触摸屏初始化开始");
    // 触摸屏初始化代码(如果启用了触摸功能)
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t touch_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    touch_io_config.pclk_hz = TOUCH_SPI_CLOCK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HSPI_HOST, &touch_io_config, &touch_io_handle));

    esp_lcd_touch_config_t touch_cfg={
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1, // 如果触摸控制器没有复位引脚，可以设置为-1
        .int_gpio_num = -1, // 如果触摸控制器没有中断引脚，可以设置为-1
        .flags = {
            .swap_xy = 0, // 根据触摸屏的实际连接情况设置
            .mirror_x = 0,
            .mirror_y = CONFIG_LCD_MIRROR_Y,// 根据配置设置Y轴镜像翻转
        },
    };

    esp_lcd_touch_handle_t touch_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(touch_io_handle, &touch_cfg, &touch_handle));// 创建触摸屏句柄

    static lv_indev_t* indev_drv;
    indev_drv = lv_indev_create(); // 创建LVGL输入设备对象
    lv_indev_set_type(indev_drv, LV_INDEV_TYPE_POINTER); // 设置输入设备类型为指针设备(适用于触摸屏)
    lv_indev_set_display(indev_drv, disp); // 将输入设备关联到之前创建的LVGL显示对象
    lv_indev_set_user_data(indev_drv, touch_handle); // 将触摸屏句柄作为用户数据关联到LVGL输入设备对象,以便在输入处理回调中使用(可以通过lv_indev_get_user_data(indev_drv)拿到touch_handle)
    lv_indev_set_read_cb(indev_drv, touch_read_cb); // 设置LVGL输入设备读取回调函数,在这个回调函数中可以调用esp_lcd_touch_read()等函数获取触摸屏的坐标和状态,并将其传递给LVGL进行处理

    ESP_LOGI(TAG,"触摸屏初始化完成");
#endif

    ESP_LOGI(TAG,"创建LVGL示例界面");
    xTaskCreate(lvgl_port_task,"lvgl_port_task",8192,NULL,5,NULL); // 提高栈空间，避免复杂 LVGL 页面（如下拉框）触发栈溢出

    _lock_acquire(&lvgl_api_lock); // 在LVGL任务中使用锁来保护LVGL API的调用,确保线程安全
    //展示初始界面,创建一个标签显示"Hello LVGL!"
    ui_init(); 
    screen_idle_lock_init(5 * 60 * 1000); // 初始化屏幕空闲锁,设置超时时间为5分钟
    _lock_release(&lvgl_api_lock);
}