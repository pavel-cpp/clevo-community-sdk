#include "clevo/Transport.hpp"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace clevo {
namespace {

// Exported by InsydeDCHU.dll. x64 has a single calling convention, so these
// match the vendor's __cdecl/__stdcall declarations alike.
using GetIntegerFn = int (*)(int command, int *data);
using GetBufferFn = int (*)(int command, std::uint8_t *buffer);
using SetDataFn = int (*)(int command, std::uint8_t *buffer, int length);
using SetDataExFn = int (*)(int command, std::uint8_t *buffer, int length, std::uint8_t *output);
using SettingsFn = int (*)(int page, int offset, int length, std::uint8_t *buffer);

std::string lastErrorText(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || !buffer)
        return "Windows error " + std::to_string(code);

    const int size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), text.data(), size, nullptr, nullptr);
    LocalFree(buffer);

    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

class InsydeTransport final : public DchuTransport {
public:
    InsydeTransport(HMODULE module, GetIntegerFn getInteger, GetBufferFn getBuffer, SetDataFn setData,
                    SetDataExFn setDataEx, SettingsFn readSettings, SettingsFn writeSettings)
        : m_module(module)
        , m_getInteger(getInteger)
        , m_getBuffer(getBuffer)
        , m_setData(setData)
        , m_setDataEx(setDataEx)
        , m_readSettings(readSettings)
        , m_writeSettings(writeSettings)
    {
    }

    ~InsydeTransport() override { FreeLibrary(m_module); }

    InsydeTransport(const InsydeTransport &) = delete;
    InsydeTransport &operator=(const InsydeTransport &) = delete;

    std::uint32_t queryWord(std::int32_t command) override
    {
        std::scoped_lock lock(m_mutex);
        int data = 0;
        m_getInteger(command, &data);
        return static_cast<std::uint32_t>(data);
    }

    Packet queryPacket(std::int32_t command) override
    {
        std::scoped_lock lock(m_mutex);
        Packet packet{};
        m_getBuffer(command, packet.data());
        return packet;
    }

    void send(std::int32_t command, std::span<const std::uint8_t> payload) override
    {
        // The driver takes a mutable pointer, so never hand it caller memory.
        std::vector<std::uint8_t> buffer(payload.begin(), payload.end());
        std::scoped_lock lock(m_mutex);
        m_setData(command, buffer.data(), static_cast<int>(buffer.size()));
    }

    Packet exchange(std::int32_t command, const Packet &request) override
    {
        Packet input = request;
        Packet output{};
        std::scoped_lock lock(m_mutex);
        m_setDataEx(command, input.data(), static_cast<int>(input.size()), output.data());
        return output;
    }

    void readSettings(std::int32_t page, std::int32_t offset, std::span<std::uint8_t> out) override
    {
        if (out.empty())
            return;
        std::scoped_lock lock(m_mutex);
        m_readSettings(page, offset, static_cast<int>(out.size()), out.data());
    }

    void writeSettings(std::int32_t page, std::int32_t offset, std::span<const std::uint8_t> data) override
    {
        if (data.empty())
            return;
        std::vector<std::uint8_t> buffer(data.begin(), data.end());
        std::scoped_lock lock(m_mutex);
        m_writeSettings(page, offset, static_cast<int>(buffer.size()), buffer.data());
    }

private:
    HMODULE m_module;
    GetIntegerFn m_getInteger;
    GetBufferFn m_getBuffer;
    SetDataFn m_setData;
    SetDataExFn m_setDataEx;
    SettingsFn m_readSettings;
    SettingsFn m_writeSettings;
    // The vendor DLL is not documented as thread-safe; effect playback and UI
    // calls may overlap, so every call is serialised.
    std::mutex m_mutex;
};

std::string toUtf8(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

template <typename Fn>
Fn resolve(HMODULE module, const char *name)
{
    return reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(module, name)));
}

} // namespace

Result<std::unique_ptr<DchuTransport>> openInsydeTransport(const std::filesystem::path &library)
{
    const bool bareName = !library.has_parent_path();
    const std::filesystem::path target = bareName ? library : std::filesystem::absolute(library);
    const DWORD searchFlags = bareName
        ? (LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)
        : (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

    const HMODULE module = LoadLibraryExW(target.c_str(), nullptr, searchFlags);
    if (!module) {
        return makeError(Errc::DriverLibraryNotFound,
                         "Cannot load " + toUtf8(target) + ": " + lastErrorText(GetLastError()));
    }

    const auto getInteger = resolve<GetIntegerFn>(module, "GetDCHU_Data_Integer");
    const auto getBuffer = resolve<GetBufferFn>(module, "GetDCHU_Data_Buffer");
    const auto setData = resolve<SetDataFn>(module, "SetDCHU_Data");
    const auto setDataEx = resolve<SetDataExFn>(module, "SetDCHU_DataEx");
    const auto readSettings = resolve<SettingsFn>(module, "ReadAppSettings");
    const auto writeSettings = resolve<SettingsFn>(module, "WriteAppSettings");

    if (!getInteger || !getBuffer || !setData || !setDataEx || !readSettings || !writeSettings) {
        FreeLibrary(module);
        return makeError(Errc::DriverExportMissing,
                         toUtf8(target) + " does not export the expected DCHU interface");
    }

    return std::make_unique<InsydeTransport>(module, getInteger, getBuffer, setData, setDataEx, readSettings,
                                             writeSettings);
}

} // namespace clevo
