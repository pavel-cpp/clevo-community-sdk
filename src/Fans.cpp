#include "clevo/Fans.hpp"

#include "Protocol.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace clevo {
namespace {

using namespace protocol;

// Query::FanTelemetry layout.
constexpr std::size_t CpuRpmHigh = 2;
constexpr std::size_t GpuRpmHigh = 4;
constexpr std::size_t CpuDuty = 16;
constexpr std::size_t CpuTemperature = 18;
constexpr std::size_t GpuDuty = 19;
constexpr std::size_t GpuTemperature = 21;

// The tachometer reports a period; this folds the vendor's
// 60 / (5.565e-5 * period) * 2 into one constant.
constexpr double RpmPeriodNumerator = 2156250.0;

// Query::FanDefaults layout: each fan lists T1, D1, T2, D2, T3, D3 with duty
// on a 0..255 scale.
constexpr std::size_t DefaultsFanCount = 12;
constexpr std::size_t DefaultsInitialMode = 14;
constexpr std::size_t DefaultsCpu = 16;
constexpr std::size_t DefaultsGpu = 24;
constexpr std::size_t DefaultsSecondGpu = 32;

// Command::FanTable layout.
constexpr std::size_t TableCpu = 2;
constexpr std::size_t TableGpu = 6;
constexpr std::size_t TableSecondGpu = 10;
constexpr std::size_t TableCpuSlopes = 14;
constexpr std::size_t TableGpuSlopes = 20;
constexpr std::size_t TableSecondGpuSlopes = 26;

// Settings store fan page layout: a header, then one record per fan.
constexpr std::size_t StoreVersion = 0;
constexpr std::size_t StoreInitialMode = 4;
constexpr std::size_t StoreMode = 5;
constexpr std::size_t StoreFanCount = 6;
constexpr std::size_t StoreCpu = 16;
constexpr std::size_t StoreGpu = 34;
constexpr std::size_t StoreSecondGpu = 52;

constexpr std::uint8_t StoreFormatVersion = 3;

constexpr std::size_t storeRecordOffset(FanId fan)
{
    return fan == FanId::Cpu ? StoreCpu : StoreGpu;
}

constexpr std::size_t defaultsOffset(FanId fan)
{
    return fan == FanId::Cpu ? DefaultsCpu : DefaultsGpu;
}

FanCurve decodeDefaults(const Packet &packet, std::size_t at)
{
    FanCurve curve;
    curve.base = {packet[at], byteToPercent(packet[at + 1])};
    curve.lower = {packet[at + 2], byteToPercent(packet[at + 3])};
    curve.upper = {packet[at + 4], byteToPercent(packet[at + 5])};

    // Factory tables occasionally round to equal or inverted duties.
    curve.lower.duty = std::max(curve.lower.duty, curve.base.duty);
    curve.upper.duty = std::clamp(curve.upper.duty, curve.lower.duty, FanCurve::ceiling.duty);
    return curve;
}

FanCurve decodeStoreRecord(const Packet &page, std::size_t at)
{
    FanCurve curve;
    curve.base = {page[at + 6], page[at + 0]};
    curve.lower = {page[at + 7], page[at + 1]};
    curve.upper = {page[at + 8], page[at + 2]};
    return curve;
}

// Firmware slope between two points: duty on the 0..255 scale per degree, in
// 12.4 fixed point.
std::uint16_t slope(FanPoint from, FanPoint to)
{
    const double perDegree = static_cast<double>(to.duty - from.duty) / (to.temperature - from.temperature);
    return static_cast<std::uint16_t>(std::lround(perDegree * 2.55 * 16.0));
}

struct Slopes {
    std::uint16_t baseToLower;
    std::uint16_t lowerToUpper;
    std::uint16_t upperToCeiling;
};

Slopes slopesOf(const FanCurve &curve)
{
    return {slope(curve.base, curve.lower), slope(curve.lower, curve.upper), slope(curve.upper, FanCurve::ceiling)};
}

void writeBigEndian(Packet &packet, std::size_t at, std::uint16_t value)
{
    packet[at] = static_cast<std::uint8_t>(value >> 8);
    packet[at + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void writeLittleEndian(Packet &packet, std::size_t at, std::uint16_t value)
{
    packet[at] = static_cast<std::uint8_t>(value & 0xFF);
    packet[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void encodeTableEntry(Packet &table, std::size_t pointsAt, std::size_t slopesAt, const FanCurve &curve)
{
    table[pointsAt] = curve.lower.temperature;
    table[pointsAt + 1] = percentToByte(curve.lower.duty);
    table[pointsAt + 2] = curve.upper.temperature;
    table[pointsAt + 3] = percentToByte(curve.upper.duty);

    const Slopes slopes = slopesOf(curve);
    writeBigEndian(table, slopesAt, slopes.baseToLower);
    writeBigEndian(table, slopesAt + 2, slopes.lowerToUpper);
    writeBigEndian(table, slopesAt + 4, slopes.upperToCeiling);
}

void encodeStoreRecord(Packet &page, std::size_t at, const FanCurve &curve, const FanCurve &factory)
{
    page[at + 0] = curve.base.duty;
    page[at + 1] = curve.lower.duty;
    page[at + 2] = curve.upper.duty;
    page[at + 3] = FanCurve::ceiling.duty;
    page[at + 4] = factory.lower.duty;
    page[at + 5] = factory.upper.duty;
    page[at + 6] = curve.base.temperature;
    page[at + 7] = curve.lower.temperature;
    page[at + 8] = curve.upper.temperature;
    page[at + 9] = FanCurve::ceiling.temperature;
    page[at + 10] = factory.lower.temperature;
    page[at + 11] = factory.upper.temperature;

    const Slopes slopes = slopesOf(curve);
    writeLittleEndian(page, at + 12, slopes.baseToLower);
    writeLittleEndian(page, at + 14, slopes.lowerToUpper);
    writeLittleEndian(page, at + 16, slopes.upperToCeiling);
}

FanReading decodeReading(const Packet &packet, std::size_t rpmAt, std::size_t dutyAt, std::size_t temperatureAt)
{
    const unsigned period = (static_cast<unsigned>(packet[rpmAt]) << 8) | packet[rpmAt + 1];

    FanReading reading;
    reading.rpm = period == 0 ? 0 : static_cast<std::uint32_t>(std::lround(RpmPeriodNumerator / period));
    reading.dutyPercent = byteToPercent(packet[dutyAt]);
    reading.temperatureCelsius = packet[temperatureAt];
    return reading;
}

Status validateNamed(const FanCurve &curve, const char *name)
{
    auto status = validate(curve);
    if (!status)
        status.error().message = std::string(name) + " fan curve: " + status.error().message;
    return status;
}

} // namespace

Status validate(const FanCurve &curve)
{
    if (std::max({curve.base.duty, curve.lower.duty, curve.upper.duty}) > FanCurve::ceiling.duty)
        return makeError(Errc::InvalidArgument, "fan duty cannot exceed 100 %");

    const std::array<FanPoint, 4> points{curve.base, curve.lower, curve.upper, FanCurve::ceiling};
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].temperature <= points[i - 1].temperature)
            return makeError(Errc::InvalidArgument, "temperatures must strictly increase up to 100 °C");
        if (points[i].duty < points[i - 1].duty)
            return makeError(Errc::InvalidArgument, "fan duty must not decrease as temperature rises");
    }
    return {};
}

FanController::FanController(std::shared_ptr<DchuTransport> transport)
    : m_transport(std::move(transport))
{
}

FanTelemetry FanController::telemetry() const
{
    const Packet packet = queryPacket(*m_transport, Query::FanTelemetry);
    return {
        decodeReading(packet, CpuRpmHigh, CpuDuty, CpuTemperature),
        decodeReading(packet, GpuRpmHigh, GpuDuty, GpuTemperature),
    };
}

std::optional<FanMode> FanController::mode() const
{
    switch (store::readByte(*m_transport, store::FanMode)) {
    case static_cast<std::uint8_t>(FanMode::Automatic):
        return FanMode::Automatic;
    case static_cast<std::uint8_t>(FanMode::Maximum):
        return FanMode::Maximum;
    case static_cast<std::uint8_t>(FanMode::MaxQ):
        return FanMode::MaxQ;
    case static_cast<std::uint8_t>(FanMode::Custom):
        return FanMode::Custom;
    default:
        return std::nullopt;
    }
}

void FanController::setMode(FanMode mode)
{
    const auto value = static_cast<std::uint8_t>(mode);
    send(*m_transport, Command::Control, control(ControlCode::FanMode, value));
    store::writeByte(*m_transport, store::FanMode, value);
}

std::uint8_t FanController::offsetPercent() const
{
    return store::readByte(*m_transport, store::FanOffset);
}

Status FanController::setOffsetPercent(std::uint8_t percent)
{
    if (percent > 100)
        return makeError(Errc::InvalidArgument, "fan offset must be between 0 and 100 %");

    send(*m_transport, Command::Control, control(ControlCode::FanOffset, percentToByte(percent)));
    store::writeByte(*m_transport, store::FanOffset, percent);
    return {};
}

FanCurve FanController::defaultCurve(FanId fan) const
{
    return decodeDefaults(queryPacket(*m_transport, Query::FanDefaults), defaultsOffset(fan));
}

FanCurve FanController::curve(FanId fan) const
{
    const Packet page = store::readPage(*m_transport, store::FanPage);
    const FanCurve stored = decodeStoreRecord(page, storeRecordOffset(fan));
    // An untouched store reads back as zeros, which never forms a valid curve.
    return validate(stored) ? stored : defaultCurve(fan);
}

Status FanController::applyCustomCurves(const FanCurve &cpu, const FanCurve &gpu)
{
    if (auto status = validateNamed(cpu, "CPU"); !status)
        return status;
    if (auto status = validateNamed(gpu, "GPU"); !status)
        return status;

    const Packet defaults = queryPacket(*m_transport, Query::FanDefaults);
    const FanCurve factoryCpu = decodeDefaults(defaults, DefaultsCpu);
    const FanCurve factoryGpu = decodeDefaults(defaults, DefaultsGpu);
    // Machines with a third fan keep its factory curve; on others the entry
    // is empty and stays zeroed.
    const FanCurve secondGpu = decodeDefaults(defaults, DefaultsSecondGpu);
    const bool hasSecondGpu = validate(secondGpu).has_value();

    Packet table{};
    encodeTableEntry(table, TableCpu, TableCpuSlopes, cpu);
    encodeTableEntry(table, TableGpu, TableGpuSlopes, gpu);
    if (hasSecondGpu)
        encodeTableEntry(table, TableSecondGpu, TableSecondGpuSlopes, secondGpu);
    send(*m_transport, Command::FanTable, std::span<const std::uint8_t>(table));

    const auto customMode = static_cast<std::uint8_t>(FanMode::Custom);
    send(*m_transport, Command::Control, control(ControlCode::FanMode, customMode));

    Packet page = store::readPage(*m_transport, store::FanPage);
    page[StoreVersion] = StoreFormatVersion;
    page[StoreVersion + 1] = 0;
    page[StoreVersion + 2] = 0;
    page[StoreVersion + 3] = 0;
    page[StoreInitialMode] = defaults[DefaultsInitialMode];
    page[StoreMode] = customMode;
    page[StoreFanCount] = defaults[DefaultsFanCount];
    encodeStoreRecord(page, StoreCpu, cpu, factoryCpu);
    encodeStoreRecord(page, StoreGpu, gpu, factoryGpu);
    if (hasSecondGpu)
        encodeStoreRecord(page, StoreSecondGpu, secondGpu, secondGpu);
    m_transport->writeSettings(store::FanPage, 0, page);

    return {};
}

void FanController::startDustCleaning()
{
    send(*m_transport, Command::Control, control(ControlCode::DustCleaning, 1));
}

} // namespace clevo
