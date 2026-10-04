/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * ESP32-S3-N16R8 with the Bread Compact WiFi LCD expansion board. Pins and
 * panel settings follow xiaozhi-esp32's bread-compact-wifi-lcd/config.h:
 * https://github.com/78/xiaozhi-esp32/blob/main/main/boards/bread-compact-wifi-lcd/config.h
 * The supplied kit image identifies the 2.0-inch ST7789 240x320 display,
 * pre-soldered audio amp and 3 W speaker. The board provides an INMP441-style
 * I2S mic and MAX98357-style I2S amp; no I2C codec or amp-enable line is used.
 */
#include <math.h>

#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"

static const char *TAG = "board";

#define LCD_W 240
#define LCD_H 320
#define LCD_HOST SPI3_HOST
#define LCD_SCLK GPIO_NUM_21
#define LCD_MOSI GPIO_NUM_47
#define LCD_CS GPIO_NUM_41
#define LCD_DC GPIO_NUM_40
#define LCD_RST GPIO_NUM_45
#define LCD_BL GPIO_NUM_42
#define DRAW_BUF_LINES 32

#define TALK_GPIO GPIO_NUM_0

#define MIC_I2S I2S_NUM_0
#define MIC_WS GPIO_NUM_4
#define MIC_BCLK GPIO_NUM_5
#define MIC_DIN GPIO_NUM_6
#define SPK_I2S I2S_NUM_1
#define SPK_BCLK GPIO_NUM_15
#define SPK_WS GPIO_NUM_16
#define SPK_DOUT GPIO_NUM_7
#define AUDIO_BLOCK_FRAMES 256

static esp_lcd_panel_handle_t s_panel;
static muse_gpio_button_t s_talk;
static i2s_chan_handle_t s_mic_rx;
static i2s_chan_handle_t s_spk_tx;
static bool s_mic_on;
static bool s_spk_on;
static int s_mic_gain_q8 = 256;

static esp_err_t init(void)
{
    return muse_gpio_button_init(&s_talk, TALK_GPIO);
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    (void)touch;
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_ch) != ESP_OK) {
        return NULL;
    }

    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_W * DRAW_BUF_LINES * sizeof(uint16_t),
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_swap_xy(s_panel, false);
    esp_lcd_panel_mirror(s_panel, false, false);
    esp_lcd_panel_disp_on_off(s_panel, true);

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_W,
            .ver_res = LCD_H,
            .buffer_height = DRAW_BUF_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp || esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep);
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static int mic_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h;
    (void)type;
    if (on == s_mic_on) {
        return ESP_CODEC_DEV_OK;
    }
    esp_err_t err = on ? i2s_channel_enable(s_mic_rx) : i2s_channel_disable(s_mic_rx);
    if (err == ESP_OK) {
        s_mic_on = on;
    }
    return err == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int mic_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    if (size <= 0 || size % (2 * sizeof(int16_t)) != 0) {
        return ESP_CODEC_DEV_READ_FAIL;
    }

    int16_t *dst = (int16_t *)data;
    size_t frames_left = (size_t)size / (2 * sizeof(int16_t));
    int32_t raw[AUDIO_BLOCK_FRAMES * 2];
    while (frames_left) {
        size_t frames = frames_left > AUDIO_BLOCK_FRAMES ? AUDIO_BLOCK_FRAMES : frames_left;
        size_t bytes = frames * 2 * sizeof(raw[0]);
        size_t got = 0;
        if (i2s_channel_read(s_mic_rx, raw, bytes, &got, pdMS_TO_TICKS(1000)) != ESP_OK || got != bytes) {
            return ESP_CODEC_DEV_READ_FAIL;
        }
        size_t offset = ((size_t)size / (2 * sizeof(int16_t))) - frames_left;
        for (size_t i = 0; i < frames * 2; i++) {
            /* INMP441 sends signed 24-bit PCM in a 32-bit I2S slot. */
            int sample = raw[i] >> 16;
            sample = sample * s_mic_gain_q8 >> 8;
            dst[offset * 2 + i] = sample > INT16_MAX ? INT16_MAX : sample < INT16_MIN ? INT16_MIN : sample;
        }
        frames_left -= frames;
    }
    return ESP_CODEC_DEV_OK;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    (void)mic;
    s_mic_gain_q8 = (int)(256.0f * powf(10.0f, db / 20.0f));
}

static int spk_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h;
    (void)type;
    if (on == s_spk_on) {
        return ESP_CODEC_DEV_OK;
    }
    esp_err_t err = on ? i2s_channel_enable(s_spk_tx) : i2s_channel_disable(s_spk_tx);
    if (err == ESP_OK) {
        s_spk_on = on;
    }
    return err == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int spk_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    size_t wrote = 0;
    if (i2s_channel_write(s_spk_tx, data, (size_t)size, &wrote, pdMS_TO_TICKS(1000)) != ESP_OK
        || wrote != (size_t)size) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    return ESP_CODEC_DEV_OK;
}

static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_config_t mic_chan = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&mic_chan, NULL, &s_mic_rx), TAG, "mic channel");
    const i2s_std_config_t mic_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_mic_rx, &mic_cfg), TAG, "mic i2s");

    i2s_chan_config_t spk_chan = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S, I2S_ROLE_MASTER);
    spk_chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&spk_chan, &s_spk_tx, NULL), TAG, "speaker channel");
    const i2s_std_config_t spk_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK,
            .ws = SPK_WS,
            .dout = SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk_tx, &spk_cfg), TAG, "speaker i2s");

    static const audio_codec_data_if_t spk_if = { .enable = spk_enable, .write = spk_write };
    static const audio_codec_data_if_t mic_if = { .enable = mic_enable, .read = mic_read };
    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .data_if = &spk_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .data_if = &mic_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk);
}

static void wait_buttons(int timeout_ms)
{
    muse_gpio_button_t *buttons[] = { &s_talk };
    muse_gpio_buttons_wait(buttons, 1, timeout_ms);
}

static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    while (gpio_get_level(TALK_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup(TALK_GPIO, 0), TAG, "button wake");
    esp_deep_sleep_start();
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "ESP32-S3 Bread Compact WiFi LCD",
    .width = LCD_W,
    .height = LCD_H,
    .diagonal_in = 2.0f,
    .round = false,
    .touch = false,
    .talk_button = "BOOT",
    .talk_hint = { LV_ALIGN_BOTTOM_LEFT, 18, -4 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = -1, /* INMP441's L/R strap determines which slot carries audio. */
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    .wait_buttons = wait_buttons,
    .power_off = power_off,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
