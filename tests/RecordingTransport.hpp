#pragma once

#include "clevo/Transport.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

// In-memory stand-in for the DCHU driver: records every command and keeps a
// settings store, so tests can assert on the exact bytes the SDK emits.
class RecordingTransport final : public clevo::DchuTransport {
public:
    struct Sent {
        std::int32_t command;
        std::vector<std::uint8_t> payload;
    };

    std::vector<Sent> sent;
    std::map<std::int32_t, std::uint32_t> words;
    std::map<std::int32_t, clevo::Packet> packets;
    std::map<std::int32_t, clevo::Packet> pages;
    std::function<clevo::Packet(std::int32_t, const clevo::Packet &)> onExchange;

    std::uint32_t queryWord(std::int32_t command) override
    {
        const auto it = words.find(command);
        return it == words.end() ? 0 : it->second;
    }

    clevo::Packet queryPacket(std::int32_t command) override
    {
        const auto it = packets.find(command);
        return it == packets.end() ? clevo::Packet{} : it->second;
    }

    void send(std::int32_t command, std::span<const std::uint8_t> payload) override
    {
        sent.push_back({command, {payload.begin(), payload.end()}});
    }

    clevo::Packet exchange(std::int32_t command, const clevo::Packet &request) override
    {
        return onExchange ? onExchange(command, request) : clevo::Packet{};
    }

    void readSettings(std::int32_t page, std::int32_t offset, std::span<std::uint8_t> out) override
    {
        const clevo::Packet &stored = pages[page];
        std::copy_n(stored.begin() + offset, out.size(), out.begin());
    }

    void writeSettings(std::int32_t page, std::int32_t offset, std::span<const std::uint8_t> data) override
    {
        std::copy(data.begin(), data.end(), pages[page].begin() + offset);
    }
};
