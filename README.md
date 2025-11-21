# NPG-Lite-IDF-Firmware
Neuro PlayGround (NPG) Lite (ESP-IDF) firmware repository.

Build
```
idf.py set-target esp32c6
idf.py build
```
Flash
```
idf.py flash monitor -p <port>
```

References: 
1. [Apache Mynewt BLE user guide](https://mynewt.apache.org/latest/network/index.html)
2. [ESP-IDF NimBLE GATT SERVER Example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/ble_get_started/nimble/NimBLE_GATT_Server)
3. [ESP-IDF NimBLE Power Save Example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md)
