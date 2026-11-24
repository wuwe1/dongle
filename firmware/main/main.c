// dongle v0: a listen-only CAN sniffer. Every frame goes to the USB console as a candump log line;
// over BLE (Nordic UART) a phone gets the frames that pass its filter, and can send commands.
//
// Listen-only is the TWAI controller's hardware mode: it never transmits, not even the ACK bit, so
// it cannot disturb the bus it is plugged into. This firmware has no code path that sends a frame.
//
// Lines: (seconds.micros) can0 ID#DATA      as candump -L writes them; ID is 3 hex digits for
//                                            standard frames, 8 for extended; R for a remote frame
//        # mark SECONDS TEXT                   a marker the phone set ("mark lock"), on the frames' clock
//        # ...                               status and command replies (not frames)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "frame.h"
#include "freertos/task.h"
#include "nus.h"
#include "nvs_flash.h"

static const char *TAG = "dongle";

#define LINE_LEN 64
#define MAX_FILTER 16

static const struct {
    uint32_t bitrate;
    twai_timing_config_t timing;
} rates[] = {
    {500000, TWAI_TIMING_CONFIG_500KBITS()},
    {250000, TWAI_TIMING_CONFIG_250KBITS()},
    {125000, TWAI_TIMING_CONFIG_125KBITS()},
    {1000000, TWAI_TIMING_CONFIG_1MBITS()},
};
#define NRATES (sizeof rates / sizeof rates[0])

typedef struct {
    char s[LINE_LEN];
} line_t;

static QueueHandle_t ble_queue;  // lines for the phone
static uint32_t bitrate;         // 0 until the bus is up
static volatile uint32_t frames, dropped, bus_errors;

// BLE filter: frame IDs the phone wants; none means all. Set from the NimBLE task, read by the CAN
// task.
static portMUX_TYPE filter_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t filter[MAX_FILTER];
static int nfilter;

static bool wanted(uint32_t id) {
    bool yes = true;
    taskENTER_CRITICAL(&filter_lock);
    if (nfilter) {
        yes = false;
        for (int i = 0; i < nfilter; i++)
            if (filter[i] == id) yes = true;
    }
    taskEXIT_CRITICAL(&filter_lock);
    return yes;
}

static void to_phone(const char *s) {
    line_t l;
    strlcpy(l.s, s, sizeof l.s);
    if (xQueueSend(ble_queue, &l, 0) != pdTRUE) dropped++;
}

static void reply_stats(void) {
    char s[LINE_LEN];
    snprintf(s, sizeof s, "# bitrate=%lu frames=%lu dropped=%lu errors=%lu filter=%d\n",
             (unsigned long)bitrate, (unsigned long)frames, (unsigned long)dropped,
             (unsigned long)bus_errors, nfilter);
    to_phone(s);
    fputs(s, stdout);
}

// Commands from the phone:  stats | mark TEXT | filter off | filter 1A0,2B3,...
static void on_command(const char *cmd) {
    if (strncmp(cmd, "mark ", 5) == 0) {
        // Into the log, to cut it into before/during an action (tools/canlog.py diff --base/--action)
        int64_t us = esp_timer_get_time();
        char s[LINE_LEN];
        snprintf(s, sizeof s, "# mark %lld.%06lld %.24s\n", us / 1000000, us % 1000000, cmd + 5);
        fputs(s, stdout);
        to_phone(s);
    } else if (strcmp(cmd, "stats") == 0) {
        reply_stats();
    } else if (strcmp(cmd, "filter off") == 0) {
        taskENTER_CRITICAL(&filter_lock);
        nfilter = 0;
        taskEXIT_CRITICAL(&filter_lock);
        to_phone("# filter off\n");
    } else if (strncmp(cmd, "filter ", 7) == 0) {
        uint32_t ids[MAX_FILTER];
        int n = parse_ids(cmd + 7, ids, MAX_FILTER);
        taskENTER_CRITICAL(&filter_lock);
        memcpy(filter, ids, sizeof ids[0] * n);
        nfilter = n;
        taskEXIT_CRITICAL(&filter_lock);
        char s[LINE_LEN];
        snprintf(s, sizeof s, "# filter %d ids\n", n);
        to_phone(s);
    } else {
        to_phone("# commands: stats | mark TEXT | filter off | filter 1A0,2B3\n");
    }
}

static void bus_start(size_t i) {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        CONFIG_DONGLE_CAN_TX_GPIO, CONFIG_DONGLE_CAN_RX_GPIO, TWAI_MODE_LISTEN_ONLY);
    g.tx_queue_len = 0;  // nothing is ever sent
    g.rx_queue_len = 256;
    g.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&g, &rates[i].timing, &f));
    ESP_ERROR_CHECK(twai_start());
}

static void bus_stop(void) {
    twai_stop();
    twai_driver_uninstall();
}

// Listens at one bitrate for a while: right if clean frames arrive. At the wrong bitrate the
// controller sees only bit errors and no valid frame.
static bool probe(size_t i) {
    bus_start(i);
    int ok = 0, errs = 0;
    int64_t end = esp_timer_get_time() + 1500 * 1000;
    while (esp_timer_get_time() < end) {
        twai_message_t m;
        if (twai_receive(&m, pdMS_TO_TICKS(50)) == ESP_OK) ok++;
        uint32_t alerts;
        if (twai_read_alerts(&alerts, 0) == ESP_OK && (alerts & TWAI_ALERT_BUS_ERROR)) errs++;
    }
    bus_stop();
    ESP_LOGI(TAG, "%lu bit/s: %d frames, %d errors", (unsigned long)rates[i].bitrate, ok, errs);
    return ok >= 3 && errs <= ok / 10;
}

// Finds the bus and starts it. A silent bus (car asleep, or a gateway that passes nothing to the
// OBD port) finds nothing; keep trying.
static void bus_up(void) {
    for (;;) {
        for (size_t i = 0; i < NRATES; i++) {
            if (CONFIG_DONGLE_CAN_BITRATE && rates[i].bitrate != CONFIG_DONGLE_CAN_BITRATE)
                continue;
            if (CONFIG_DONGLE_CAN_BITRATE || probe(i)) {
                bus_start(i);
                bitrate = rates[i].bitrate;
                ESP_LOGI(TAG, "listening at %lu bit/s", (unsigned long)bitrate);
                return;
            }
        }
        ESP_LOGW(TAG, "bus silent at every bitrate; retrying");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void can_task(void *arg) {
    bus_up();
    int64_t last_flush = 0;
    for (;;) {
        twai_message_t m;
        esp_err_t rc = twai_receive(&m, pdMS_TO_TICKS(20));
        if (rc == ESP_OK) {
            frames++;
            line_t l;
            frame_line(l.s, sizeof l.s, esp_timer_get_time(), m.identifier, m.extd, m.rtr,
                       m.data_length_code, m.data);
            fputs(l.s, stdout);
            if (nus_ready() && wanted(m.identifier) && xQueueSend(ble_queue, &l, 0) != pdTRUE)
                dropped++;
        }
        uint32_t alerts;
        if (twai_read_alerts(&alerts, 0) == ESP_OK) {
            if (alerts & TWAI_ALERT_BUS_ERROR) bus_errors++;
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL) dropped++;
        }
        // stdout is block-buffered for throughput; flush when idle or every 50 ms
        int64_t now = esp_timer_get_time();
        if (rc != ESP_OK || now - last_flush > 50 * 1000) {
            fflush(stdout);
            last_flush = now;
        }
    }
}

static void ble_task(void *arg) {
    for (;;) {
        line_t l;
        if (xQueueReceive(ble_queue, &l, portMAX_DELAY) == pdTRUE && !nus_send(l.s, strlen(l.s)))
            dropped++;
    }
}

void app_main(void) {
    esp_err_t rc = nvs_flash_init();  // the BLE stack keeps calibration data here
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        rc = nvs_flash_init();
    }
    ESP_ERROR_CHECK(rc);
    static char stdout_buf[4096];
    setvbuf(stdout, stdout_buf, _IOFBF, sizeof stdout_buf);

    ble_queue = xQueueCreate(64, sizeof(line_t));
    nus_start(CONFIG_DONGLE_BLE_NAME, on_command);
    xTaskCreate(ble_task, "ble_tx", 4096, NULL, 5, NULL);
    xTaskCreate(can_task, "can_rx", 4096, NULL, 6, NULL);
}
