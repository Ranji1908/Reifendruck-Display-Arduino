// Gemeinsamer BLE-Empfang fuer beide Firmware-Varianten (NimBLE-Arduino 1.4.x).
// Hoert passiv auf Werbepakete, dekodiert Reifensensoren und legt sie in eine Queue.
#pragma once

#include <NimBLEDevice.h>

#include "TpmsCore.h"

#ifndef DEBUG_BLE
#define DEBUG_BLE 0
#endif

namespace blet {

inline QueueHandle_t& queue() {
  static QueueHandle_t q = nullptr;
  return q;
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (!dev->haveManufacturerData()) return;
    std::string md = dev->getManufacturerData();
    std::string name = dev->haveName() ? dev->getName() : std::string();

    tpms::Advertisement adv;
    const uint8_t* a = dev->getAddress().getNative();  // little endian
    for (int i = 5; i >= 0; --i) adv.address = (adv.address << 8) | a[i];
    adv.mfr = reinterpret_cast<const uint8_t*>(md.data());
    adv.mfrLen = md.size();
    adv.name = name.c_str();
    static const uint16_t known[] = {0x27A5, 0xA827, 0xA828};
    for (uint16_t u : known)
      if (dev->isAdvertisingService(NimBLEUUID(u))) adv.services[adv.serviceCount++] = u;

    tpms::SensorReading r;
    bool ok = tpms::decodeAdvertisement(adv, r);
    if (ok) {
      r.rssi = (int8_t)dev->getRSSI();
      xQueueSend(queue(), &r, 0);  // BLE-Task -> loop()
    }
#if DEBUG_BLE
    Serial.printf("BLE %s '%s' rssi=%d %s mfr=", dev->getAddress().toString().c_str(), name.c_str(),
                  dev->getRSSI(), ok ? tpms::sensorTypeName(r.type) : "-");
    for (unsigned char c : md) Serial.printf("%02X", c);
    Serial.println();
#endif
  }
};

inline void begin() {
  queue() = xQueueCreate(32, sizeof(tpms::SensorReading));
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), true /* Duplikate erwuenscht */);
  scan->setActiveScan(true);  // manche Sensoren senden ihren Namen erst in der Scan-Antwort
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setDuplicateFilter(false);
  scan->setMaxResults(0);  // nichts zwischenspeichern, nur Callback
  scan->start(0, nullptr, false);
}

// Regelmaessig aufrufen: startet den Scan neu, falls er beendet wurde.
inline void keepAlive() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) scan->start(0, nullptr, false);
}

inline bool poll(tpms::SensorReading& r) { return xQueueReceive(queue(), &r, 0) == pdTRUE; }

}  // namespace blet
