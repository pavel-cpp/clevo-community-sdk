#include "clevo/Capabilities.hpp"

#include "Protocol.hpp"

namespace clevo {
namespace {

using namespace protocol;

// Versioned feature block published in settings page 7 by newer BIOSes.
constexpr std::uint16_t FeatureBlockVersion1 = 0x0100;

std::uint32_t littleEndian(const Packet &packet, std::size_t at, std::size_t bytes)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i)
        value |= static_cast<std::uint32_t>(packet[at + i]) << (8 * i);
    return value;
}

struct FirmwareWords {
    std::uint32_t features = 0; // query 16
    std::uint32_t platform = 0; // query 122
    std::uint32_t cooling = 0;  // query 96
    bool turboFan = false;
    bool dynamicTuning = false;
};

FirmwareWords readFirmwareWords(DchuTransport &transport)
{
    const Packet block = store::readPage(transport, store::BiosFeatureBlock.page);
    const auto version = static_cast<std::uint16_t>((block[0] << 8) | block[1]);

    FirmwareWords words;
    if (version == FeatureBlockVersion1) {
        words.platform = littleEndian(block, 6, 4);
        words.cooling = littleEndian(block, 10, 2);
        words.features = littleEndian(block, 13, 2);
        words.turboFan = bit(block[16], 4);
        words.dynamicTuning = bit(words.cooling, 12);
    } else {
        words.features = query(transport, Query::FirmwareFeatures);
        words.platform = query(transport, Query::PlatformFlags);
        words.cooling = query(transport, Query::CoolingFlags);
    }
    return words;
}

} // namespace

Capabilities Capabilities::detect(DchuTransport &transport)
{
    const FirmwareWords words = readFirmwareWords(transport);
    const Packet fanDefaults = queryPacket(transport, Query::FanDefaults);

    Capabilities caps;
    caps.systemFlags = query(transport, Query::SystemFlags);
    caps.powerFlags = query(transport, Query::PowerFlags);
    caps.coolingFlags = words.cooling;
    caps.platformFlags = words.platform;

    caps.powerProfiles = bit(words.features, 0);
    caps.flexiKey = bit(words.features, 1);
    caps.flexiAccess = bit(words.features, 2);
    caps.perKeyKeyboard = bit(words.features, 3);
    caps.rgbKeyboard = bit(words.features, 4);
    caps.gpuOverclocking = bit(words.features, 5);
    caps.cpuOverclocking = bit(words.features, 6);
    caps.energySaving = bit(words.features, 8);
    caps.batteryUtility = bit(words.features, 9);

    caps.energyStar = bit(caps.powerFlags, 21);
    caps.batteryChargeControl = bit(caps.powerFlags, 27);

    caps.gpuOverclockingInAllModes = bit(words.cooling, 5);
    caps.dustCleaning = bit(words.cooling, 7);
    caps.fanOffset = !bit(words.cooling, 10);
    caps.dynamicTuning = words.dynamicTuning;
    caps.turboFan = words.turboFan;

    caps.headphones3d = bit(words.platform, 2);
    caps.wakeOnLan = bit(words.platform, 5);
    caps.touchpadLighting = bit(words.platform, 11);
    caps.lightBar = bit(words.platform, 12);
    caps.fanless = bit(words.platform, 15);
    caps.logoLighting = bit(words.platform, 18);
    caps.hybridGraphicsSwitch = bit(words.platform, 20);
    caps.ethernetLed = bit(words.platform, 21);
    caps.ess = bit(words.platform, 22);
    caps.xmpProfiles = bit(words.platform, 24);
    caps.keyboardSleepTimer = bit(words.platform, 28);
    caps.hardwarePStates = bit(words.platform, 30);

    caps.fanControl = bit(words.platform, 0) || bit(words.features, 7);
    caps.fanCount = fanDefaults[12];
    caps.maxQ = fanDefaults[14] == static_cast<std::uint8_t>(5);
    caps.customFanCurves = caps.fanCount > 1 && !bit(fanDefaults[43], 1);

    return caps;
}

std::vector<CapabilityFlag> Capabilities::flags() const
{
    return {
        {"Power profiles", powerProfiles},
        {"CPU overclocking", cpuOverclocking},
        {"GPU overclocking", gpuOverclocking},
        {"GPU overclocking in all modes", gpuOverclockingInAllModes},
        {"Energy saving", energySaving},
        {"Battery utility", batteryUtility},
        {"Battery charge control", batteryChargeControl},
        {"Energy Star", energyStar},
        {"Hardware P-states", hardwarePStates},
        {"Dynamic tuning", dynamicTuning},
        {"RGB keyboard", rgbKeyboard},
        {"Per-key keyboard", perKeyKeyboard},
        {"Keyboard sleep timer", keyboardSleepTimer},
        {"FlexiKey", flexiKey},
        {"FlexiAccess", flexiAccess},
        {"Light bar", lightBar},
        {"Logo lighting", logoLighting},
        {"Touchpad lighting", touchpadLighting},
        {"Fan control", fanControl},
        {"Custom fan curves", customFanCurves},
        {"Fan offset", fanOffset},
        {"MaxQ", maxQ},
        {"Dust cleaning", dustCleaning},
        {"Turbo fan", turboFan},
        {"Fanless", fanless},
        {"Wake on LAN", wakeOnLan},
        {"XMP profiles", xmpProfiles},
        {"Hybrid graphics switch", hybridGraphicsSwitch},
        {"Ethernet LED", ethernetLed},
        {"3D headphones", headphones3d},
        {"ESS audio", ess},
    };
}

} // namespace clevo
