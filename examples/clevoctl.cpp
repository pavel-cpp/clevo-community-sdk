// clevoctl - command-line front end for ClevoCommunitySDK.
//
//   clevoctl status                  read-only overview of the machine
//   clevoctl power <quiet|saving|performance|entertainment>
//   clevoctl fan <auto|max|maxq|custom>
//   clevoctl color <r> <g> <b>
//   clevoctl brightness <0-255>

#include "clevo/Clevo.hpp"

#include <charconv>
#include <cstdio>
#include <optional>
#include <string_view>

namespace {

const char *yesNo(bool value)
{
    return value ? "yes" : "no";
}

const char *powerName(std::optional<clevo::PowerProfile> profile)
{
    if (!profile)
        return "unknown";
    switch (*profile) {
    case clevo::PowerProfile::Quiet:
        return "quiet";
    case clevo::PowerProfile::PowerSaving:
        return "power saving";
    case clevo::PowerProfile::Performance:
        return "performance";
    case clevo::PowerProfile::Entertainment:
        return "entertainment";
    }
    return "unknown";
}

const char *fanModeName(std::optional<clevo::FanMode> mode)
{
    if (!mode)
        return "unknown";
    switch (*mode) {
    case clevo::FanMode::Automatic:
        return "automatic";
    case clevo::FanMode::Maximum:
        return "maximum";
    case clevo::FanMode::MaxQ:
        return "MaxQ";
    case clevo::FanMode::Custom:
        return "custom";
    }
    return "unknown";
}

std::optional<std::uint8_t> parseByte(std::string_view text)
{
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || value > 255)
        return std::nullopt;
    return static_cast<std::uint8_t>(value);
}

void printCurve(const char *name, const clevo::FanCurve &curve)
{
    std::printf("  %s curve: %u°C/%u%%  %u°C/%u%%  %u°C/%u%%  100°C/100%%\n", name, curve.base.temperature,
                curve.base.duty, curve.lower.temperature, curve.lower.duty, curve.upper.temperature,
                curve.upper.duty);
}

int status(const clevo::Device &device)
{
    const auto caps = device.capabilities();
    const auto keyboard = device.keyboard().state();
    const auto fans = device.fans();
    const auto telemetry = fans.telemetry();

    std::printf("Embedded controller  %s\n", device.system().embeddedControllerVersion().c_str());
    std::printf("Power profile        %s\n", powerName(device.power().profile()));
    std::printf("\nKeyboard\n");
    std::printf("  enabled %s, color #%02X%02X%02X, brightness %u, boot effect %s\n", yesNo(keyboard.enabled),
                keyboard.color.r, keyboard.color.g, keyboard.color.b, keyboard.brightness, yesNo(keyboard.bootEffect));
    if (keyboard.sleepTimeout)
        std::printf("  sleeps after %lld s\n", static_cast<long long>(keyboard.sleepTimeout->count()));

    std::printf("\nFans (%d, mode %s, offset %u%%)\n", caps.fanCount, fanModeName(fans.mode()), fans.offsetPercent());
    std::printf("  CPU %u RPM  %u%%  %u°C\n", telemetry.cpu.rpm, telemetry.cpu.dutyPercent,
                telemetry.cpu.temperatureCelsius);
    std::printf("  GPU %u RPM  %u%%  %u°C\n", telemetry.gpu.rpm, telemetry.gpu.dutyPercent,
                telemetry.gpu.temperatureCelsius);
    if (caps.customFanCurves) {
        printCurve("CPU", fans.curve(clevo::FanId::Cpu));
        printCurve("GPU", fans.curve(clevo::FanId::Gpu));
    }

    std::printf("\nCapabilities\n");
    for (const auto &flag : caps.flags())
        std::printf("  %-32.*s %s\n", static_cast<int>(flag.name.size()), flag.name.data(), yesNo(flag.supported));
    return 0;
}

int usage()
{
    std::fputs("usage: clevoctl status\n"
               "       clevoctl power <quiet|saving|performance|entertainment>\n"
               "       clevoctl fan <auto|max|maxq|custom>\n"
               "       clevoctl color <r> <g> <b>\n"
               "       clevoctl brightness <0-255>\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
        return usage();

    auto device = clevo::Device::open();
    if (!device) {
        std::fprintf(stderr, "clevoctl: %s\n", device.error().message.c_str());
        return 1;
    }

    const std::string_view command = argv[1];

    if (command == "status")
        return status(*device);

    if (command == "power" && argc == 3) {
        const std::string_view name = argv[2];
        if (name == "quiet")
            device->power().setProfile(clevo::PowerProfile::Quiet);
        else if (name == "saving")
            device->power().setProfile(clevo::PowerProfile::PowerSaving);
        else if (name == "performance")
            device->power().setProfile(clevo::PowerProfile::Performance);
        else if (name == "entertainment")
            device->power().setProfile(clevo::PowerProfile::Entertainment);
        else
            return usage();
        return 0;
    }

    if (command == "fan" && argc == 3) {
        const std::string_view name = argv[2];
        if (name == "auto")
            device->fans().setMode(clevo::FanMode::Automatic);
        else if (name == "max")
            device->fans().setMode(clevo::FanMode::Maximum);
        else if (name == "maxq")
            device->fans().setMode(clevo::FanMode::MaxQ);
        else if (name == "custom")
            device->fans().setMode(clevo::FanMode::Custom);
        else
            return usage();
        return 0;
    }

    if (command == "color" && argc == 5) {
        const auto r = parseByte(argv[2]);
        const auto g = parseByte(argv[3]);
        const auto b = parseByte(argv[4]);
        if (!r || !g || !b)
            return usage();
        device->keyboard().setColor({*r, *g, *b});
        return 0;
    }

    if (command == "brightness" && argc == 3) {
        const auto value = parseByte(argv[2]);
        if (!value)
            return usage();
        device->keyboard().setBrightness(*value);
        return 0;
    }

    return usage();
}
