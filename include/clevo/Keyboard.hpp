#pragma once

#include "clevo/Error.hpp"
#include "clevo/Export.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace clevo {

class DchuTransport;

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend constexpr bool operator==(Rgb, Rgb) = default;
};

// Animations rendered by the keyboard controller firmware itself.
enum class HardwareEffect : std::uint8_t {
    Random,
    Breathing,
    Cycle,
    Wave,
    Dance,
    Tempo,
    Flash,
};

struct KeyboardState {
    bool enabled = false;
    Rgb color;
    std::uint8_t brightness = 0;
    // Empty while the keyboard shows a static color.
    std::optional<HardwareEffect> effect;
    bool bootEffect = false;
    // Empty when the backlight never times out.
    std::optional<std::chrono::seconds> sleepTimeout;
};

class CLEVO_SDK_EXPORT KeyboardController {
public:
    static constexpr std::chrono::seconds MaxSleepTimeout{0xFFFF};

    explicit KeyboardController(std::shared_ptr<DchuTransport> transport);

    [[nodiscard]] KeyboardState state() const;

    void setEnabled(bool enabled);
    // Also switches the keyboard from any running effect to a static color.
    void setColor(Rgb color);
    void setBrightness(std::uint8_t brightness);
    void setEffect(HardwareEffect effect);
    void setBootEffectEnabled(bool enabled);
    // std::nullopt disables the timeout. Fails above MaxSleepTimeout.
    Status setSleepTimeout(std::optional<std::chrono::seconds> timeout);

private:
    std::shared_ptr<DchuTransport> m_transport;
};

} // namespace clevo
