#pragma once

#include <memory>
#include <string>

namespace clevo {

class DchuTransport;

class SystemController {
public:
    explicit SystemController(std::shared_ptr<DchuTransport> transport);

    [[nodiscard]] std::string embeddedControllerVersion() const;

    // Sends Ctrl+Win+F24, the chord Clevo's touchpad driver toggles on.
    void toggleTouchpad() const;
    void turnDisplayOff() const;

private:
    std::shared_ptr<DchuTransport> m_transport;
};

} // namespace clevo
