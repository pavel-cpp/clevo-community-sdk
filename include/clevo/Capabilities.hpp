#pragma once

#include "clevo/Export.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace clevo {

class DchuTransport;

struct CapabilityFlag {
    std::string_view name;
    bool supported;
};

// What the firmware reports this machine supports. Newer BIOSes publish a
// versioned feature block; older ones only answer the individual queries, and
// both sources are normalised into the same fields.
struct CLEVO_SDK_EXPORT Capabilities {
    // Performance and power
    bool powerProfiles = false;
    bool cpuOverclocking = false;
    bool gpuOverclocking = false;
    bool gpuOverclockingInAllModes = false;
    bool energySaving = false;
    bool batteryUtility = false;
    bool batteryChargeControl = false;
    bool energyStar = false;
    bool hardwarePStates = false;
    bool dynamicTuning = false;

    // Keyboard and lighting
    bool rgbKeyboard = false;
    bool perKeyKeyboard = false;
    bool keyboardSleepTimer = false;
    bool flexiKey = false;
    bool flexiAccess = false;
    bool lightBar = false;
    bool logoLighting = false;
    bool touchpadLighting = false;

    // Cooling
    int fanCount = 0;
    bool fanControl = false;
    bool customFanCurves = false;
    bool fanOffset = false;
    bool maxQ = false;
    bool dustCleaning = false;
    bool turboFan = false;
    bool fanless = false;

    // Platform
    bool wakeOnLan = false;
    bool xmpProfiles = false;
    bool hybridGraphicsSwitch = false;
    bool ethernetLed = false;
    bool headphones3d = false;
    bool ess = false;

    // Raw words for anything not modelled above.
    std::uint32_t systemFlags = 0;   // query 70
    std::uint32_t powerFlags = 0;    // query 82
    std::uint32_t coolingFlags = 0;  // query 96
    std::uint32_t platformFlags = 0; // query 122

    [[nodiscard]] static Capabilities detect(DchuTransport &transport);

    // Every boolean above paired with a readable name, in declaration order.
    [[nodiscard]] std::vector<CapabilityFlag> flags() const;
};

} // namespace clevo
