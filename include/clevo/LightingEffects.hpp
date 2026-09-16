#pragma once

#include "clevo/Export.hpp"
#include "clevo/Keyboard.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace clevo {

class DchuTransport;

struct LightFrame {
    Rgb color;
    std::uint8_t brightness = 255;

    friend constexpr bool operator==(LightFrame, LightFrame) = default;
};

// A software effect is a pure function of the time since it started, so any
// effect can be previewed, tested or driven by an external timer.
using SoftwareEffect = std::function<LightFrame(std::chrono::milliseconds elapsed)>;

[[nodiscard]] CLEVO_SDK_EXPORT std::vector<Rgb> defaultEffectPalette();

[[nodiscard]] CLEVO_SDK_EXPORT SoftwareEffect breathingEffect(Rgb color, std::chrono::milliseconds period);
[[nodiscard]] CLEVO_SDK_EXPORT SoftwareEffect colorCycleEffect(std::vector<Rgb> palette,
                                                               std::chrono::milliseconds transition);
// Breathes through the palette, moving to the next color at every dark point.
[[nodiscard]] CLEVO_SDK_EXPORT SoftwareEffect colorfulBreathingEffect(std::vector<Rgb> palette,
                                                                      std::chrono::milliseconds period);

// Plays a software effect on the keyboard from a background thread. Frames
// are pushed straight to the firmware without touching the persistent
// settings store, and unchanged values are not re-sent.
class CLEVO_SDK_EXPORT EffectPlayer {
public:
    explicit EffectPlayer(std::shared_ptr<DchuTransport> transport);
    ~EffectPlayer();

    EffectPlayer(const EffectPlayer &) = delete;
    EffectPlayer &operator=(const EffectPlayer &) = delete;

    void play(SoftwareEffect effect, std::chrono::milliseconds frameInterval = std::chrono::milliseconds(33));
    void stop();
    [[nodiscard]] bool isPlaying() const;

private:
    std::shared_ptr<DchuTransport> m_transport;
    mutable std::mutex m_mutex;
    std::jthread m_worker;
};

} // namespace clevo
