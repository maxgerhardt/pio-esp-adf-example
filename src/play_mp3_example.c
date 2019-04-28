/* Play mp3 file by audio pipeline

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "board.h"

static const char *TAG = "PLAY_MP3_FLASH";
/*
   To embed it in the app binary, the mp3 file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const uint8_t adf_music_mp3_start[] asm("_binary_adf_music_mp3_start");
extern const uint8_t adf_music_mp3_end[]   asm("_binary_adf_music_mp3_end");

void led_test();
void led_test2(void);
void test_buttons(void);
void asr_example();

audio_element_err_t mp3_music_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
    static int mp3_pos;
    int read_size = adf_music_mp3_end - adf_music_mp3_start - mp3_pos;
    if (read_size == 0) {
        return AEL_IO_DONE;
    } else if (len < read_size) {
        read_size = len;
    }
    memcpy(buf, adf_music_mp3_start + mp3_pos, read_size);
    mp3_pos += read_size;
    return (audio_element_err_t) read_size;
}

void app_main(void)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t i2s_stream_writer, mp3_decoder;

    asr_example();

    test_buttons();
    led_test2();

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    led_test();


    ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 2 ] Create audio pipeline, add all elements to pipeline, and subscribe pipeline event");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create mp3 decoder to decode mp3 file and set custom read callback");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    // typedef audio_element_err_t (*stream_func)(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait,
    //void *context);
    audio_element_set_read_cb(mp3_decoder, mp3_music_read_cb, NULL);

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[2.4] Link it together [mp3_music_read_cb]-->mp3_decoder-->i2s_stream-->[codec_chip]");
#if (CONFIG_ESP_LYRAT_V4_3_BOARD || CONFIG_ESP_LYRAT_V4_2_BOARD)
    const char* link_tags[] = {"mp3", "i2s"};
    audio_pipeline_link(pipeline, link_tags, 2);
#endif

    /**Zl38063 does not support 44.1KHZ frequency, so resample needs to be used to convert files to other rates.
     * You can transfer to 16kHZ or 48kHZ.
     */
#if (CONFIG_ESP_LYRATD_MSC_V2_1_BOARD || CONFIG_ESP_LYRATD_MSC_V2_2_BOARD)
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 44100;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = 48000;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.type = AUDIO_CODEC_TYPE_DECODER;
    audio_element_handle_t filter = rsp_filter_init(&rsp_cfg);
    audio_pipeline_register(pipeline, filter, "filter");
    const char* link_tags2[] = {"mp3", "filter", "i2s"};
    audio_pipeline_link(pipeline, link_tags2, 3);
#endif
    ESP_LOGI(TAG, "[ 3 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[3.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);

            /* Es8388 and es8374 use this function to set I2S and codec to the same frequency as the music file, and zl38063
             * does not need this step because the data has been resampled.*/
#if (CONFIG_ESP_LYRAT_V4_3_BOARD || CONFIG_ESP_LYRAT_V4_2_BOARD)
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates , music_info.bits, music_info.channels);
#endif
            continue;
        }
        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_STATE_STOPPED) {
            break;
        }
    }

    ESP_LOGI(TAG, "[ 5 ] Stop audio_pipeline");
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, mp3_decoder);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Make sure audio_pipeline_remove_listener is called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, mp3_decoder);
#if (CONFIG_ESP_LYRATD_MSC_V2_1_BOARD || CONFIG_ESP_LYRATD_MSC_V2_2_BOARD)
    audio_pipeline_unregister(pipeline, filter);
    audio_element_deinit(filter);
#endif
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
}

//extern "C" {
#include <IS31FL3216.h>
//}
is31fl3216_handle_t drvHandle;

void led_inits(){
    drvHandle = is31fl3216_init();
}

void led_trigger(){
    is31fl3216_ch_enable(drvHandle, IS31FL3216_CH_ALL);
    is31fl3216_ch_duty_set(drvHandle, IS31FL3216_CH_ALL, 255);
    vTaskDelay(2000 / portTICK_RATE_MS);
    is31fl3216_ch_disable(drvHandle, IS31FL3216_CH_ALL);
    is31fl3216_ch_duty_set(drvHandle, IS31FL3216_CH_ALL, 0);
}

void led_test() {
	is31fl3216_handle_t drvHandle = is31fl3216_init();

	//repeat 5 times
	for(int j=0; j < 10; j++) {
		for(int i=1; i <= 16; i++) {
			if ( i % 2 == 0){ //light up all even leds
				is31fl3216_ch_enable(drvHandle, (is31_pwm_channel_t) (1 << i));
				is31fl3216_ch_duty_set(drvHandle, (is31_pwm_channel_t) (1 << i), 255);
			} else {
				is31fl3216_ch_disable(drvHandle, (is31_pwm_channel_t) (1 << i));
				is31fl3216_ch_duty_set(drvHandle, (is31_pwm_channel_t) (1 << i), 0);
			}
		}

		vTaskDelay(500 / portTICK_PERIOD_MS );

		for(int i=1; i <= 16; i++) {
			if ( i % 2 == 1){ //light up all uneven
				is31fl3216_ch_enable(drvHandle, (is31_pwm_channel_t) (1 << i));
				is31fl3216_ch_duty_set(drvHandle, (is31_pwm_channel_t) (1 << i), 255);
			} else {
				is31fl3216_ch_disable(drvHandle, (is31_pwm_channel_t) (1 << i));
				is31fl3216_ch_duty_set(drvHandle, (is31_pwm_channel_t) (1 << i), 0);
			}
		}

		vTaskDelay(500 / portTICK_PERIOD_MS );
	}

	// turn on LED channel 1
	//is31fl3216_ch_enable(drvHandle, IS31FL3216_CH_1);
	//is31fl3216_ch_duty_set(drvHandle, IS31FL3216_CH_1, 255);

	//vTaskDelay(2000 / portTICK_PERIOD_MS );

	is31fl3216_ch_disable(drvHandle, IS31FL3216_CH_ALL);
	is31fl3216_ch_duty_set(drvHandle, IS31FL3216_CH_ALL, 0);
	//destroy driver
	is31fl3216_deinit(drvHandle);
}


#include "periph_is31fl3216.h"

//static const char *TAG = "CHECK_ESP32-LyraTD-MSC_LEDs";


void led_test2(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 1 ] Initialize peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[ 2 ] Initialize IS31fl3216 peripheral");
    periph_is31fl3216_cfg_t is31fl3216_cfg = { 0 };
    is31fl3216_cfg.state = IS31FL3216_STATE_ON;
    esp_periph_handle_t is31fl3216_periph = periph_is31fl3216_init(&is31fl3216_cfg);

    ESP_LOGI(TAG, "[ 3 ] Start peripherals");
    esp_err_t ret = esp_periph_start(set, is31fl3216_periph);
    ESP_LOGI(TAG, "[ 3 ] Start peripherals: %d", (int) ret);

    ESP_LOGI(TAG, "[ 4 ] Set duty for each LED index");
    for (int i = 0; i < 14; i++) {
        periph_is31fl3216_set_duty(is31fl3216_periph, i, 64);
        //periph_is31fl3216_set_light_on_num(is31fl3216_periph, i,14);
    }

    ESP_LOGI(TAG, "[ 5 ] Rotate LED pattern");
    int led_index = 0;
    periph_is31fl3216_state_t led_state = IS31FL3216_STATE_ON;
    while (1) {
        int blink_pattern = (1UL << led_index) - 1;
        periph_is31fl3216_set_blink_pattern(is31fl3216_periph, blink_pattern);
        led_index++;
        if (led_index > 14){
            led_index = 0;
            led_state = (led_state == IS31FL3216_STATE_ON) ? IS31FL3216_STATE_FLASH : IS31FL3216_STATE_ON;
            periph_is31fl3216_set_state(is31fl3216_periph, led_state);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "[ 6 ] Destroy peripherals");
    esp_periph_set_destroy(set);
    ESP_LOGI(TAG, "[ 7 ] Finished");
}


#include "periph_adc_button.h"

void test_buttons(void)
{
    ESP_LOGI(TAG, "[ 1 ] Initialize peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.1] Initialize ADC Button peripheral");
    periph_adc_button_cfg_t adc_button_cfg = { 0 };
    adc_arr_t adc_btn_tag = ADC_DEFAULT_ARR();
    adc_button_cfg.arr = &adc_btn_tag;
    adc_button_cfg.arr_size = 1;

    ESP_LOGI(TAG, "[1.2] Start ADC Button peripheral");
    esp_periph_handle_t adc_button_periph = periph_adc_button_init(&adc_button_cfg);
    esp_periph_start(set, adc_button_periph);

    ESP_LOGI(TAG, "[ 2 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGW(TAG, "[ 3 ] Waiting for a button to be pressed ...");


    while (1) {
        char *btn_states[] = {"idle", "click", "click released", "press", "press released"};

        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == PERIPH_ID_ADC_BTN) {
            int button_id = (int)msg.data;  // button id is sent as data_len
            int state     = msg.cmd;       // button state is sent as cmd
            switch (button_id) {
                case USER_KEY_ID0:
                    ESP_LOGI(TAG, "[ * ] Button SET %s", btn_states[state]);
                    break;
                case USER_KEY_ID1:
                    ESP_LOGI(TAG, "[ * ] Button PLAY %s", btn_states[state]);
                    break;
                case USER_KEY_ID2:
                    ESP_LOGI(TAG, "[ * ] Button REC %s", btn_states[state]);
                    break;
                case USER_KEY_ID3:
                    ESP_LOGI(TAG, "[ * ] Button MODE %s", btn_states[state]);
                    break;
                case USER_KEY_ID4:
                    ESP_LOGI(TAG, "[ * ] Button VOL- %s", btn_states[state]);
                    break;
                case USER_KEY_ID5:
                    ESP_LOGI(TAG, "[ * ] Button VOL+ %s", btn_states[state]);
                    break;
                default:
                    ESP_LOGE(TAG, "[ * ] Not supported button id: %d)", button_id);
            }
        }
    }

    ESP_LOGI(TAG, "[ 4 ] Stop & destroy all peripherals and event interface");
    esp_periph_set_destroy(set);
    audio_event_iface_destroy(evt);
}


#include "esp_sr_iface.h"
#include "esp_sr_models.h"
static const char *EVENT_TAG = "asr_event";

typedef enum {
    WAKE_UP = 1,
    OPEN_THE_LIGHT,
    CLOSE_THE_LIGHT,
    VOLUME_INCREASE,
    VOLUME_DOWN,
    PLAY,
    PAUSE,
    MUTE,
    PLAY_LOCAL_MUSIC,
} asr_event_t;

void asr_example()
{
    led_inits();
#if defined CONFIG_ESP_LYRAT_V4_3_BOARD
    gpio_config_t gpio_conf = {
        .pin_bit_mask = 1UL << get_green_led_gpio(),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = 0
    };
    gpio_config(&gpio_conf);
#endif

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set(EVENT_TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "Initialize SR handle");
#if CONFIG_SR_MODEL_WN4_QUANT
    const esp_sr_iface_t *model = &esp_sr_wakenet4_quantized;
#else
    const esp_sr_iface_t *model = &esp_sr_wakenet3_quantized;
#endif
    model_iface_data_t *iface = model->create(DET_MODE_90);
    int num = model->get_word_num(iface);
    for (int i = 1; i <= num; i++) {
        char *name = model->get_word_name(iface, i);
        ESP_LOGI(TAG, "keywords: %s (index = %d)", name, i);
    }
    float threshold = model->get_det_threshold_by_mode(iface, DET_MODE_90, 1);
    int sample_rate = model->get_samp_rate(iface);
    int audio_chunksize = model->get_samp_chunksize(iface);
    ESP_LOGI(EVENT_TAG, "keywords_num = %d, threshold = %f, sample_rate = %d, chunksize = %d, sizeof_uint16 = %d", num, threshold, sample_rate, audio_chunksize, sizeof(int16_t));
    int16_t *buff = (int16_t *)malloc(audio_chunksize * sizeof(short));
    if (NULL == buff) {
        ESP_LOGE(EVENT_TAG, "Memory allocation failed!");
        model->destroy(iface);
        model = NULL;
        return;
    }

    ESP_LOGI(EVENT_TAG, "[ 1 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    audio_pipeline_handle_t pipeline;
    audio_element_handle_t i2s_stream_reader, filter, raw_read;

    ESP_LOGI(EVENT_TAG, "[ 2.0 ] Create audio pipeline for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(EVENT_TAG, "[ 2.1 ] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(EVENT_TAG, "[ 2.2 ] Create filter to resample audio data");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 48000;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = 16000;
    rsp_cfg.dest_ch = 1;
    rsp_cfg.type = AUDIO_CODEC_TYPE_ENCODER;
    filter = rsp_filter_init(&rsp_cfg);

    ESP_LOGI(EVENT_TAG, "[ 2.3 ] Create raw to receive data");
    raw_stream_cfg_t raw_cfg = {
            .out_rb_size = 8 * 1024,
            .type = AUDIO_STREAM_READER,
    };
    raw_read = raw_stream_init(&raw_cfg);

    ESP_LOGI(EVENT_TAG, "[ 3 ] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, filter, "filter");
    audio_pipeline_register(pipeline, raw_read, "raw");

    ESP_LOGI(EVENT_TAG, "[ 4 ] Link elements together [codec_chip]-->i2s_stream-->filter-->raw-->[SR]");
    audio_pipeline_link(pipeline, (const char *[]) {"i2s", "filter", "raw"}, 3);

    ESP_LOGI(EVENT_TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    while (1) {
        raw_stream_read(raw_read, (char *)buff, audio_chunksize * sizeof(short));
        int keyword = model->detect(iface, (int16_t *)buff);
        switch (keyword) {
            case WAKE_UP:
                ESP_LOGI(TAG, "Wake up");
                led_trigger();
                break;
            case OPEN_THE_LIGHT:
                ESP_LOGI(TAG, "Turn on the light");
#if defined CONFIG_ESP_LYRAT_V4_3_BOARD
                gpio_set_level(get_green_led_gpio(), 1);
#endif
                break;
            case CLOSE_THE_LIGHT:
                ESP_LOGI(TAG, "Turn off the light");
#if defined CONFIG_ESP_LYRAT_V4_3_BOARD
                gpio_set_level(get_green_led_gpio(), 0);
#endif
                break;
            case VOLUME_INCREASE:
                ESP_LOGI(TAG, "volume increase");
                break;
            case VOLUME_DOWN:
                ESP_LOGI(TAG, "volume down");
                break;
            case PLAY:
                ESP_LOGI(TAG, "play");
                break;
            case PAUSE:
                ESP_LOGI(TAG, "pause");
                break;
            case MUTE:
                ESP_LOGI(TAG, "mute");
                break;
            case PLAY_LOCAL_MUSIC:
                ESP_LOGI(TAG, "play local music");
                break;
            default:
                ESP_LOGD(TAG, "Not supported keyword");
                break;
        }

    }

    ESP_LOGI(EVENT_TAG, "[ 6 ] Stop audio_pipeline");

    audio_pipeline_terminate(pipeline);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    audio_pipeline_unregister(pipeline, raw_read);
    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, filter);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(raw_read);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(filter);

    ESP_LOGI(EVENT_TAG, "[ 7 ] Destroy model");
    model->destroy(iface);
    model = NULL;
    free(buff);
    buff = NULL;
}