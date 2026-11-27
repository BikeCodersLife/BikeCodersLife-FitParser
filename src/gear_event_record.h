#ifndef GEAR_EVENT_RECORD_H
#define GEAR_EVENT_RECORD_H

#include <cstdint>

/**
 * One decoded FIT `event` message (global 21) of type front_gear_change (42)
 * or rear_gear_change (43), written by Di2 / AXS / EPS drivetrains. A front
 * change normally carries the front fields only, a rear change the rear
 * fields only; any field may be missing (FIT invalid value), so every value
 * has a has-flag, as in DeviceInfoRecord.
 *
 * Gear numbers count from the innermost gear (1 = smallest ring / biggest
 * sprocket); the teeth counts are what the chain wear model uses.
 */
struct GearEventRecord {
    bool hasTimestamp = false;
    uint32_t timestamp = 0;          // FIT epoch seconds

    bool hasFrontGear = false;
    uint8_t frontGear = 0;           // front chainring teeth
    bool hasRearGear = false;
    uint8_t rearGear = 0;            // rear sprocket teeth
    bool hasFrontGearNum = false;
    uint8_t frontGearNum = 0;        // front gear index, 1 = innermost
    bool hasRearGearNum = false;
    uint8_t rearGearNum = 0;         // rear gear index, 1 = innermost
};

#endif // GEAR_EVENT_RECORD_H
