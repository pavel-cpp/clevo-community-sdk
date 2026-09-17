#include "clevo/Transport.hpp"

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
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

// InsydeDCHU.dll talks to the firmware through COM and window messages, so
// its objects belong to the thread that first used them. Calling it from a
// second thread marshals the call back into the first one; if that thread is
// itself waiting for the driver, both block forever. Every call - including
// loading and unloading the library - therefore runs on this one thread, and
// callers simply wait for the result.
class DriverThread {
public:
    DriverThread()
        : m_thread([this](std::stop_token stopToken) { run(stopToken); })
    {
    }

    DriverThread(const DriverThread &) = delete;
    DriverThread &operator=(const DriverThread &) = delete;

    template <typename F>
    std::invoke_result_t<F> invoke(F &&function)
    {
        std::packaged_task<std::invoke_result_t<F>()> task(std::forward<F>(function));
        auto result = task.get_future();
        {
            std::scoped_lock lock(m_mutex);
            // The caller blocks on `result` below, so `task` outlives the job.
            m_jobs.emplace_back([&task] { task(); });
        }
        m_wakeUp.notify_one();
        return result.get();
    }

private:
    void run(std::stop_token stopToken)
    {
        std::unique_lock lock(m_mutex);
        while (m_wakeUp.wait(lock, stopToken, [this] { return !m_jobs.empty(); })) {
            std::function<void()> job = std::move(m_jobs.front());
            m_jobs.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }

    std::mutex m_mutex;
    std::condition_variable_any m_wakeUp;
    std::deque<std::function<void()>> m_jobs;
    // Declared last so the queue it serves is still alive while it stops.
    std::jthread m_thread;
};

struct DriverLibrary {
    HMODULE module = nullptr;
    GetIntegerFn getInteger = nullptr;
    GetBufferFn getBuffer = nullptr;
    SetDataFn setData = nullptr;
    SetDataExFn setDataEx = nullptr;
    SettingsFn readSettings = nullptr;
    SettingsFn writeSettings = nullptr;
};

Result<DriverLibrary> loadLibrary(const std::filesystem::path &target, DWORD searchFlags)
{
    DriverLibrary library;
    library.module = LoadLibraryExW(target.c_str(), nullptr, searchFlags);
    if (!library.module) {
        return makeError(Errc::DriverLibraryNotFound,
                         "Cannot load " + toUtf8(target) + ": " + lastErrorText(GetLastError()));
    }

    library.getInteger = resolve<GetIntegerFn>(library.module, "GetDCHU_Data_Integer");
    library.getBuffer = resolve<GetBufferFn>(library.module, "GetDCHU_Data_Buffer");
    library.setData = resolve<SetDataFn>(library.module, "SetDCHU_Data");
    library.setDataEx = resolve<SetDataExFn>(library.module, "SetDCHU_DataEx");
    library.readSettings = resolve<SettingsFn>(library.module, "ReadAppSettings");
    library.writeSettings = resolve<SettingsFn>(library.module, "WriteAppSettings");

    if (!library.getInteger || !library.getBuffer || !library.setData || !library.setDataEx
        || !library.readSettings || !library.writeSettings) {
        FreeLibrary(library.module);
        return makeError(Errc::DriverExportMissing,
                         toUtf8(target) + " does not export the expected DCHU interface");
    }
    return library;
}

class InsydeTransport final : public DchuTransport {
public:
    InsydeTransport(std::unique_ptr<DriverThread> driver, DriverLibrary library)
        : m_driver(std::move(driver))
        , m_library(library)
    {
    }

    ~InsydeTransport() override
    {
        m_driver->invoke([module = m_library.module] { FreeLibrary(module); });
    }

    InsydeTransport(const InsydeTransport &) = delete;
    InsydeTransport &operator=(const InsydeTransport &) = delete;

    std::uint32_t queryWord(std::int32_t command) override
    {
        return m_driver->invoke([&] {
            int data = 0;
            m_library.getInteger(command, &data);
            return static_cast<std::uint32_t>(data);
        });
    }

    Packet queryPacket(std::int32_t command) override
    {
        return m_driver->invoke([&] {
            Packet packet{};
            m_library.getBuffer(command, packet.data());
            return packet;
        });
    }

    void send(std::int32_t command, std::span<const std::uint8_t> payload) override
    {
        // The driver takes a mutable pointer, so never hand it caller memory.
        std::vector<std::uint8_t> buffer(payload.begin(), payload.end());
        m_driver->invoke([&] { m_library.setData(command, buffer.data(), static_cast<int>(buffer.size())); });
    }

    Packet exchange(std::int32_t command, const Packet &request) override
    {
        Packet input = request;
        return m_driver->invoke([&] {
            Packet output{};
            m_library.setDataEx(command, input.data(), static_cast<int>(input.size()), output.data());
            return output;
        });
    }

    void readSettings(std::int32_t page, std::int32_t offset, std::span<std::uint8_t> out) override
    {
        if (out.empty())
            return;
        m_driver->invoke([&] { m_library.readSettings(page, offset, static_cast<int>(out.size()), out.data()); });
    }

    void writeSettings(std::int32_t page, std::int32_t offset, std::span<const std::uint8_t> data) override
    {
        if (data.empty())
            return;
        std::vector<std::uint8_t> buffer(data.begin(), data.end());
        m_driver->invoke(
            [&] { m_library.writeSettings(page, offset, static_cast<int>(buffer.size()), buffer.data()); });
    }

private:
    std::unique_ptr<DriverThread> m_driver;
    DriverLibrary m_library;
};

} // namespace

Result<std::unique_ptr<DchuTransport>> openInsydeTransport(const std::filesystem::path &library)
{
    const bool bareName = !library.has_parent_path();
    const std::filesystem::path target = bareName ? library : std::filesystem::absolute(library);
    const DWORD searchFlags = bareName
        ? (LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)
        : (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

    auto driver = std::make_unique<DriverThread>();
    auto loaded = driver->invoke([&] { return loadLibrary(target, searchFlags); });
    if (!loaded)
        return std::unexpected(std::move(loaded.error()));

    return std::make_unique<InsydeTransport>(std::move(driver), *loaded);
}

} // namespace clevo
