#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "esp_adc/adc_oneshot.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "../../user_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "src/axs15231b/esp_lcd_axs15231b.h"
#include "src/tca9554/esp_io_expander_tca9554.h"
#include "../i2c_bsp/i2c_bsp.h"

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t lvgl_mux = NULL;

// UI Elements for Detector
static lv_obj_t * mag_arc;
static lv_obj_t * compass_label_n;
static lv_obj_t * compass_label_e;
static lv_obj_t * compass_label_s;
static lv_obj_t * compass_label_w;
static lv_obj_t * compass_label_ne;
static lv_obj_t * compass_label_se;
static lv_obj_t * compass_label_sw;
static lv_obj_t * compass_label_nw;
static lv_obj_t * crosshair_dot;
static lv_obj_t * mag_label;
static lv_obj_t * title_label;
static lv_obj_t * declination_label;
static lv_obj_t * elevation_label;
static lv_obj_t * sens_label;
static lv_obj_t * polarity_label;
static lv_obj_t * nt_label;
static lv_obj_t * scan_btn;
static lv_obj_t * ip_label;
static lv_obj_t * scan_label;
static lv_obj_t * mute_btn;
static lv_obj_t * mute_label;
static lv_obj_t * wave_btn;
static lv_obj_t * wave_label;
static lv_obj_t * tare_btn;
static lv_obj_t * tare_label;
static int main_tare_state = 0; // 0=RAW, 1=TARE, 2=AUTO
lv_obj_t * cal_progress_bar;
lv_obj_t * cal_status_label;
lv_obj_t * full_cal_btn;
lv_obj_t * full_cal_label;
static bool is_calibrating = false;
static lv_obj_t * wifi_btn;
static lv_obj_t * wifi_label;
lv_obj_t * ui_date_label;
lv_obj_t * ui_time_label;
#include "../settings_manager/settings_manager.h"
#include "../web_server/web_server.h"
#include "../data_logger/data_logger.h"
#include "../screenshot/screenshot.h"

static void mark_settings_dirty() {
    settings_dirty = true;
    last_settings_change_ms = millis();
}

static uint16_t *trans_buf_1 = NULL;
uint8_t *lvgl_dest = NULL;
static SemaphoreHandle_t flush_done_semaphore;
esp_io_expander_handle_t io_expander = NULL;
#define LCD_BIT_PER_PIXEL 16
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (MFS_LCD_H_RES * MFS_LCD_V_RES * BYTES_PER_PIXEL)

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = 
{
  {0x11, (uint8_t []){0x00}, 0, 100},
  {0x29, (uint8_t []){0x00}, 0, 100},
};

static bool mfs_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  BaseType_t TaskWoken;
  xSemaphoreGiveFromISR(flush_done_semaphore,&TaskWoken);
  return false;
}

static void mfs_increase_lvgl_tick(void *arg)
{
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void mfs_lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
  esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  lv_draw_sw_rgb565_swap(color_p, lv_area_get_width(area) * lv_area_get_height(area));
#if (Rotated == USER_DISP_ROT_90)
  lv_display_rotation_t rotation = lv_display_get_rotation(disp);
  lv_area_t rotated_area;
  if(rotation != LV_DISPLAY_ROTATION_0)
  {
    lv_color_format_t cf = lv_display_get_color_format(disp);
    /*Calculate the position of the rotated area*/
    rotated_area = *area;
    lv_display_rotate_area(disp, &rotated_area);
    /*Calculate the source stride (bytes in a line) from the width of the area*/
    uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
    /*Calculate the stride of the destination (rotated) area too*/
    uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);
    /*Have a buffer to store the rotated area and perform the rotation*/
    
    int32_t src_w = lv_area_get_width(area);
    int32_t src_h = lv_area_get_height(area);
    lv_draw_sw_rotate(color_p, lvgl_dest, src_w, src_h, src_stride, dest_stride, rotation, cf);
    /*Use the rotated area and rotated buffer from now on*/
    area = &rotated_area;
  }

  const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
  const int offgap = (MFS_LCD_V_RES / flush_coun);
  const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
  int offsetx1 = 0;
  int offsety1 = 0;
  int offsetx2 = MFS_LCD_H_RES;
  int offsety2 = offgap;

  uint16_t *map = (uint16_t *)lvgl_dest;
  xSemaphoreGive(flush_done_semaphore);
  for(int i = 0; i<flush_coun; i++)
  {
    xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
    memcpy(trans_buf_1,map,LVGL_DMA_BUFF_LEN);
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, trans_buf_1);
    offsety1 += offgap;
    offsety2 += offgap;
    map += dmalen;
  }
  xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
  lv_disp_flush_ready(disp);
#else
  const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
  const int offgap = (MFS_LCD_V_RES / flush_coun);
  const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
  int offsetx1 = 0;
  int offsety1 = 0;
  int offsetx2 = MFS_LCD_H_RES;
  int offsety2 = offgap;

  uint16_t *map = (uint16_t *)color_p;
  xSemaphoreGive(flush_done_semaphore);
  for(int i = 0; i<flush_coun; i++)
  {
    xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
    memcpy(trans_buf_1,map,LVGL_DMA_BUFF_LEN);
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, trans_buf_1);
    offsety1 += offgap;
    offsety2 += offgap;
    map += dmalen;
  }
  xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
  lv_disp_flush_ready(disp);
#endif
}
static void TouchInputReadCallback(lv_indev_t * indev, lv_indev_data_t *indevData)
{
  uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e,0x0, 0x0, 0x0};
  uint8_t buff[32] = {0};
  ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle,read_touchpad_cmd,11,buff,32));
  uint16_t pointX;
  uint16_t pointY;
  pointX = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
  pointY = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];
  ESP_LOGI("Touch","%d,%d",buff[0],buff[1]);
  if (buff[1]>0 && buff[1]<5)
  {
    indevData->state = LV_INDEV_STATE_PRESSED;
    if(pointX > MFS_LCD_V_RES) pointX = MFS_LCD_V_RES;
    if(pointY > MFS_LCD_H_RES) pointY = MFS_LCD_H_RES;
    indevData->point.x = pointY;
    indevData->point.y = (MFS_LCD_V_RES-pointX);
  }
  else 
  {
    indevData->state = LV_INDEV_STATE_RELEASED;
  }
}

bool mfs_lvgl_lock(int timeout_ms)
{
  const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;       
}

void mfs_lvgl_unlock(void)
{
  assert(lvgl_mux && "lvgl_mux must exist");
  xSemaphoreGive(lvgl_mux);
}

void mfs_lvgl_port_task(void *arg)
{
  uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
  for(;;)
  {
    if (mfs_lvgl_lock(-1)) 
    {
      task_delay_ms = lv_timer_handler();
      //Release the mutex
      mfs_lvgl_unlock();
    }
    if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS)
    {
      task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS)
    {
      task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

static void mfs_lcd_pwm_off_early(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)1 << MFS_PIN_NUM_BK_LIGHT);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&gpio_conf));
  ESP_ERROR_CHECK(gpio_set_level(MFS_PIN_NUM_BK_LIGHT, 0));
}

static void mfs_lcd_exio_init(void)
{
  if (io_expander == NULL) {
    i2c_master_bus_handle_t tca9554_i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &tca9554_i2c_bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
  }

  ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, MFS_EXIO_PIN_TOUCH_INT, IO_EXPANDER_INPUT));
  ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, MFS_EXIO_PIN_BL_EN | MFS_EXIO_PIN_LCD_RST | MFS_EXIO_PIN_SYS_EN, IO_EXPANDER_OUTPUT));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_SYS_EN, 1)); // Keep battery power on
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_BL_EN, 0));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_LCD_RST, 1));
}

static void mfs_lcd_reset(void)
{
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
}

extern void init_backlight_pwm(void);
extern void set_backlight_pwm(uint8_t brightness);

static void mfs_lcd_backlight_set(bool enable)
{
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_BL_EN, enable ? 1 : 0));
    if (enable) {
        init_backlight_pwm();
    } else {
        set_backlight_pwm(0);
    }
}

void lvgl_port_init(void)
{
  flush_done_semaphore = xSemaphoreCreateBinary();
  assert(flush_done_semaphore);
  ESP_LOGI(TAG, "Initialize LCD reset and backlight");
  mfs_lcd_pwm_off_early();
  mfs_lcd_exio_init();

  ESP_LOGI(TAG, "Initialize QSPI bus");
  spi_bus_config_t buscfg = {};

    buscfg.data0_io_num = MFS_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = MFS_PIN_NUM_LCD_DATA1;
    buscfg.sclk_io_num = MFS_PIN_NUM_LCD_PCLK;
    buscfg.data2_io_num = MFS_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = MFS_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  ESP_LOGI(TAG, "Install panel IO");
	  esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    
  esp_lcd_panel_io_spi_config_t io_config = {};
	io_config.cs_gpio_num = MFS_PIN_NUM_LCD_CS;                 
    io_config.dc_gpio_num = -1;          
    io_config.spi_mode = 3;              
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;    
    io_config.on_color_trans_done = mfs_notify_lvgl_flush_ready; 
    //io_config.user_ctx = &disp_drv,         
    io_config.lcd_cmd_bits = 32;         
    io_config.lcd_param_bits = 8;        
    io_config.flags.quad_mode = true;                         
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));
    
  axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
    
  esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;

  ESP_LOGI(TAG, "Install panel driver");
  ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

  mfs_lcd_reset();
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  mfs_lcd_backlight_set(true);

  lv_init();

  lv_display_t * disp = lv_display_create(MFS_LCD_H_RES, MFS_LCD_V_RES);  /* 以水平和垂直分辨率（像素）进行基本初始化 */
  lv_display_set_flush_cb(disp, mfs_lvgl_flush_cb);                           /* 设置刷新回调函数以绘制到显示屏 */
  
  uint8_t *buffer_1 = NULL;
  uint8_t *buffer_2 = NULL;
  buffer_1 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
  buffer_2 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
  assert(buffer_1);
  assert(buffer_2);
	trans_buf_1 = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
	assert(trans_buf_1);
  lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_user_data(disp, panel);
#if (Rotated == USER_DISP_ROT_90)
    lvgl_dest = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM); //旋转buf
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
#endif
  /*port indev*/
  lv_indev_t *touch_indev = NULL;
  touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, TouchInputReadCallback);

  ESP_LOGI(TAG, "Install LVGL tick timer");
  esp_timer_create_args_t lvgl_tick_timer_args = {};
    lvgl_tick_timer_args.callback = &mfs_increase_lvgl_tick;
    lvgl_tick_timer_args.name = "lvgl_tick";
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer,LVGL_TICK_PERIOD_MS * 1000));

  lvgl_mux = xSemaphoreCreateMutex();
  assert(lvgl_mux);
  xTaskCreatePinnedToCore(mfs_lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL,0);
  if (mfs_lvgl_lock(-1))
  {
    create_detector_ui();
    mfs_lvgl_unlock();
  }
}

#include "../mfs_api.h"

static void vol_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int vol = lv_slider_get_value(slider);
    current_settings.volume = vol;
    mark_settings_dirty();
    set_audio_volume(vol);
}

static void gain_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int gain = lv_slider_get_value(slider);
    current_settings.audio_gain = gain;
    mark_settings_dirty();
    set_audio_gain(gain);
}

static void scan_btn_event_cb(lv_event_t * e) {
    bool is_scanning = lv_obj_has_state(scan_btn, LV_STATE_CHECKED);
    if (is_scanning) {
        lv_obj_clear_state(scan_btn, LV_STATE_CHECKED);
        lv_label_set_text(scan_label, LV_SYMBOL_PAUSE);
        is_scanning = false;
    } else {
        lv_obj_add_state(scan_btn, LV_STATE_CHECKED);
        lv_label_set_text(scan_label, LV_SYMBOL_PLAY);
        is_scanning = true;
    }
    toggle_scanning(is_scanning);
}



static void wave_btn_event_cb(lv_event_t * e) {
    current_settings.audio_waveform = (current_settings.audio_waveform + 1) % 4;
    if (current_settings.audio_waveform == 0) lv_label_set_text(wave_label, "SQR");
    else if (current_settings.audio_waveform == 1) lv_label_set_text(wave_label, "TRI");
    else if (current_settings.audio_waveform == 2) lv_label_set_text(wave_label, "SIN");
    else lv_label_set_text(wave_label, "GCM");
    mark_settings_dirty();
}

static void mute_btn_event_cb(lv_event_t * e) {
    if (current_settings.is_muted) {
        lv_obj_clear_state(mute_btn, LV_STATE_CHECKED);
        lv_label_set_text(mute_label, LV_SYMBOL_VOLUME_MAX);
        current_settings.is_muted = false;
        toggle_mute(false);
    } else {
        lv_obj_add_state(mute_btn, LV_STATE_CHECKED);
        lv_label_set_text(mute_label, LV_SYMBOL_MUTE);
        current_settings.is_muted = true;
        toggle_mute(true);
    }
    mark_settings_dirty();
}



static lv_obj_t * cc_label;
static lv_obj_t * batt_label;

static void update_cc_btn_color(lv_obj_t* btn, uint16_t cc) {
    lv_color_t color;
    if (cc <= 200) color = lv_color_hex(0x00FFFF); // Cyan
    else if (cc <= 400) color = lv_palette_main(LV_PALETTE_BLUE); // Default Blue
    else if (cc <= 800) color = lv_palette_main(LV_PALETTE_PURPLE); // Purple
    else if (cc <= 1600) color = lv_palette_main(LV_PALETTE_ORANGE); // Orange
    else color = lv_palette_main(LV_PALETTE_RED); // Red
    
    lv_obj_set_style_bg_color(btn, color, 0);
}

static void cycle_count_event_cb(lv_event_t * e) {
    uint16_t c = current_settings.cycle_count;
    // Map existing invalid values safely, and set rotation
    if (c < 200) c = 200;
    else if (c == 200) c = 400;
    else if (c == 400) c = 800;
    else if (c == 800) c = 1600;
    else if (c == 1600) c = 3200;
    else c = 200;
    
    current_settings.cycle_count = c;
    if (cc_label) lv_label_set_text_fmt(cc_label, "%d", c);
    update_cc_btn_color(lv_event_get_target(e), c);
    mark_settings_dirty();
    set_rm3100_cycle_count(c);
}



extern void start_logging(void);
extern void stop_logging(void);
extern void start_wifi(void);
extern void stop_wifi(void);
extern bool wifi_active;

static void log_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * label = (lv_obj_t *)lv_obj_get_child(btn, 0);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        start_logging();
        lv_label_set_text(label, "Stop Log");
    } else {
        stop_logging();
        lv_label_set_text(label, "Start Log");
    }
}

static void brightness_slider_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    
    int duty = 255 - (percent * 255 / 100);
    if (duty > 240) duty = 240; 
    if (duty < 0) duty = 0;
    
    set_backlight_pwm((uint8_t)duty);
    
    current_settings.brightness = percent;
    mark_settings_dirty();
}

static void wifi_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * label = (lv_obj_t *)lv_obj_get_child(btn, 0);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        start_wifi();
        lv_label_set_text(label, "Stop Wi-Fi");
    } else {
        stop_wifi();
        lv_label_set_text(label, "Start Wi-Fi");
    }
}

void update_wifi_ip_label(const char * ip_str) {
    if (mfs_lvgl_lock(-1)) {
        if (ip_label != NULL) {
            lv_label_set_text(ip_label, ip_str);
        }
        if (wifi_btn != NULL && wifi_label != NULL) {
            if (wifi_active) {
                lv_obj_add_state(wifi_btn, LV_STATE_CHECKED);
                lv_label_set_text(wifi_label, "Stop Wi-Fi");
            } else {
                lv_obj_clear_state(wifi_btn, LV_STATE_CHECKED);
                lv_label_set_text(wifi_label, "Start Wi-Fi");
            }
        }
        mfs_lvgl_unlock();
    }
}

static lv_timer_t * cal_msg_timer = NULL;

static void cal_msg_timer_cb(lv_timer_t * timer) {
    if (cal_status_label) {
        lv_label_set_text(cal_status_label, "Ready for Calibration");
        lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    if (cal_progress_bar) {
        lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    }
    cal_msg_timer = NULL;
    lv_timer_del(timer);
}

void show_calibration_result_msg(bool success) {
    if (mfs_lvgl_lock(-1)) {
        is_calibrating = false;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(full_cal_label, "Calibrate");
        }
        if (cal_status_label) {
            if (success) {
                lv_label_set_text(cal_status_label, "Calibration Complete!");
                lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0x00ff00), LV_PART_MAIN);
            } else {
                lv_label_set_text(cal_status_label, "Math Solver Failed!");
                lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xff0000), LV_PART_MAIN);
            }
        }
        
        if (cal_msg_timer) lv_timer_del(cal_msg_timer);
        cal_msg_timer = lv_timer_create(cal_msg_timer_cb, 4000, NULL);
        
        mfs_lvgl_unlock();
    }
}



static void main_tare_btn_event_cb(lv_event_t * e) {
    if (main_tare_state == 0) {
        // RAW -> TARE
        trigger_manual_tare();
        main_tare_state = 1;
        lv_label_set_text(tare_label, "TARE");
        lv_obj_set_style_bg_color(tare_btn, lv_palette_main(LV_PALETTE_RED), 0);
    } else if (main_tare_state == 1) {
        // TARE -> AUTO
        current_settings.auto_tare_on = true;
        set_auto_tare(true);
        mark_settings_dirty();
        main_tare_state = 2;
        lv_label_set_text(tare_label, "AUTO");
        lv_obj_set_style_bg_color(tare_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        // AUTO -> RAW
        current_settings.auto_tare_on = false;
        set_auto_tare(false);
        mark_settings_dirty();
        reset_tare();
        main_tare_state = 0;
        lv_label_set_text(tare_label, "RAW");
        lv_obj_set_style_bg_color(tare_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    }
}

extern void start_on_wand_calibration(void);
extern void stop_on_wand_calibration(void);

static void start_cal_event_cb(lv_event_t * e) {
    if (!is_calibrating) {
        start_on_wand_calibration();
        is_calibrating = true;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text(full_cal_label, "STOP");
        }
        if (cal_status_label) lv_label_set_text(cal_status_label, "Capturing Data: 0%");
        if (cal_progress_bar) lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    } else {
        stop_on_wand_calibration();
        is_calibrating = false;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(full_cal_label, "Calibrate");
        }
        if (cal_status_label) {
            lv_label_set_text(cal_status_label, "Stopped");
            lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        }
        
        if (cal_msg_timer) lv_timer_del(cal_msg_timer);
        cal_msg_timer = lv_timer_create(cal_msg_timer_cb, 3000, NULL);
    }
}


lv_obj_t * main_tv = NULL;

void create_detector_ui(void) {
    lv_obj_t * tv = lv_tileview_create(lv_screen_active());
    main_tv = tv;
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // =====================================
    // Tile 1: Main Detector HUD
    // =====================================
    lv_obj_t * tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_scrollbar_mode(tile1, LV_SCROLLBAR_MODE_OFF);

    title_label = lv_label_create(tile1);
    lv_label_set_text(title_label, "Magnetic Field\nScanner");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label = lv_label_create(tile1);
    lv_label_set_text(fw_label, "FW: " FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label, LV_ALIGN_TOP_MID, 0, 60);

    // Arc / Dial Indicator
    mag_arc = lv_arc_create(tile1);
    lv_obj_set_size(mag_arc, 160, 160);
    lv_arc_set_rotation(mag_arc, 135);
    lv_arc_set_bg_angles(mag_arc, 0, 270);
    lv_arc_set_range(mag_arc, 0, 5000); 
    lv_arc_set_value(mag_arc, 0);
    lv_obj_align(mag_arc, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_remove_style(mag_arc, NULL, LV_PART_KNOB); 
    lv_obj_clear_flag(mag_arc, LV_OBJ_FLAG_CLICKABLE);

    // Radar Grid Lines
    lv_obj_t * h_line = lv_obj_create(mag_arc);
    lv_obj_set_size(h_line, 140, 2);
    lv_obj_set_style_bg_color(h_line, lv_color_hex(0x004400), LV_PART_MAIN); // Dark Green
    lv_obj_set_style_border_width(h_line, 0, LV_PART_MAIN);
    lv_obj_center(h_line);
    
    lv_obj_t * v_line = lv_obj_create(mag_arc);
    lv_obj_set_size(v_line, 2, 140);
    lv_obj_set_style_bg_color(v_line, lv_color_hex(0x004400), LV_PART_MAIN); // Dark Green
    lv_obj_set_style_border_width(v_line, 0, LV_PART_MAIN);
    lv_obj_center(v_line);

    // Crosshair Dot    // Crosshair Dot
    crosshair_dot = lv_obj_create(mag_arc);
    lv_obj_set_size(crosshair_dot, 10, 10);
    lv_obj_set_style_radius(crosshair_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(crosshair_dot, lv_color_hex(0x00ff00), LV_PART_MAIN); // Light Green
    lv_obj_set_style_border_width(crosshair_dot, 0, LV_PART_MAIN);
    lv_obj_center(crosshair_dot); 
    
    // Compass Labels (Videogame Minimap)
    compass_label_n = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_n, "N");
    lv_obj_set_style_text_color(compass_label_n, lv_color_hex(0xff0000), 0); // Red North
    
    compass_label_e = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_e, "E");
    lv_obj_set_style_text_color(compass_label_e, lv_color_hex(0xffffff), 0);
    
    compass_label_s = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_s, "S");
    lv_obj_set_style_text_color(compass_label_s, lv_color_hex(0xffffff), 0);
    
    compass_label_w = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_w, "W");
    lv_obj_set_style_text_color(compass_label_w, lv_color_hex(0xffffff), 0);
    
    // Intercardinal Ticks
    compass_label_ne = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_ne, "NE");
    lv_obj_set_style_text_color(compass_label_ne, lv_color_hex(0x888888), 0);
    
    compass_label_se = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_se, "SE");
    lv_obj_set_style_text_color(compass_label_se, lv_color_hex(0x888888), 0);
    
    compass_label_sw = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_sw, "SW");
    lv_obj_set_style_text_color(compass_label_sw, lv_color_hex(0x888888), 0);
    
    compass_label_nw = lv_label_create(mag_arc);
    lv_label_set_text(compass_label_nw, "NW");
    lv_obj_set_style_text_color(compass_label_nw, lv_color_hex(0x888888), 0);
    // Declination Indicator (Top Right of Arc)
    declination_label = lv_label_create(tile1);
    lv_obj_set_style_text_color(declination_label, lv_color_hex(0x888888), 0); // Gray text
    lv_obj_align(declination_label, LV_ALIGN_TOP_RIGHT, -5, 110); // Matches mag_arc Y position
    
    elevation_label = lv_label_create(tile1);
    lv_obj_set_style_text_color(elevation_label, lv_color_hex(0xffaa00), 0); // Orange/Amber
    lv_obj_align(elevation_label, LV_ALIGN_TOP_LEFT, 5, 110);
    
    // Huge Mag Label
    mag_label = lv_label_create(tile1);
    lv_obj_set_width(mag_label, 172);
    lv_obj_set_style_text_align(mag_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(mag_label, "0");
    lv_obj_set_style_text_color(mag_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(mag_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(mag_label, LV_ALIGN_TOP_MID, 0, 280);

    // Medium nT Label
    nt_label = lv_label_create(tile1);
    lv_obj_set_width(nt_label, 172);
    lv_obj_set_style_text_align(nt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(nt_label, "0.0 nT");
    lv_obj_set_style_text_color(nt_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(nt_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(nt_label, LV_ALIGN_TOP_MID, 0, 340);

    // Polarity Label
    polarity_label = lv_label_create(tile1);
    lv_obj_set_width(polarity_label, 172);
    lv_obj_set_style_text_align(polarity_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(polarity_label, " "); 
    lv_obj_set_style_text_color(polarity_label, lv_color_hex(0xffa500), LV_PART_MAIN); 
    lv_obj_set_style_text_font(polarity_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(polarity_label, LV_ALIGN_TOP_MID, 0, 380);


    mute_btn = lv_button_create(tile1);
    lv_obj_set_size(mute_btn, 70, 50);
    lv_obj_align(mute_btn, LV_ALIGN_TOP_MID, -40, 420);
    lv_obj_add_event_cb(mute_btn, mute_btn_event_cb, LV_EVENT_CLICKED, NULL);
    mute_label = lv_label_create(mute_btn);
    if (current_settings.is_muted) { 
        lv_obj_add_state(mute_btn, LV_STATE_CHECKED); 
        lv_label_set_text(mute_label, LV_SYMBOL_MUTE); 
    } else { 
        lv_label_set_text(mute_label, LV_SYMBOL_VOLUME_MAX); 
    }
    lv_obj_center(mute_label);

    wave_btn = lv_button_create(tile1);
    lv_obj_set_size(wave_btn, 70, 50);
    lv_obj_align(wave_btn, LV_ALIGN_TOP_MID, 40, 420);
    lv_obj_add_event_cb(wave_btn, wave_btn_event_cb, LV_EVENT_CLICKED, NULL);
    wave_label = lv_label_create(wave_btn);
    if (current_settings.audio_waveform == 0) lv_label_set_text(wave_label, "SQR");
    else if (current_settings.audio_waveform == 1) lv_label_set_text(wave_label, "TRI");
    else if (current_settings.audio_waveform == 2) lv_label_set_text(wave_label, "SIN");
    else lv_label_set_text(wave_label, "GCM");
    lv_obj_center(wave_label);
    
    scan_btn = lv_button_create(tile1);
    lv_obj_set_size(scan_btn, 70, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, -40, 490);
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(scan_btn, LV_STATE_CHECKED);
    scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, LV_SYMBOL_PLAY);
    lv_obj_center(scan_label);

    tare_btn = lv_button_create(tile1);
    lv_obj_set_size(tare_btn, 70, 50);
    lv_obj_align(tare_btn, LV_ALIGN_TOP_MID, 40, 490);
    lv_obj_set_style_bg_color(tare_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(tare_btn, main_tare_btn_event_cb, LV_EVENT_CLICKED, NULL);
    tare_label = lv_label_create(tare_btn);
    lv_label_set_text(tare_label, "RAW");
    lv_obj_center(tare_label);

    // Date/Time Labels
    ui_date_label = lv_label_create(tile1);
    lv_obj_align(ui_date_label, LV_ALIGN_TOP_MID, 0, 560);
    lv_obj_set_style_text_color(ui_date_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(ui_date_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ui_date_label, "00/00/0000");
    
    ui_time_label = lv_label_create(tile1);
    lv_obj_align(ui_time_label, LV_ALIGN_TOP_MID, 0, 590);
    lv_obj_set_style_text_color(ui_time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(ui_time_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ui_time_label, "00:00:00");



    // =====================================
    // Tile 2: Calibration & Tracking
    // =====================================
    lv_obj_t * tile2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);
    lv_obj_set_scrollbar_mode(tile2, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * cal_title = lv_label_create(tile2);
    lv_label_set_text(cal_title, "Calibration");
    lv_obj_set_style_text_align(cal_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cal_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(cal_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(cal_title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label2 = lv_label_create(tile2);
    lv_label_set_text(fw_label2, "FW: " FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label2, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label2, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label2, LV_ALIGN_TOP_MID, 0, 60);

    cal_status_label = lv_label_create(tile2);
    lv_label_set_long_mode(cal_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cal_status_label, 160);
    lv_obj_set_style_text_align(cal_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(cal_status_label, "Ready for Calibration");
    lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(cal_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(cal_status_label, LV_ALIGN_TOP_MID, 0, 140);

    cal_progress_bar = lv_bar_create(tile2);
    lv_obj_set_size(cal_progress_bar, 140, 20);
    lv_obj_align(cal_progress_bar, LV_ALIGN_TOP_MID, 0, 215);
    lv_bar_set_range(cal_progress_bar, 0, 100);
    lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);

    full_cal_btn = lv_button_create(tile2);
    lv_obj_set_size(full_cal_btn, 140, 50);
    lv_obj_align(full_cal_btn, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    full_cal_label = lv_label_create(full_cal_btn);
    lv_label_set_text(full_cal_label, "Calibrate");
    lv_obj_add_event_cb(full_cal_btn, start_cal_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_align(full_cal_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(full_cal_label);


    // =====================================
    // Tile 3: System, Hardware & Data
    // =====================================
    lv_obj_t * tile3 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_scrollbar_mode(tile3, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * sys_title = lv_label_create(tile3);
    batt_label = lv_label_create(tile3);
    lv_label_set_text(batt_label, "Bat: --.--V");
    lv_obj_set_style_text_color(batt_label, lv_color_hex(0xffaa00), LV_PART_MAIN);
    lv_obj_set_style_text_font(batt_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(batt_label, LV_ALIGN_TOP_MID, 0, 90);

    lv_label_set_text(sys_title, "System\n& Hardware");
    lv_obj_set_style_text_align(sys_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(sys_title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label3 = lv_label_create(tile3);
    lv_label_set_text(fw_label3, "FW: " FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label3, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label3, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label3, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t * bright_label = lv_label_create(tile3);
    lv_label_set_text(bright_label, "Brightness");
    lv_obj_set_style_text_color(bright_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(bright_label, LV_ALIGN_TOP_MID, 0, 140);
    
    lv_obj_t * brightness_slider = lv_slider_create(tile3);
    lv_obj_set_width(brightness_slider, 140);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 170);
    lv_slider_set_range(brightness_slider, 1, 100);
    lv_slider_set_value(brightness_slider, current_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * vol_label = lv_label_create(tile3);
    lv_label_set_text(vol_label, "Volume");
    lv_obj_set_style_text_color(vol_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(vol_label, LV_ALIGN_TOP_MID, 0, 210);
    
    lv_obj_t * vol_slider = lv_slider_create(tile3);
    lv_obj_set_width(vol_slider, 140);
    lv_obj_align(vol_slider, LV_ALIGN_TOP_MID, 0, 240);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, current_settings.volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(vol_slider, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * gain_label = lv_label_create(tile3);
    lv_label_set_text(gain_label, "Audio Gain");
    lv_obj_set_style_text_color(gain_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(gain_label, LV_ALIGN_TOP_MID, 0, 280);
    
    lv_obj_t * gain_slider = lv_slider_create(tile3);
    lv_obj_set_width(gain_slider, 140);
    lv_obj_align(gain_slider, LV_ALIGN_TOP_MID, 0, 310);
    lv_slider_set_range(gain_slider, 1, 100);
    lv_slider_set_value(gain_slider, current_settings.audio_gain, LV_ANIM_OFF);
    lv_obj_add_event_cb(gain_slider, gain_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * roller_label = lv_label_create(tile3);
    lv_label_set_text(roller_label, "Cycle Counts");
    lv_obj_set_style_text_color(roller_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(roller_label, LV_ALIGN_TOP_MID, 0, 360);
    
    lv_obj_t * cc_btn = lv_button_create(tile3);
    lv_obj_set_size(cc_btn, 140, 40);
    lv_obj_align(cc_btn, LV_ALIGN_TOP_MID, 0, 390);
    lv_obj_add_event_cb(cc_btn, cycle_count_event_cb, LV_EVENT_CLICKED, NULL);
    cc_label = lv_label_create(cc_btn);
    lv_label_set_text_fmt(cc_label, "%d", current_settings.cycle_count);
    lv_obj_center(cc_label);
    update_cc_btn_color(cc_btn, current_settings.cycle_count);

    lv_obj_t * log_btn = lv_button_create(tile3);
    lv_obj_set_size(log_btn, 140, 40);
    lv_obj_align(log_btn, LV_ALIGN_TOP_MID, 0, 465);
    lv_obj_add_flag(log_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(log_btn, log_btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * log_label = lv_label_create(log_btn);
    lv_label_set_text(log_label, "Start Log");
    lv_obj_center(log_label);

    wifi_btn = lv_button_create(tile3);
    lv_obj_set_size(wifi_btn, 140, 40);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_MID, 0, 525);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(wifi_btn, wifi_btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    wifi_label = lv_label_create(wifi_btn);
    if (wifi_active) {
        lv_obj_add_state(wifi_btn, LV_STATE_CHECKED);
        lv_label_set_text(wifi_label, "Stop Wi-Fi");
    } else {
        lv_label_set_text(wifi_label, "Start Wi-Fi");
    }
    lv_obj_center(wifi_label);

    ip_label = lv_label_create(tile3);
    lv_label_set_text(ip_label, "IP: Disconnected");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(ip_label, LV_ALIGN_TOP_MID, 0, 585);

    // Apply loaded settings to hardware on boot
    int duty = 255 - (current_settings.brightness * 255 / 100);
    if (duty > 240) duty = 240; 
    if (duty < 0) duty = 0;
    set_backlight_pwm((uint8_t)duty);
    
    toggle_mute(current_settings.is_muted);
    set_audio_volume(current_settings.volume);
    set_audio_gain(current_settings.audio_gain);
    set_rm3100_cycle_count(current_settings.cycle_count);
}

void update_detector_ui(const UIData *data) {
    if (mfs_lvgl_lock(-1)) {

        // Update Time Labels
        if (ui_date_label && ui_time_label) {
            char ts[64];
            extern void get_formatted_timestamp(char* buffer, size_t max_len, bool include_ms);
            get_formatted_timestamp(ts, sizeof(ts), false);
            char d_str[16];
            char t_str[16];
            // Format is YYYY-MM-DD_HH-MM-SS.SSS+Offset
            snprintf(d_str, sizeof(d_str), "%c%c/%c%c/%c%c%c%c", ts[5], ts[6], ts[8], ts[9], ts[0], ts[1], ts[2], ts[3]);
            snprintf(t_str, sizeof(t_str), "%c%c:%c%c:%c%c", ts[11], ts[12], ts[14], ts[15], ts[17], ts[18]);
            lv_label_set_text(ui_date_label, d_str);
            lv_label_set_text(ui_time_label, t_str);
        }
        if (cal_progress_bar != NULL && cal_status_label != NULL) {
            if (data->cal_progress > 0 && data->cal_progress < 100) {
                lv_bar_set_value(cal_progress_bar, data->cal_progress, LV_ANIM_ON);
                int remain = (100 - data->cal_progress) * 60 / 100;
                lv_label_set_text_fmt(cal_status_label, "Capturing Data: %d%% (%ds left)", data->cal_progress, remain);
            } else if (data->cal_progress >= 100) {
                lv_bar_set_value(cal_progress_bar, 100, LV_ANIM_ON);
                lv_label_set_text(cal_status_label, "Calculating...");
            }
        }

        if (title_label != NULL) {
            if (data->auto_tare_on) {
                lv_label_set_text(title_label, "Magnetic Field\n(AUTO)");
                lv_obj_set_style_text_color(title_label, lv_color_hex(0x00ff00), LV_PART_MAIN);
            } else if (data->tare_active) {
                lv_label_set_text(title_label, "Magnetic Field\n(TARED)");
                lv_obj_set_style_text_color(title_label, lv_color_hex(0xff0000), LV_PART_MAIN);
            } else {
                lv_label_set_text(title_label, "Magnetic Field\nScanner");
                lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
            }
        }
    
        if (mag_arc != NULL && mag_label != NULL) {
            lv_arc_set_value(mag_arc, (int32_t)data->nt);
            
            static int last_color_state = -1;
            int current_color_state = 0;
            
            if (data->nt > 500) current_color_state = 2; // Red
            else if (data->nt > 150) current_color_state = 1; // Yellow
            else current_color_state = 0; // Green
            
            if (current_color_state != last_color_state) {
                if (current_color_state == 2) {
                    lv_obj_set_style_arc_color(mag_arc, lv_color_hex(0xff0000), LV_PART_INDICATOR);
                    lv_obj_set_style_text_color(mag_label, lv_color_hex(0xff0000), LV_PART_MAIN);
                    if (nt_label != NULL) lv_obj_set_style_text_color(nt_label, lv_color_hex(0xff0000), LV_PART_MAIN);
                } else if (current_color_state == 1) {
                    lv_obj_set_style_arc_color(mag_arc, lv_color_hex(0xffff00), LV_PART_INDICATOR);
                    lv_obj_set_style_text_color(mag_label, lv_color_hex(0xffff00), LV_PART_MAIN);
                    if (nt_label != NULL) lv_obj_set_style_text_color(nt_label, lv_color_hex(0xffff00), LV_PART_MAIN);
                } else {
                    lv_obj_set_style_arc_color(mag_arc, lv_color_hex(0x00ff00), LV_PART_INDICATOR);
                    lv_obj_set_style_text_color(mag_label, lv_color_hex(0x00ff00), LV_PART_MAIN);
                    if (nt_label != NULL) lv_obj_set_style_text_color(nt_label, lv_color_hex(0xffffff), LV_PART_MAIN);
                }
                last_color_state = current_color_state;
            }

            lv_label_set_text_fmt(mag_label, "%ld", (int32_t)data->nt);
            
            if (nt_label != NULL) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%ld", (int32_t)data->mag);
                lv_label_set_text(nt_label, buf);
            }
        }
        
        float batt_v = data->battery_voltage;
        
        // Fine-tune calibration factor if the ESP32 internal reference varies
        // e.g. you can multiply by 1.05 if it reads low
        
        if (batt_label) {
            int b_volts = (int)batt_v;
            int b_cents = (int)((batt_v - b_volts) * 100);
            if (b_cents < 0) b_cents = 0;
            lv_label_set_text_fmt(batt_label, "Bat: %d.%02dV", b_volts, b_cents);
        }


            // Crosshair update
            if (crosshair_dot != NULL) {
                float squelch_nt = 20.0f; 
                if (data->nt < squelch_nt) {
                    lv_obj_add_flag(crosshair_dot, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_clear_flag(crosshair_dot, LV_OBJ_FLAG_HIDDEN);
                    
                    // 1. Calculate horizontal magnitude (2D plane)
                    float H_mag = sqrtf(data->gradX * data->gradX + data->gradY * data->gradY);
                    
                    // 2. Inverse distance mapping (Physical 1/r^3 dipole falloff)
                    // Magnetic fields drop off at the cube of distance. 
                    // Taking the cube root maps the nT field strength back to physical linear distance!
                    // This prevents the dot from rushing to the center too fast at high nT.
                    float radius = 70.0f * powf(squelch_nt / data->nt, 0.33333f);
                    if (radius > 70.0f) radius = 70.0f;
                    
                    float px = 0.0f;
                    float py = 0.0f;
                    
                    if (H_mag > 0.1f) {
                        // Normalize the vector
                        float normX = data->gradX / H_mag;
                        float normY = data->gradY / H_mag;
                        
                        // Map to screen. 
                        // Based on physical testing, the physical Y-axis gradient is inverted relative to the screen.
                        // We map them directly to fix the Y-axis mirroring.
                        px = normX * radius;
                        py = normY * radius; // Removed inversion to fix Y-axis mirroring
                    }
                    
                    // 3. Polarity Color Coding based on trueZ
                    // If Z is heavily positive (North pole pushing up?), make it Red. Else Blue.
                    // If it is mostly a horizontal field, keep it Green.
                    if (data->trueZ > 15.0f) {
                        lv_obj_set_style_bg_color(crosshair_dot, lv_color_hex(0xff3333), LV_PART_MAIN); // Red
                    } else if (data->trueZ < -15.0f) {
                        lv_obj_set_style_bg_color(crosshair_dot, lv_color_hex(0x3388ff), LV_PART_MAIN); // Blue
                    } else {
                        lv_obj_set_style_bg_color(crosshair_dot, lv_color_hex(0x00ff00), LV_PART_MAIN); // Green
                    }

                    lv_obj_align(crosshair_dot, LV_ALIGN_CENTER, (int32_t)px, (int32_t)py);
                }
                
                // --- Declination Indicator Update ---
            if (declination_label != NULL) {
                if (current_settings.mag_declination_deg == 0.0f) {
                    lv_label_set_text(declination_label, "MAG");
                } else {
                    lv_label_set_text(declination_label, "TN");
                }
            }
            
            if (elevation_label != NULL) {
                char elev_buf[16];
                snprintf(elev_buf, sizeof(elev_buf), "%+05.1f°", data->elevation);
                lv_label_set_text(elevation_label, elev_buf);
            }
// --- 9-DOF Compass Minimap Animation ---
                if (compass_label_n != NULL) {
                    float cr = 58.0f; // Compass ring radius (just inside the 70px arc)
                    float yaw_rad = data->azimuth * 3.14159f / 180.0f;
                    
                    int nx = (int)(sinf(-yaw_rad) * cr);
                    int ny = (int)(-cosf(-yaw_rad) * cr);
                    lv_obj_align(compass_label_n, LV_ALIGN_CENTER, nx, ny);
                    
                    int ex = (int)(sinf(1.5708f - yaw_rad) * cr);
                    int ey = (int)(-cosf(1.5708f - yaw_rad) * cr);
                    lv_obj_align(compass_label_e, LV_ALIGN_CENTER, ex, ey);
                    
                    int sx = (int)(sinf(3.14159f - yaw_rad) * cr);
                    int sy = (int)(-cosf(3.14159f - yaw_rad) * cr);
                    lv_obj_align(compass_label_s, LV_ALIGN_CENTER, sx, sy);
                    
                    int wx = (int)(sinf(4.71239f - yaw_rad) * cr);
                    int wy = (int)(-cosf(4.71239f - yaw_rad) * cr);
                    lv_obj_align(compass_label_w, LV_ALIGN_CENTER, wx, wy);
                    
                    // Ticks Animation
                    int nex = (int)(sinf(0.785398f - yaw_rad) * cr);
                    int ney = (int)(-cosf(0.785398f - yaw_rad) * cr);
                    lv_obj_align(compass_label_ne, LV_ALIGN_CENTER, nex, ney);
                    
                    int sex = (int)(sinf(2.356194f - yaw_rad) * cr);
                    int sey = (int)(-cosf(2.356194f - yaw_rad) * cr);
                    lv_obj_align(compass_label_se, LV_ALIGN_CENTER, sex, sey);
                    
                    int swx = (int)(sinf(3.926991f - yaw_rad) * cr);
                    int swy = (int)(-cosf(3.926991f - yaw_rad) * cr);
                    lv_obj_align(compass_label_sw, LV_ALIGN_CENTER, swx, swy);
                    
                    int nwx = (int)(sinf(5.497787f - yaw_rad) * cr);
                    int nwy = (int)(-cosf(5.497787f - yaw_rad) * cr);
                    lv_obj_align(compass_label_nw, LV_ALIGN_CENTER, nwx, nwy);
                }
            }

            // Polarity Label Update
            if (polarity_label != NULL) {
                if (data->is_pin) {
                    lv_label_set_text(polarity_label, "FERROUS PIN");
                } else {
                    lv_label_set_text(polarity_label, " "); // Blank
                }
            }
        
        mfs_lvgl_unlock();
    }
}



