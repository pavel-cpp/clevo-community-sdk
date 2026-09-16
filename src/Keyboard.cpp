#include "clevo/Keyboard.hpp"

#include "Protocol.hpp"

#include <array>
#include <string>
#include <utility>

namespace clevo {
namespace {

using namespace protocol;

struct EffectEncoding {
    HardwareEffect effect;
    std::uint8_t firmwareCode; // top byte of the Command::Keyboard word
    std::uint8_t storedMode;   // keyboard mode byte in the settings store
};

constexpr std::array<EffectEncoding, 7> effectEncodings{{
    {HardwareEffect::Random, 0x70, keyboard_mode::Random},
    {HardwareEffect::Breathing, 0x10, keyboard_mode::Breathing},
    {HardwareEffect::Cycle, 0x33, keyboard_mode::Cycle},
    {HardwareEffect::Wave, 0xB0, keyboard_mode::Wave},
    {HardwareEffect::Dance, 0x80, keyboard_mode::Dance},
    {HardwareEffect::Tempo, 0x90, keyboard_mode::Tempo},
    {HardwareEffect::Flash, 0xA0, keyboard_mode::Flash},
}};

constexpr const EffectEncoding &encodingOf(HardwareEffect effect)
{
    for (const auto &encoding : effectEncodings) {
        if (encoding.effect == effect)
            return encoding;
    }
    return effectEncodings.front();
}

std::optional<HardwareEffect> effectForStoredMode(std::uint8_t mode)
{
    for (const auto &encoding : effectEncodings) {
        if (encoding.storedMode == mode)
            return encoding.effect;
    }
    return std::nullopt;
}

} // namespace

KeyboardController::KeyboardController(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

KeyboardState KeyboardController::state() const
{
    auto &transport = *m_transport;

    std::array<std::uint8_t, 3> color{};
    transport.readSettings(store::KeyboardColor.page, store::KeyboardColor.offset, color);

    std::array<std::uint8_t, 3> sleep{};
    transport.readSettings(store::KeyboardSleepTime.page, store::KeyboardSleepTime.offset, sleep);

    KeyboardState state;
    state.enabled = store::readByte(transport, store::KeyboardEnabled) != 0;
    state.color = Rgb{color[0], color[1], color[2]};
    state.brightness = store::readByte(transport, store::KeyboardBrightness);
    state.effect = effectForStoredMode(store::readByte(transport, store::KeyboardMode));
    state.bootEffect = store::readByte(transport, store::KeyboardBootEffect) != 0;

    if (store::readByte(transport, store::KeyboardSleepEnabled) != 0) {
        state.sleepTimeout = std::chrono::hours(sleep[0]) + std::chrono::minutes(sleep[1])
            + std::chrono::seconds(sleep[2]);
    }
    return state;
}

void KeyboardController::setEnabled(bool enabled)
{
    send(*m_transport, Command::Keyboard,
         word(static_cast<std::uint8_t>(KeyboardCode::Power), enabled ? (1u << 16) : 0u));
    store::writeByte(*m_transport, store::KeyboardEnabled, enabled ? 1 : 0);
}

void KeyboardController::setColor(Rgb color)
{
    sendKeyboardColor(*m_transport, color.r, color.g, color.b);

    const std::array<std::uint8_t, 3> stored{color.r, color.g, color.b};
    m_transport->writeSettings(store::KeyboardColor.page, store::KeyboardColor.offset, stored);
    store::writeByte(*m_transport, store::KeyboardMode, keyboard_mode::StaticColor);
}

void KeyboardController::setBrightness(std::uint8_t brightness)
{
    sendKeyboardBrightness(*m_transport, brightness);
    store::writeByte(*m_transport, store::KeyboardBrightness, brightness);
}

void KeyboardController::setEffect(HardwareEffect effect)
{
    const auto &encoding = encodingOf(effect);
    send(*m_transport, Command::Keyboard, word(encoding.firmwareCode, 0));
    store::writeByte(*m_transport, store::KeyboardMode, encoding.storedMode);
}

void KeyboardController::setBootEffectEnabled(bool enabled)
{
    const std::uint8_t value = enabled ? 1 : 0;
    send(*m_transport, Command::Control, control(ControlCode::KeyboardBootEffect, value));
    store::writeByte(*m_transport, store::KeyboardBootEffect, value);
}

Status KeyboardController::setSleepTimeout(std::optional<std::chrono::seconds> timeout)
{
    if (!timeout || timeout->count() == 0) {
        send(*m_transport, Command::Control, control(ControlCode::KeyboardSleepTimer, 0));
        store::writeByte(*m_transport, store::KeyboardSleepEnabled, 0);
        return {};
    }

    if (timeout->count() < 0 || *timeout > MaxSleepTimeout) {
        return makeError(Errc::InvalidArgument,
                         "Keyboard sleep timeout must be between 1 and "
                             + std::to_string(MaxSleepTimeout.count()) + " seconds");
    }

    const auto total = static_cast<std::uint32_t>(timeout->count());
    // Low byte 0xFF marks a custom duration; the seconds follow as 16 bits.
    send(*m_transport, Command::Control, control(ControlCode::KeyboardSleepTimer, (total << 8) | 0xFF));

    const std::array<std::uint8_t, 3> hms{
        static_cast<std::uint8_t>(total / 3600),
        static_cast<std::uint8_t>(total / 60 % 60),
        static_cast<std::uint8_t>(total % 60),
    };
    store::writeByte(*m_transport, store::KeyboardSleepEnabled, 1);
    m_transport->writeSettings(store::KeyboardSleepTime.page, store::KeyboardSleepTime.offset, hms);
    return {};
}

} // namespace clevo
