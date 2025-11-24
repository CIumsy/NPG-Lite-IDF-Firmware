// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Copyright (c) 2025 Upside Down Labs
// Author: Mahesh Tupe (tupemahesh@upsidedownlabs.tech)
//
// At Upside Down Labs, we create open-source DIY neuroscience hardware and
// software. Our mission is to make neuroscience affordable and accessible for
// everyone. By supporting us with your purchase, you help spread innovation and
// open science. Thank you for being part of this journey with us!

#include "common.h"
#include "esp_adc/adc_continuous.h"
#include "esp_pm.h"
#include "gap.h"
#include "gatt.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"
#include <stdint.h>
#include <stdio.h>

#define SAMPLING_RATE 250
#define PACKET_LEN 25
#define NUM_CHANNELS 3

#define SAMPLE_SIZE (NUM_CHANNELS * 2 + 1)
// should be less than 256 - 3 (for ble)
#define PACKET_SIZE (PACKET_LEN * SAMPLE_SIZE)

#define CONV_FRAME_SIZE (NUM_CHANNELS * PACKET_LEN * SOC_ADC_DIGI_RESULT_BYTES)
#define MAX_STORE_BUF_SIZE (CONV_FRAME_SIZE * 10)

#define DEBUG // To disable light sleep & enable debug statements
// #define SECONDARY_CH // To use adc channel 3-5, default is 0-2

#ifdef SECONDARY_CH
uint8_t channels[NUM_CHANNELS] = {3, 4, 5};
#else
uint8_t channels[NUM_CHANNELS] = {0, 1, 2};
#endif

/* To Do: add config function which checks max value given by adc and
 * replace 3329 (max output of adc).
 */
// Trying to map adc output to 12bit range
#define MAP(X) ((X) * 4095 / 3329)

const char *TAG = "NPG-IDF";

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

static TaskHandle_t adc_conv_task_handle;
static bool IRAM_ATTR conv_done_cb(adc_continuous_handle_t handle,
                                   const adc_continuous_evt_data_t *edata,
                                   void *user_data) {
  BaseType_t mustYield = pdFALSE;
  // Notify that ADC continuous driver has done enough number of conversions
  vTaskNotifyGiveFromISR(adc_conv_task_handle, &mustYield);

  return (mustYield == pdTRUE);
}

adc_continuous_handle_t adc_handle = NULL;

static void on_stack_reset(int reason) {
  /* On reset, print reset reason to console */
  ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
  /* On stack sync, do advertising initialization */
  adv_init();
}

static void nimble_host_config_init(void) {
  /* Set host callbacks */
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  /* Store host configuration */
  ble_store_config_init();
}

static void nimble_host_task(void *param) {
  /* Task entry log */
  ESP_LOGI(TAG, "nimble host task has been started!");
  /* This function won't return until nimble_port_stop() is executed */
  nimble_port_run();
  /* Clean up at exit */
  vTaskDelete(NULL);
}

void continuous_adc_init() {
  adc_digi_pattern_config_t pattern[NUM_CHANNELS] = {0};
  adc_continuous_handle_cfg_t handle_conf = {
      .max_store_buf_size = MAX_STORE_BUF_SIZE,
      .conv_frame_size = CONV_FRAME_SIZE,
  };

  for (int i = 0; i < NUM_CHANNELS; i++) {
    pattern[i].atten = ADC_ATTEN_DB_12;
    pattern[i].bit_width = ADC_BITWIDTH_12;
    pattern[i].channel = channels[i];
    pattern[i].unit = ADC_UNIT_1;
  }
  adc_continuous_config_t adc_conf = {
      .adc_pattern = pattern,
      .pattern_num = NUM_CHANNELS,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,

      /* should be less than SOC_ADC_SAMPLE_FREQ_THRES_HIGH
       * and more than SOC_ADC_SAMPLE_FREQ_THRES_LOW
       * refer "soc/soc_caps.h"
       */
      .sample_freq_hz = SAMPLING_RATE * NUM_CHANNELS,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_conf, &adc_handle));
  ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &adc_conf));
  adc_continuous_evt_cbs_t cbs = {
      .on_conv_done = conv_done_cb,
  };
  ESP_ERROR_CHECK(
      adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL));
}

static void
adc_conv_task(void *arg) { // changed signature to proper FreeRTOS prototype
  esp_err_t ret = ESP_OK;
  uint32_t size_ret = 0;
  uint8_t result[CONV_FRAME_SIZE] = {0};
  uint16_t adc_reading = 0;
  uint8_t chords_packet[PACKET_LEN][SAMPLE_SIZE];
  uint8_t counter = 0;

  adc_conv_task_handle = xTaskGetCurrentTaskHandle();
  continuous_adc_init();
  adc_continuous_start(adc_handle);
  while (1) {
    while (streaming) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      ret = adc_continuous_read(adc_handle, result, CONV_FRAME_SIZE, &size_ret,
                                0);

      if (ret == ESP_OK && size_ret == CONV_FRAME_SIZE) {
        for (int j = 0; j < PACKET_LEN; j++) {
          chords_packet[j][0] = counter;
          counter++;
          for (int i = 0; i < NUM_CHANNELS; i++) {
            adc_digi_output_data_t *parsed_data =
                (void *)&result[(i + j * NUM_CHANNELS) *
                                SOC_ADC_DIGI_RESULT_BYTES];
            adc_reading = MAP(parsed_data->type2.data);
            chords_packet[j][1 + i * 2] = (adc_reading >> 8) & 0xFF;
            chords_packet[j][2 + i * 2] = adc_reading & 0xFF;
          }
        }
        // send over ble
        struct os_mbuf *om =
            ble_hs_mbuf_from_flat((void *)chords_packet, PACKET_SIZE);
        if (om) {
          int rc = ble_gatts_notify_custom(conn_handle, data_char_handle, om);
          if (rc != 0) {
            ESP_LOGE(TAG, "Notification failed, rc=%d", rc);
          }
        }

#ifdef DEBUG
        uint16_t ch3 =
            (uint16_t)(chords_packet[0][1] << 8) | chords_packet[0][2];
        ESP_LOGI("adc_conv_task", "counter: %d, CH3:%d, data_char_handle:%d",
                 chords_packet[0][0], ch3, data_char_handle);
#endif

      } else {
        ESP_LOGD("adc_conv_task", "Corrupted reading from adc, size_ret:%d",
                 size_ret);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // use pdMS_TO_TICKS for portable delay
  }
}

void app_main(void) {
  /*
   * NVS flash initialization
   * Dependency of BLE stack to store configurations
   */
  int rc = 0;
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  esp_pm_config_t pm_config = {.max_freq_mhz = 160,
                               .min_freq_mhz = 40,
#ifndef DEBUG
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
                               .light_sleep_enable = true
#endif
#endif
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
    return;
  }

  /* NimBLE stack initialization */
  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ", ret);
    return;
  }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
  /* GAP service initialization */
  rc = gap_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
    return;
  }
#endif

  /* GATT server initialization */
  rc = gatt_svc_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
    return;
  }

  /* NimBLE host configuration initialization */
  nimble_host_config_init();

  /* Start NimBLE host task thread and return */
  xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

  xTaskCreate(adc_conv_task, "adc_conv_task", 4 * 1024, NULL, 5, NULL);
}
