#include "clevo/LightingEffects.hpp"

#include "Protocol.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <numbers>
#include <optional>
#include <utility>

namespace clevo {
namespace {

using std::chrono::milliseconds;

milliseconds atLeastOneMillisecond(milliseconds duration)
{
    return std::max(duration, milliseconds(1));
}

std::vector<Rgb> orDefaultPalette(std::vector<Rgb> palette)
{
    return palette.empty() ? defaultEffectPalette() : std::move(palette);
}

// Eased 0 -> 255 -> 0 over one period, starting dark.
std::uint8_t breathingLevel(milliseconds elapsed, milliseconds period)
{
    const double phase = static_cast<double>(elapsed.count() % period.count()) / static_cast<double>(period.count());
    const double level = (1.0 - std::cos(2.0 * std::numbers::pi * phase)) / 2.0;
    return static_cast<std::uint8_t>(std::lround(level * 255.0));
}

std::uint8_t mix(std::uint8_t from, std::uint8_t to, double t)
{
    return static_cast<std::uint8_t>(std::lround(from + (to - from) * t));
}

} // namespace

std::vector<Rgb> defaultEffectPalette()
{
    return {
        {255, 0, 0},
        {0, 0, 255},
        {0, 255, 0},
        {255, 255, 0},
        {0, 255, 255},
        {255, 0, 255},
    };
}

SoftwareEffect breathingEffect(Rgb color, milliseconds period)
{
    period = atLeastOneMillisecond(period);
    return [color, period](milliseconds elapsed) {
        return LightFrame{color, breathingLevel(elapsed, period)};
    };
}

SoftwareEffect colorCycleEffect(std::vector<Rgb> palette, milliseconds transition)
{
    palette = orDefaultPalette(std::move(palette));
    transition = atLeastOneMillisecond(transition);
    return [palette = std::move(palette), transition](milliseconds elapsed) {
        const auto step = static_cast<std::size_t>(elapsed / transition);
        const double t = static_cast<double>((elapsed % transition).count()) / static_cast<double>(transition.count());
        const Rgb from = palette[step % palette.size()];
        const Rgb to = palette[(step + 1) % palette.size()];
        return LightFrame{{mix(from.r, to.r, t), mix(from.g, to.g, t), mix(from.b, to.b, t)}, 255};
    };
}

SoftwareEffect colorfulBreathingEffect(std::vector<Rgb> palette, milliseconds period)
{
    palette = orDefaultPalette(std::move(palette));
    period = atLeastOneMillisecond(period);
    return [palette = std::move(palette), period](milliseconds elapsed) {
        const auto cycle = static_cast<std::size_t>(elapsed / period);
        return LightFrame{palette[cycle % palette.size()], breathingLevel(elapsed, period)};
    };
}

EffectPlayer::EffectPlayer(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

EffectPlayer::~EffectPlayer()
{
    stop();
}

void EffectPlayer::play(SoftwareEffect effect, milliseconds frameInterval)
{
    frameInterval = atLeastOneMillisecond(frameInterval);

    std::scoped_lock lock(m_mutex);
    // Assigning over a running jthread requests it to stop and joins it.
    m_worker = std::jthread([transport = m_transport, effect = std::move(effect),
                             frameInterval](std::stop_token stopToken) {
        using clock = std::chrono::steady_clock;

        std::mutex sleepMutex;
        std::condition_variable_any wakeUp;
        const auto start = clock::now();
        auto nextFrame = start;
        std::optional<LightFrame> shown;

        while (!stopToken.stop_requested()) {
            const auto elapsed = std::chrono::duration_cast<milliseconds>(clock::now() - start);
            const LightFrame frame = effect(elapsed);

            if (!shown || shown->color != frame.color)
                protocol::sendKeyboardColor(*transport, frame.color.r, frame.color.g, frame.color.b);
            if (!shown || shown->brightness != frame.brightness)
                protocol::sendKeyboardBrightness(*transport, frame.brightness);
            shown = frame;

            // A slow driver call must not trigger a burst of catch-up frames.
            nextFrame = std::max(nextFrame + frameInterval, clock::now());
            std::unique_lock sleepLock(sleepMutex);
            wakeUp.wait_until(sleepLock, stopToken, nextFrame, [] { return false; });
        }
    });
}

void EffectPlayer::stop()
{
    std::scoped_lock lock(m_mutex);
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
}

bool EffectPlayer::isPlaying() const
{
    std::scoped_lock lock(m_mutex);
    return m_worker.joinable();
}

} // namespace clevo
