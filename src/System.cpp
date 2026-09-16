#include "clevo/System.hpp"

#include "Protocol.hpp"

#include <windows.h>

#include <array>
#include <utility>

namespace clevo {
namespace {

// Embedded-controller string read: sub-function 0xDE of Command::EmbeddedController
// returns the version in five-character chunks, terminated by '$'.
constexpr std::uint8_t EcVersionFunction = 0xDE;
constexpr std::uint8_t EcVersionChunks = 3;
constexpr std::size_t EcChunkLength = 5;

Packet ecVersionRequest(std::uint8_t chunk)
{
    Packet request{};
    request[0] = 1;
    request[1] = chunk;
    request[6] = EcVersionFunction;
    return request;
}

INPUT keyEvent(WORD key, bool release)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = release ? KEYEVENTF_KEYUP : 0;
    return input;
}

} // namespace

SystemController::SystemController(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

std::string SystemController::embeddedControllerVersion() const
{
    const auto command = static_cast<std::int32_t>(protocol::Command::EmbeddedController);

    // Chunk 0 primes the read; the vendor tool issues it and discards the answer.
    m_transport->exchange(command, ecVersionRequest(0));

    std::string version = "1.";
    for (std::uint8_t chunk = 1; chunk <= EcVersionChunks; ++chunk) {
        const Packet reply = m_transport->exchange(command, ecVersionRequest(chunk));
        version.append(reply.begin() + 1, reply.begin() + 1 + EcChunkLength);
    }

    if (const auto end = version.find('$'); end != std::string::npos)
        version.erase(end);
    while (!version.empty() && version.back() == '\0')
        version.pop_back();
    return version;
}

void SystemController::toggleTouchpad() const
{
    std::array<INPUT, 6> chord{
        keyEvent(VK_CONTROL, false), keyEvent(VK_LWIN, false), keyEvent(VK_F24, false),
        keyEvent(VK_F24, true),      keyEvent(VK_LWIN, true),  keyEvent(VK_CONTROL, true),
    };
    SendInput(static_cast<UINT>(chord.size()), chord.data(), sizeof(INPUT));
}

void SystemController::turnDisplayOff() const
{
    constexpr LPARAM PowerOff = 2;
    PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, PowerOff);
}

} // namespace clevo
