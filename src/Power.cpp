#include "clevo/Power.hpp"

#include "Protocol.hpp"

#include <utility>

namespace clevo {

PowerController::PowerController(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

std::optional<PowerProfile> PowerController::profile() const
{
    const std::uint8_t stored = protocol::store::readByte(*m_transport, protocol::store::PowerProfile);
    if (stored > static_cast<std::uint8_t>(PowerProfile::Entertainment))
        return std::nullopt;
    return static_cast<PowerProfile>(stored);
}

void PowerController::setProfile(PowerProfile profile)
{
    const auto value = static_cast<std::uint8_t>(profile);
    protocol::send(*m_transport, protocol::Command::Control,
                   protocol::control(protocol::ControlCode::PowerProfile, value));
    protocol::store::writeByte(*m_transport, protocol::store::PowerProfile, value);
}

} // namespace clevo
