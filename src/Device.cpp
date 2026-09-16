#include "clevo/Device.hpp"

#include <utility>

namespace clevo {

Result<Device> Device::open()
{
    auto transport = openInsydeTransport();
    if (!transport)
        return std::unexpected(std::move(transport.error()));
    return Device(std::shared_ptr<DchuTransport>(std::move(*transport)));
}

Device::Device(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

PowerController Device::power() const
{
    return PowerController(m_transport);
}

KeyboardController Device::keyboard() const
{
    return KeyboardController(m_transport);
}

FanController Device::fans() const
{
    return FanController(m_transport);
}

SystemController Device::system() const
{
    return SystemController(m_transport);
}

Capabilities Device::capabilities() const
{
    return Capabilities::detect(*m_transport);
}

const std::shared_ptr<DchuTransport> &Device::transport() const
{
    return m_transport;
}

} // namespace clevo
