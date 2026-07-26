#ifndef DEVICE_INFO_RECORD_H
#define DEVICE_INFO_RECORD_H

#include <cstdint>

/**
 * One decoded FIT `device_info` message (global 23) — the per-sensor identity
 * + battery telemetry the head unit snapshots at ride start/end (Garmin),
 * every ~10 min (Wahoo), or vendor-specific cadence.
 *
 * Raw-first: every message is kept verbatim (no in-binary aggregation) so the
 * JSON consumer, the battery model, and the GPS-strip re-writer all see the
 * same source data. Every field is optional in FIT — paired has-flags mirror
 * the SDK's IsXxxValid() checks.
 *
 * Battery design 2026-07-26: the Favero Assioma emits battery_voltage
 * (quantised 1/256 V) + battery_status, never battery_level; other sensors
 * vary. The GPS stripper MUST pass these messages through (see FitWriter),
 * otherwise the archived ride loses its battery telemetry forever.
 */
struct DeviceInfoRecord {
    bool hasTimestamp = false;
    uint32_t timestamp = 0;          // FIT epoch seconds

    bool hasDeviceIndex = false;
    uint8_t deviceIndex = 0;         // groups samples per physical sensor

    bool hasDeviceType = false;
    uint8_t deviceType = 0;          // raw field 1 (source-type dependent)
    bool hasAntplusDeviceType = false;
    uint8_t antplusDeviceType = 0;   // 11=bike_power, 120=heart_rate, ...

    bool hasManufacturer = false;
    uint16_t manufacturer = 0;       // FIT_MANUFACTURER (263=Favero)
    bool hasProduct = false;
    uint16_t product = 0;            // manufacturer-scoped (Favero 12=Assioma Duo)
    bool hasSerialNumber = false;
    uint32_t serialNumber = 0;
    bool hasAntDeviceNumber = false;
    uint16_t antDeviceNumber = 0;
    bool hasSourceType = false;
    uint8_t sourceType = 0;          // 1=ANT+, 3=BLE, 5=local

    bool hasSoftwareVersion = false;
    float softwareVersion = 0.0f;    // SDK-scaled (÷100)

    bool hasBatteryVoltage = false;
    float batteryVoltage = 0.0f;     // volts, SDK-scaled (÷256)
    bool hasBatteryStatus = false;
    uint8_t batteryStatus = 0;       // 1=new 2=good 3=ok 4=low 5=critical 6=charging
    bool hasBatteryLevel = false;
    uint8_t batteryLevel = 0;        // %, rarely emitted (never by Favero)
};

#endif // DEVICE_INFO_RECORD_H
