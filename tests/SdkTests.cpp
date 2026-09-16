#include "RecordingTransport.hpp"

#include "clevo/Clevo.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TestCase {
    const char *name;
    void (*run)();
};

std::vector<TestCase> &registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

int failures = 0;

struct Register {
    Register(const char *name, void (*run)()) { registry().push_back({name, run}); }
};

#define TEST(name)                                                                                                     \
    void name();                                                                                                       \
    const Register register_##name(#name, &name);                                                                      \
    void name()

#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ++failures;                                                                                                \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expression);                     \
        }                                                                                                              \
    } while (false)

using Bytes = std::vector<std::uint8_t>;

constexpr std::int32_t ControlCommand = 121;
constexpr std::int32_t KeyboardCommand = 103;
constexpr std::int32_t FanTableCommand = 14;
constexpr std::int32_t FanDefaultsQuery = 13;
constexpr std::int32_t FanTelemetryQuery = 12;

struct Fixture {
    std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
    clevo::Device device{transport};
};

bool sentExactly(const RecordingTransport &transport, std::size_t index, std::int32_t command, const Bytes &payload)
{
    return transport.sent.size() > index && transport.sent[index].command == command
        && transport.sent[index].payload == payload;
}

std::uint8_t stored(RecordingTransport &transport, std::int32_t page, std::size_t offset)
{
    return transport.pages[page][offset];
}

clevo::Packet factoryFanDefaults()
{
    clevo::Packet packet{};
    packet[12] = 2; // fan count
    packet[14] = 0; // initial mode
    const std::uint8_t table[] = {40, 89, 60, 153, 80, 204}; // T/duty pairs, duty on 0..255
    for (std::size_t i = 0; i < 6; ++i) {
        packet[16 + i] = table[i];
        packet[24 + i] = table[i];
    }
    return packet;
}

// ---------------------------------------------------------------- power --

TEST(powerProfileIsSentAndPersisted)
{
    Fixture f;
    f.device.power().setProfile(clevo::PowerProfile::Entertainment);

    CHECK(sentExactly(*f.transport, 0, ControlCommand, {3, 0, 0, 25}));
    CHECK(stored(*f.transport, 1, 1) == 3);
    CHECK(f.device.power().profile() == clevo::PowerProfile::Entertainment);
}

TEST(unknownStoredPowerProfileIsEmpty)
{
    Fixture f;
    f.transport->pages[1][1] = 9;
    CHECK(!f.device.power().profile().has_value());
}

// ------------------------------------------------------------- keyboard --

TEST(keyboardColorIsSentGreenRedBlue)
{
    Fixture f;
    f.device.keyboard().setColor({10, 20, 30});

    CHECK(sentExactly(*f.transport, 0, KeyboardCommand, {20, 10, 30, 0xF0}));
    CHECK(stored(*f.transport, 2, 81) == 10);
    CHECK(stored(*f.transport, 2, 82) == 20);
    CHECK(stored(*f.transport, 2, 83) == 30);
    CHECK(stored(*f.transport, 2, 32) == 8);
}

TEST(keyboardPowerAndBrightness)
{
    Fixture f;
    auto keyboard = f.device.keyboard();
    keyboard.setEnabled(true);
    keyboard.setEnabled(false);
    keyboard.setBrightness(175);

    CHECK(sentExactly(*f.transport, 0, KeyboardCommand, {0, 0, 1, 0xE0}));
    CHECK(sentExactly(*f.transport, 1, KeyboardCommand, {0, 0, 0, 0xE0}));
    CHECK(sentExactly(*f.transport, 2, KeyboardCommand, {175, 0, 0, 0xF4}));
    CHECK(stored(*f.transport, 2, 84) == 0);
    CHECK(stored(*f.transport, 2, 35) == 175);
}

TEST(keyboardHardwareEffect)
{
    Fixture f;
    f.device.keyboard().setEffect(clevo::HardwareEffect::Wave);

    CHECK(sentExactly(*f.transport, 0, KeyboardCommand, {0, 0, 0, 0xB0}));
    CHECK(stored(*f.transport, 2, 32) == 4);
    CHECK(f.device.keyboard().state().effect == clevo::HardwareEffect::Wave);
}

TEST(keyboardSleepTimeout)
{
    Fixture f;
    auto keyboard = f.device.keyboard();

    CHECK(keyboard.setSleepTimeout(std::chrono::minutes(10)).has_value());
    // 600 s = 0x0258, packed above the 0xFF "custom duration" marker.
    CHECK(sentExactly(*f.transport, 0, ControlCommand, {0xFF, 0x58, 0x02, 24}));
    CHECK(stored(*f.transport, 2, 36) == 1);
    CHECK(keyboard.state().sleepTimeout == std::chrono::seconds(600));

    CHECK(keyboard.setSleepTimeout(std::nullopt).has_value());
    CHECK(sentExactly(*f.transport, 1, ControlCommand, {0, 0, 0, 24}));
    CHECK(!keyboard.state().sleepTimeout.has_value());
}

TEST(keyboardSleepTimeoutRejectsOverflow)
{
    Fixture f;
    const auto status = f.device.keyboard().setSleepTimeout(std::chrono::hours(20));

    CHECK(!status.has_value());
    CHECK(!status && status.error().code == clevo::Errc::InvalidArgument);
    CHECK(f.transport->sent.empty());
}

TEST(keyboardBootEffect)
{
    Fixture f;
    f.device.keyboard().setBootEffectEnabled(true);

    CHECK(sentExactly(*f.transport, 0, ControlCommand, {1, 0, 0, 30}));
    CHECK(f.device.keyboard().state().bootEffect);
}

// ----------------------------------------------------------------- fans --

TEST(fanModeAndOffset)
{
    Fixture f;
    auto fans = f.device.fans();
    fans.setMode(clevo::FanMode::MaxQ);

    CHECK(sentExactly(*f.transport, 0, ControlCommand, {5, 0, 0, 1}));
    CHECK(fans.mode() == clevo::FanMode::MaxQ);

    CHECK(fans.setOffsetPercent(50).has_value());
    CHECK(sentExactly(*f.transport, 1, ControlCommand, {128, 0, 0, 14}));
    CHECK(fans.offsetPercent() == 50);

    CHECK(!fans.setOffsetPercent(101).has_value());
    CHECK(f.transport->sent.size() == 2);
}

TEST(fanTelemetryDecoding)
{
    Fixture f;
    clevo::Packet packet{};
    packet[2] = 0x03; // CPU tachometer period 862
    packet[3] = 0x5E;
    packet[16] = 128; // CPU duty
    packet[18] = 52;  // CPU °C
    packet[21] = 47;  // GPU °C, fan stopped
    f.transport->packets[FanTelemetryQuery] = packet;

    const auto telemetry = f.device.fans().telemetry();
    CHECK(telemetry.cpu.rpm == 2501);
    CHECK(telemetry.cpu.dutyPercent == 50);
    CHECK(telemetry.cpu.temperatureCelsius == 52);
    CHECK(telemetry.gpu.rpm == 0);
    CHECK(telemetry.gpu.temperatureCelsius == 47);
}

TEST(fanCurveValidation)
{
    CHECK(clevo::validate({{40, 35}, {60, 80}, {80, 99}}).has_value());
    CHECK(!clevo::validate({{40, 35}, {40, 80}, {80, 99}}).has_value());  // flat temperature
    CHECK(!clevo::validate({{40, 35}, {60, 30}, {80, 99}}).has_value());  // falling duty
    CHECK(!clevo::validate({{40, 35}, {60, 80}, {100, 99}}).has_value()); // reaches the ceiling
    CHECK(!clevo::validate({{40, 35}, {60, 80}, {80, 120}}).has_value()); // above 100 %
}

TEST(fanDefaultCurveDecoding)
{
    Fixture f;
    f.transport->packets[FanDefaultsQuery] = factoryFanDefaults();

    const clevo::FanCurve curve = f.device.fans().defaultCurve(clevo::FanId::Cpu);
    CHECK((curve.base == clevo::FanPoint{40, 35}));
    CHECK((curve.lower == clevo::FanPoint{60, 60}));
    CHECK((curve.upper == clevo::FanPoint{80, 80}));
    // An empty store falls back to the factory curve.
    CHECK(f.device.fans().curve(clevo::FanId::Cpu) == curve);
}

TEST(fanCustomCurvesEncoding)
{
    Fixture f;
    f.transport->packets[FanDefaultsQuery] = factoryFanDefaults();

    const clevo::FanCurve cpu{{40, 35}, {60, 80}, {80, 99}};
    const clevo::FanCurve gpu{{40, 35}, {60, 69}, {80, 81}};
    CHECK(f.device.fans().applyCustomCurves(cpu, gpu).has_value());

    CHECK(f.transport->sent.size() == 2);
    const auto &table = f.transport->sent[0];
    CHECK(table.command == FanTableCommand);
    CHECK(table.payload.size() == 256);
    if (table.payload.size() == 256) {
        CHECK(table.payload[2] == 60);
        CHECK(table.payload[3] == 204); // 80 %
        CHECK(table.payload[4] == 80);
        CHECK(table.payload[5] == 252); // 99 %
        // CPU slopes, big-endian 12.4 fixed point of duty/255 per °C.
        CHECK(table.payload[14] == 0 && table.payload[15] == 92);
        CHECK(table.payload[16] == 0 && table.payload[17] == 39);
        CHECK(table.payload[18] == 0 && table.payload[19] == 2);
        CHECK(table.payload[6] == 60 && table.payload[8] == 80);
        // Two-fan machine: the third fan entry stays empty.
        CHECK(table.payload[10] == 0 && table.payload[26] == 0);
    }
    CHECK(sentExactly(*f.transport, 1, ControlCommand, {6, 0, 0, 1}));

    CHECK(stored(*f.transport, 4, 5) == 6);
    CHECK(stored(*f.transport, 4, 6) == 2);
    CHECK(stored(*f.transport, 4, 28) == 92 && stored(*f.transport, 4, 29) == 0); // little-endian in the store
    CHECK(f.device.fans().curve(clevo::FanId::Cpu) == cpu);
    CHECK(f.device.fans().curve(clevo::FanId::Gpu) == gpu);
    CHECK(f.device.fans().mode() == clevo::FanMode::Custom);
}

TEST(fanCustomCurvesRejectInvalidInput)
{
    Fixture f;
    const clevo::FanCurve good{{40, 35}, {60, 80}, {80, 99}};
    const clevo::FanCurve bad{{40, 35}, {90, 80}, {80, 99}};

    const auto status = f.device.fans().applyCustomCurves(good, bad);
    CHECK(!status.has_value());
    CHECK(!status && status.error().message.starts_with("GPU"));
    CHECK(f.transport->sent.empty());
}

// --------------------------------------------------------- capabilities --

TEST(capabilitiesFromLegacyQueries)
{
    Fixture f;
    f.transport->words[16] = 0b1001'0001;       // power profiles, RGB keyboard, fan settings
    f.transport->words[96] = (1u << 7) | (1u << 10); // dust cleaning, no fan offset
    f.transport->words[122] = (1u << 28);        // keyboard sleep timer
    f.transport->words[82] = (1u << 27);         // battery charge control
    f.transport->packets[FanDefaultsQuery] = factoryFanDefaults();

    const auto caps = f.device.capabilities();
    CHECK(caps.powerProfiles && caps.rgbKeyboard && caps.fanControl);
    CHECK(!caps.perKeyKeyboard);
    CHECK(caps.dustCleaning && !caps.fanOffset);
    CHECK(caps.keyboardSleepTimer && caps.batteryChargeControl);
    CHECK(caps.fanCount == 2 && caps.customFanCurves && !caps.maxQ);
}

TEST(capabilitiesPreferVersionedFeatureBlock)
{
    Fixture f;
    f.transport->words[16] = 0xFFFF; // must be ignored in favour of the block
    clevo::Packet &block = f.transport->pages[7];
    block[0] = 0x01;
    block[1] = 0x00;
    block[10] = 0x00;
    block[11] = 0x10; // cooling bit 12: dynamic tuning
    block[13] = 0x10; // features bit 4: RGB keyboard
    block[16] = 0x10; // turbo fan

    const auto caps = f.device.capabilities();
    CHECK(caps.rgbKeyboard && !caps.powerProfiles);
    CHECK(caps.dynamicTuning && caps.turboFan);
    CHECK(caps.fanOffset);
}

// --------------------------------------------------------------- system --

TEST(embeddedControllerVersion)
{
    Fixture f;
    const std::string chunks = "07.03$....";
    f.transport->onExchange = [&](std::int32_t, const clevo::Packet &request) {
        clevo::Packet reply{};
        const std::size_t chunk = request[1];
        if (chunk >= 1 && chunk <= 2) {
            for (std::size_t i = 0; i < 5; ++i)
                reply[1 + i] = static_cast<std::uint8_t>(chunks[(chunk - 1) * 5 + i]);
        }
        return reply;
    };
    CHECK(f.device.system().embeddedControllerVersion() == "1.07.03");
}

// -------------------------------------------------------------- effects --

TEST(breathingEffectStartsDarkAndPeaksMidway)
{
    using std::chrono::milliseconds;
    const auto effect = clevo::breathingEffect({255, 0, 0}, milliseconds(2000));

    CHECK(effect(milliseconds(0)).brightness == 0);
    CHECK(effect(milliseconds(1000)).brightness == 255);
    CHECK(effect(milliseconds(2000)).brightness == 0);
    CHECK((effect(milliseconds(1000)).color == clevo::Rgb{255, 0, 0}));
}

TEST(colorCycleInterpolatesBetweenPaletteEntries)
{
    using std::chrono::milliseconds;
    const auto effect = clevo::colorCycleEffect({{0, 0, 0}, {200, 100, 0}}, milliseconds(1000));

    CHECK((effect(milliseconds(0)).color == clevo::Rgb{0, 0, 0}));
    CHECK((effect(milliseconds(500)).color == clevo::Rgb{100, 50, 0}));
    CHECK((effect(milliseconds(1000)).color == clevo::Rgb{200, 100, 0}));
    CHECK((effect(milliseconds(2000)).color == clevo::Rgb{0, 0, 0}));
}

TEST(colorfulBreathingChangesColorWhileDark)
{
    using std::chrono::milliseconds;
    const auto effect = clevo::colorfulBreathingEffect({{1, 0, 0}, {0, 2, 0}}, milliseconds(1000));

    CHECK((effect(milliseconds(999)).color == clevo::Rgb{1, 0, 0}));
    CHECK((effect(milliseconds(1000)).color == clevo::Rgb{0, 2, 0}));
    CHECK(effect(milliseconds(1000)).brightness == 0);
}

TEST(effectPlayerPushesFramesUntilStopped)
{
    Fixture f;
    clevo::EffectPlayer player(f.transport);
    player.play([](std::chrono::milliseconds) { return clevo::LightFrame{{9, 8, 7}, 100}; },
                std::chrono::milliseconds(5));
    CHECK(player.isPlaying());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    player.stop();

    CHECK(!player.isPlaying());
    // A constant frame is sent once, never repeated, and nothing is persisted.
    CHECK(f.transport->sent.size() == 2);
    CHECK(sentExactly(*f.transport, 0, KeyboardCommand, {8, 9, 7, 0xF0}));
    CHECK(sentExactly(*f.transport, 1, KeyboardCommand, {100, 0, 0, 0xF4}));
    CHECK(f.transport->pages.empty());
}

} // namespace

int main()
{
    for (const TestCase &test : registry()) {
        const int before = failures;
        test.run();
        std::printf("%s %s\n", failures == before ? "[ ok ]" : "[FAIL]", test.name);
    }
    std::printf("\n%zu tests, %d failed checks\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
