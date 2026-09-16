#pragma once

#include "clevo/Export.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace clevo {

class DchuTransport;

enum class PowerProfile : std::uint8_t {
    Quiet = 0,
    PowerSaving = 1,
    Performance = 2,
    Entertainment = 3,
};

class CLEVO_SDK_EXPORT PowerController {
public:
    explicit PowerController(std::shared_ptr<DchuTransport> transport);

    // Empty when the stored value is not a known profile.
    [[nodiscard]] std::optional<PowerProfile> profile() const;
    void setProfile(PowerProfile profile);

private:
    std::shared_ptr<DchuTransport> m_transport;
};

} // namespace clevo
