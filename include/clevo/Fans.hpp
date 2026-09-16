#pragma once

#include "clevo/Error.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace clevo {

class DchuTransport;

enum class FanMode : std::uint8_t {
    Automatic = 0,
    Maximum = 1,
    MaxQ = 5,
    Custom = 6,
};

enum class FanId {
    Cpu,
    Gpu,
};

struct FanReading {
    std::uint32_t rpm = 0;
    std::uint8_t dutyPercent = 0;
    std::uint8_t temperatureCelsius = 0;
};

struct FanTelemetry {
    FanReading cpu;
    FanReading gpu;
};

struct FanPoint {
    std::uint8_t temperature = 0; // °C
    std::uint8_t duty = 0;        // %

    friend constexpr bool operator==(FanPoint, FanPoint) = default;
};

// The embedded controller interpolates between four points. The first comes
// from the firmware and the last is pinned at full speed, so only `lower`
// and `upper` are really programmable; keep `base` as reported by
// FanController::defaultCurve().
struct FanCurve {
    static constexpr FanPoint ceiling{100, 100};

    FanPoint base;
    FanPoint lower;
    FanPoint upper;

    friend constexpr bool operator==(const FanCurve &, const FanCurve &) = default;
};

// Temperatures must strictly increase towards the ceiling and duties must
// not decrease.
[[nodiscard]] Status validate(const FanCurve &curve);

class FanController {
public:
    explicit FanController(std::shared_ptr<DchuTransport> transport);

    [[nodiscard]] FanTelemetry telemetry() const;

    // Empty when the stored value is not a known mode.
    [[nodiscard]] std::optional<FanMode> mode() const;
    // Custom re-activates the curves applied last.
    void setMode(FanMode mode);

    [[nodiscard]] std::uint8_t offsetPercent() const;
    Status setOffsetPercent(std::uint8_t percent);

    // The factory curve reported by the embedded controller.
    [[nodiscard]] FanCurve defaultCurve(FanId fan) const;
    // The last custom curve applied, or the factory curve if there is none.
    [[nodiscard]] FanCurve curve(FanId fan) const;
    // Programs both curves and switches to FanMode::Custom.
    Status applyCustomCurves(const FanCurve &cpu, const FanCurve &gpu);

    // Spins the fans in reverse for a short while to clear the heatsinks.
    void startDustCleaning();

private:
    std::shared_ptr<DchuTransport> m_transport;
};

} // namespace clevo
