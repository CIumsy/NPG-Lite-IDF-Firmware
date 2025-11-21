#include "gap.h"
#include "common.h"
#include "gatt.h"
#include <stdint.h>

static const char *TAG = "GAP";
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

bool device_connected = false;
bool streaming = false;
uint16_t conn_handle;
uint16_t data_char_handle;
uint16_t control_char_handle;

/* Private function declarations */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void print_conn_desc(struct ble_gap_conn_desc *desc);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* Private functions */
inline static void format_addr(char *addr_str, uint8_t addr[]) {
  sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2],
          addr[3], addr[4], addr[5]);
}

static void print_conn_desc(struct ble_gap_conn_desc *desc) {
  char addr_str[18] = {0};

  /* Connection handle */
  ESP_LOGI(TAG, "connection handle: %d", desc->conn_handle);

  /* Local ID address */
  format_addr(addr_str, desc->our_id_addr.val);
  ESP_LOGI(TAG, "device id address: type=%d, value=%s", desc->our_id_addr.type,
           addr_str);

  /* Peer ID address */
  format_addr(addr_str, desc->peer_id_addr.val);
  ESP_LOGI(TAG, "peer id address: type=%d, value=%s", desc->peer_id_addr.type,
           addr_str);

  /* Connection info */
  ESP_LOGI(TAG,
           "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
           "encrypted=%d, authenticated=%d, bonded=%d\n",
           desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
           desc->sec_state.encrypted, desc->sec_state.authenticated,
           desc->sec_state.bonded);
}

static void start_advertising(void) {
  int rc = 0;
  /* Start advertising. */
  struct ble_hs_adv_fields adv_fields = {0};
  struct ble_gap_adv_params adv_params = {0};
  const char *name = ble_svc_gap_device_name();

  // Advertising flags: LE General Discoverable Mode, BR/EDR not supported
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  adv_fields.name_len = strlen(name);
  adv_fields.name = (uint8_t *)name;
  adv_fields.name_is_complete = 1;
  adv_fields.tx_pwr_lvl_is_present = 1;
  adv_fields.tx_pwr_lvl = 0;

  rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertising data; rc=%d", rc);
    return;
  }

  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Connectable and undirected
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // General discoverable
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         gap_event_handler, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error starting advertising; rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "BLE Advertising started");
}

/*
 * NimBLE applies an event-driven model to keep GAP service going
 * gap_event_handler is a callback function registered when calling
 * ble_gap_adv_start API and called when a GAP event arrives
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
  int rc = 0;
  struct ble_gap_conn_desc desc;
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    ESP_LOGI(TAG, "Connection established. Status: %s",
             event->connect.status == 0 ? "OK" : "Error");
    if (event->connect.status == 0) {
      rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc != 0) {
        ESP_LOGE(TAG, "failed to find connection by handle, error code: %d",
                 rc);
        return rc;
      }
      conn_handle = event->connect.conn_handle;
      print_conn_desc(&desc);
      device_connected = true;
      struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                          .itvl_max = desc.conn_itvl,
                                          .latency = 3,
                                          .supervision_timeout =
                                              desc.supervision_timeout};
      rc = ble_gap_update_params(event->connect.conn_handle, &params);
      if (rc != 0) {
        ESP_LOGE(TAG, "failed to update connection parameters, error code: %d",
                 rc);
        return rc;
      }
    } else {
      // Restart advertising on error
      start_advertising();
    }
    return rc;
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Disconnected. Reason: %d", event->disconnect.reason);
    device_connected = false;
    streaming = false; // Stop streaming when disconnected
    // Restart advertising
    start_advertising();
    return rc;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "Advertise complete");
    start_advertising();
    return rc;
  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG, "got subscription event!!!!");
    if (event->subscribe.attr_handle == data_char_handle) {
      ESP_LOGI(TAG, "Data Characteristic Subscribe: Notify=%d, Indicate=%d",
               event->subscribe.cur_notify, event->subscribe.cur_indicate);
    }
    if (event->subscribe.attr_handle == control_char_handle) {
      ESP_LOGI(TAG, "Control Characteristic Subscribe: Notify=%d, Indicate=%d",
               event->subscribe.cur_notify, event->subscribe.cur_indicate);
    }
    return rc;
  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU updated to %d", event->mtu.value);
    return rc;
  default:
    return rc;
  }
}

/* Public functions */
void adv_init(void) {
  int rc = 0;
  char addr_str[18] = {0};

  /* Make sure we have proper BT identity address set (random preferred) */
  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "device does not have any available bt address!");
    return;
  }

  /* Figure out BT address to use while advertising (no privacy for now) */
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
    return;
  }

  /* Printing ADDR */
  rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
    return;
  }
  format_addr(addr_str, addr_val);
  ESP_LOGI(TAG, "device address: %s", addr_str);

  /* Start advertising. */
  start_advertising();
}

int gap_init(void) {
  /* Local variables */
  int rc = 0;

  /* Call NimBLE GAP initialization API */
  ble_svc_gap_init();

  /* Set GAP device name */
  rc = ble_svc_gap_device_name_set(DEVICE_NAME);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
             DEVICE_NAME, rc);
    return rc;
  }
  return rc;
}
