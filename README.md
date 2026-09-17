# ClevoCommunitySDK

A C++23 library for controlling Clevo laptops through the Insyde DCHU driver:
power profiles, keyboard backlight, fans and firmware capabilities.

> **Experimental.** The protocol was reverse-engineered from Clevo's Control
> Center. It talks to your laptop's embedded controller, so use it at your own risk.

## Requirements

- Windows 10/11 x64 on a Clevo-based laptop with the Insyde DCHU driver installed
- CMake 3.21+
- A C++23 compiler. It is developed and tested with MinGW-w64 GCC 13.1.

`InsydeDCHU.dll` ships in [`driver/`](driver) and needs no Visual C++ runtime.

## Building

The library builds as either a static library or a DLL, selected by
`CLEVO_SDK_BUILD_SHARED`. When that option is not set, it follows `BUILD_SHARED_LIBS`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCLEVO_SDK_BUILD_SHARED=OFF
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix <install-dir>
```

The tests use a fake driver, so they run on any machine.

| Option                     | Default                           | Description                                   |
|----------------------------|-----------------------------------|-----------------------------------------------|
| `CLEVO_SDK_BUILD_SHARED`   | `BUILD_SHARED_LIBS`               | Build a DLL instead of a static library       |
| `CLEVO_SDK_BUILD_TESTS`    | `ON` standalone, `OFF` as subdir  | Build the unit tests                          |
| `CLEVO_SDK_BUILD_EXAMPLES` | `ON` standalone, `OFF` as subdir  | Build `clevoctl`                              |
| `CLEVO_SDK_INSTALL`        | `ON` standalone, `OFF` as subdir  | Generate install rules and the CMake package  |

If you install both flavours, use separate prefixes. The CMake package files
share one location and would overwrite each other.

## Using it in a CMake project

Link the target, then call `clevo_sdk_deploy_runtime`. It copies
`InsydeDCHU.dll`, plus the SDK DLL for shared builds, next to your executable
after every build. With `INSTALL_DESTINATION`, it also installs them.

As an installed package:

```cmake
find_package(ClevoCommunitySDK 0.1 REQUIRED)
target_link_libraries(MyApp PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
clevo_sdk_deploy_runtime(MyApp INSTALL_DESTINATION .)
```

As a subdirectory:

```cmake
set(CLEVO_SDK_BUILD_SHARED OFF)   # or ON
add_subdirectory(ClevoCommunitySDK)
target_link_libraries(MyApp PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
clevo_sdk_deploy_runtime(MyApp INSTALL_DESTINATION .)
```

Build your application with the same compiler as the SDK. Its C++ interface
is not binary-compatible across toolchains, for example MinGW and MSVC.

## Example

```cpp
#include <clevo/Clevo.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    auto device = clevo::Device::open();
    if (!device) {
        std::cerr << device.error().message << '\n';
        return 1;
    }

    device->power().setProfile(clevo::PowerProfile::Quiet);

    auto keyboard = device->keyboard();
    keyboard.setColor({0x4C, 0xC2, 0xFF});
    keyboard.setBrightness(200);

    auto fans = device->fans();
    const auto telemetry = fans.telemetry();
    std::cout << "CPU fan: " << telemetry.cpu.rpm << " RPM, "
              << int(telemetry.cpu.temperatureCelsius) << " C\n";

    auto cpu = fans.defaultCurve(clevo::FanId::Cpu);
    cpu.lower = {60, 50};
    cpu.upper = {80, 90};
    if (auto status = fans.applyCustomCurves(cpu, fans.defaultCurve(clevo::FanId::Gpu)); !status)
        std::cerr << status.error().message << '\n';

    clevo::EffectPlayer player(device->transport());
    player.play(clevo::breathingEffect({255, 0, 0}, std::chrono::seconds(3)));
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
```

## API overview

All public headers live under `include/clevo/`. `Clevo.hpp` includes them all.

| Header                | Contents                                                                 |
|-----------------------|--------------------------------------------------------------------------|
| `Device.hpp`          | `Device`, the entry point. It opens the driver and hands out the controllers |
| `Power.hpp`           | `PowerController`: Quiet / Power Saving / Performance / Entertainment    |
| `Keyboard.hpp`        | `KeyboardController`: on/off, color, brightness, hardware effects, boot effect, sleep timer |
| `LightingEffects.hpp` | Software effects (breathing, color cycle, colorful breathing) and `EffectPlayer` |
| `Fans.hpp`            | `FanController`: telemetry, modes, offset, custom curves, dust cleaning  |
| `Capabilities.hpp`    | `Capabilities`: what the firmware reports as supported                    |
| `System.hpp`          | `SystemController`: embedded controller version, touchpad toggle, display off |
| `Transport.hpp`       | `DchuTransport`: raw driver access for commands the SDK does not model   |
| `Error.hpp`           | `Result<T>` / `Status`, based on `std::expected`                         |

Controllers are cheap handles that share the device's driver connection. You
can copy them freely, and they may outlive the `Device`. The vendor DLL is
bound to the thread that first uses it, so the SDK runs every driver call on a
dedicated thread and the calling thread waits for the result. That makes it
safe to use the SDK from several threads, including the `EffectPlayer`'s.

## clevoctl

A small command-line tool built with the examples:

```
clevoctl status                                   read-only overview
clevoctl power <quiet|saving|performance|entertainment>
clevoctl fan <auto|max|maxq|custom>
clevoctl color <r> <g> <b>
clevoctl brightness <0-255>
```

`clevoctl status` only reads state. Run it first to check that the SDK
understands your machine.
