#pragma once

// Wire-level vocabulary of the Insyde DCHU interface, recovered from Clevo's
// Control Center. Everything numeric about the protocol lives here.

#include "clevo/Transport.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace clevo::protocol {

// Read-only queries (GetDCHU_Data_Integer / GetDCHU_Data_Buffer).
enum class Query : std::int32_t {
    FanTelemetry = 12,
    FanDefaults = 13,
    FirmwareFeatures = 16,
    SystemFlags = 70,
    PowerFlags = 82,
    CoolingFlags = 96,
    PlatformFlags = 122,
};

// Write commands (SetDCHU_Data / SetDCHU_DataEx).
enum class Command : std::int32_t {
    EmbeddedController = 4,
    FanTable = 14,
    Keyboard = 103,
    Control = 121,
};

// Sub-functions of Command::Control, carried in the payload's top byte.
enum class ControlCode : std::uint8_t {
    FanMode = 1,
    FanOffset = 14,
    KeyboardSleepTimer = 24,
    PowerProfile = 25,
    KeyboardBootEffect = 30,
    DustCleaning = 41,
};

// Sub-functions of Command::Keyboard, carried in the payload's top byte.
enum class KeyboardCode : std::uint8_t {
    Power = 0xE0,
    Color = 0xF0,
    Brightness = 0xF4,
};

using Payload = std::array<std::uint8_t, 4>;

// Commands take a little-endian 32-bit word whose top byte selects the
// sub-function; the value occupies the low 24 bits.
[[nodiscard]] constexpr Payload word(std::uint8_t code, std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        code,
    };
}

[[nodiscard]] constexpr Payload control(ControlCode code, std::uint32_t value)
{
    return word(static_cast<std::uint8_t>(code), value);
}

inline void send(DchuTransport &transport, Command command, std::span<const std::uint8_t> payload)
{
    transport.send(static_cast<std::int32_t>(command), payload);
}

inline void send(DchuTransport &transport, Command command, const Payload &payload)
{
    send(transport, command, std::span<const std::uint8_t>(payload));
}

[[nodiscard]] inline std::uint32_t query(DchuTransport &transport, Query q)
{
    return transport.queryWord(static_cast<std::int32_t>(q));
}

[[nodiscard]] inline Packet queryPacket(DchuTransport &transport, Query q)
{
    return transport.queryPacket(static_cast<std::int32_t>(q));
}

[[nodiscard]] inline std::uint8_t percentToByte(double percent)
{
    return static_cast<std::uint8_t>(std::lround(percent / 100.0 * 255.0));
}

[[nodiscard]] inline std::uint8_t byteToPercent(std::uint8_t value)
{
    return static_cast<std::uint8_t>(std::lround(value / 255.0 * 100.0));
}

[[nodiscard]] inline bool bit(std::uint32_t word, int index)
{
    return ((word >> index) & 1u) != 0;
}

// Addresses in the persistent settings store.
namespace store {

struct Location {
    std::int32_t page;
    std::int32_t offset;
};

inline constexpr Location PowerProfile{1, 1};

inline constexpr Location KeyboardBootEffect{2, 7};
inline constexpr Location KeyboardMode{2, 32};
inline constexpr Location KeyboardBrightness{2, 35};
inline constexpr Location KeyboardSleepEnabled{2, 36};
inline constexpr Location KeyboardSleepTime{2, 37}; // hours, minutes, seconds
inline constexpr Location KeyboardColor{2, 81};     // red, green, blue
inline constexpr Location KeyboardEnabled{2, 84};

inline constexpr std::int32_t FanPage = 4;
inline constexpr Location FanMode{FanPage, 5};
inline constexpr Location FanOffset{FanPage, 7};

inline constexpr Location BiosFeatureBlock{7, 0};

[[nodiscard]] inline std::uint8_t readByte(DchuTransport &transport, Location at)
{
    std::uint8_t value = 0;
    transport.readSettings(at.page, at.offset, std::span<std::uint8_t>(&value, 1));
    return value;
}

inline void writeByte(DchuTransport &transport, Location at, std::uint8_t value)
{
    transport.writeSettings(at.page, at.offset, std::span<const std::uint8_t>(&value, 1));
}

[[nodiscard]] inline Packet readPage(DchuTransport &transport, std::int32_t page)
{
    Packet packet{};
    transport.readSettings(page, 0, packet);
    return packet;
}

} // namespace store

// Keyboard mode byte as persisted in the settings store.
namespace keyboard_mode {
inline constexpr std::uint8_t Random = 0;
inline constexpr std::uint8_t PerZone = 1;
inline constexpr std::uint8_t Breathing = 2;
inline constexpr std::uint8_t Cycle = 3;
inline constexpr std::uint8_t Wave = 4;
inline constexpr std::uint8_t Dance = 5;
inline constexpr std::uint8_t Tempo = 6;
inline constexpr std::uint8_t Flash = 7;
inline constexpr std::uint8_t StaticColor = 8;
} // namespace keyboard_mode

// Live keyboard frames, used by both the controller and the effect player.
inline void sendKeyboardColor(DchuTransport &transport, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    // The firmware expects green, red, blue.
    send(transport, Command::Keyboard, Payload{g, r, b, static_cast<std::uint8_t>(KeyboardCode::Color)});
}

inline void sendKeyboardBrightness(DchuTransport &transport, std::uint8_t brightness)
{
    send(transport, Command::Keyboard, word(static_cast<std::uint8_t>(KeyboardCode::Brightness), brightness));
}

} // namespace clevo::protocol
