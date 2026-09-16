#pragma once

#include "clevo/Capabilities.hpp"
#include "clevo/Error.hpp"
#include "clevo/Export.hpp"
#include "clevo/Fans.hpp"
#include "clevo/Keyboard.hpp"
#include "clevo/Power.hpp"
#include "clevo/System.hpp"
#include "clevo/Transport.hpp"

#include <memory>

namespace clevo {

// Entry point of the SDK. Controllers are lightweight handles sharing the
// device's transport, so they may be copied and outlive the Device.
class CLEVO_SDK_EXPORT Device {
public:
    [[nodiscard]] static Result<Device> open();

    explicit Device(std::shared_ptr<DchuTransport> transport);

    [[nodiscard]] PowerController power() const;
    [[nodiscard]] KeyboardController keyboard() const;
    [[nodiscard]] FanController fans() const;
    [[nodiscard]] SystemController system() const;
    [[nodiscard]] Capabilities capabilities() const;

    [[nodiscard]] const std::shared_ptr<DchuTransport> &transport() const;

private:
    std::shared_ptr<DchuTransport> m_transport;
};

} // namespace clevo
