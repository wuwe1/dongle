#include "nus.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "nus";

// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E and its RX (…02) / TX (…03), little-endian
static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static const char *device_name;
static nus_command_fn command_cb;
static uint16_t tx_handle;
static uint8_t own_addr_type;
// Written by the NimBLE host task, read by the sender: one writer, word-sized, good enough here.
static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool subscribed;
static volatile uint16_t mtu = BLE_ATT_MTU_DFLT;

static int access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                  void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    char buf[96];
    uint16_t len = 0;
    ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf - 1, &len);  // a longer command is cut short
    buf[len] = 0;
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    if (command_cb) command_cb(buf);
    return 0;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &rx_uuid.u,
                    .access_cb = access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = &tx_uuid.u,
                    .access_cb = access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &tx_handle,
                },
                {0},
            },
    },
    {0},
};

static void advertise(void);

static int on_gap(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        conn = BLE_HS_CONN_HANDLE_NONE;
        subscribed = false;
        mtu = BLE_ATT_MTU_DFLT;
        ESP_LOGI(TAG, "disconnected");
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == tx_handle) subscribed = event->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_MTU:
        mtu = event->mtu.value;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    }
    return 0;
}

static void advertise(void) {
    struct ble_hs_adv_fields adv = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (const uint8_t *)device_name,
        .name_len = strlen(device_name),
        .name_is_complete = 1,
    };
    struct ble_hs_adv_fields rsp = {
        .uuids128 = &svc_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc == 0) rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, on_gap, NULL);
    if (rc) ESP_LOGE(TAG, "advertising failed: %d", rc);
}

static void on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    advertise();
}

static void host_task(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void nus_start(const char *name, nus_command_fn on_command) {
    device_name = name;
    command_cb = on_command;
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    ble_svc_gap_device_name_set(name);
    nimble_port_freertos_init(host_task);
}

bool nus_ready(void) { return conn != BLE_HS_CONN_HANDLE_NONE && subscribed; }

bool nus_send(const char *s, size_t n) {
    if (!nus_ready()) return true;
    size_t chunk = mtu > 3 ? mtu - 3 : 20;
    for (size_t off = 0; off < n; off += chunk) {
        size_t len = n - off < chunk ? n - off : chunk;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s + off, len);
        if (!om) return false;
        if (ble_gatts_notify_custom(conn, tx_handle, om) != 0) return false;  // om is consumed
    }
    return true;
}
