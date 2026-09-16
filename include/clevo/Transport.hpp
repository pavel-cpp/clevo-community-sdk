#pragma once

#include "clevo/Error.hpp"
#include "clevo/Export.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace clevo {

// Fixed-size payload the DCHU driver uses for every buffer-shaped exchange.
using Packet = std::array<std::uint8_t, 256>;

// The raw operations exposed by the Insyde DCHU driver. The typed controllers
// are built on top of this; it is public so tools can issue commands the SDK
// does not model yet, and so tests can substitute a fake.
class CLEVO_SDK_EXPORT DchuTransport {
public:
    virtual ~DchuTransport() = default;

    virtual std::uint32_t queryWord(std::int32_t command) = 0;
    virtual Packet queryPacket(std::int32_t command) = 0;
    virtual void send(std::int32_t command, std::span<const std::uint8_t> payload) = 0;
    virtual Packet exchange(std::int32_t command, const Packet &request) = 0;

    // Persistent key/value pages the vendor stack keeps alongside the
    // firmware state; Clevo's own tools read the "current" settings from here.
    virtual void readSettings(std::int32_t page, std::int32_t offset, std::span<std::uint8_t> out) = 0;
    virtual void writeSettings(std::int32_t page, std::int32_t offset, std::span<const std::uint8_t> data) = 0;
};

// Loads InsydeDCHU.dll. A bare file name is resolved from the application
// directory and System32 only, never from the current working directory.
[[nodiscard]] CLEVO_SDK_EXPORT Result<std::unique_ptr<DchuTransport>> openInsydeTransport(
    const std::filesystem::path &library = L"InsydeDCHU.dll");

} // namespace clevo
