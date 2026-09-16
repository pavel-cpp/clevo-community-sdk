#pragma once

#include <expected>
#include <string>

namespace clevo {

enum class Errc {
    DriverLibraryNotFound,
    DriverExportMissing,
    InvalidArgument,
};

struct Error {
    Errc code;
    std::string message;
};

template <typename T = void>
using Result = std::expected<T, Error>;

using Status = Result<void>;

[[nodiscard]] inline std::unexpected<Error> makeError(Errc code, std::string message)
{
    return std::unexpected<Error>(Error{code, std::move(message)});
}

} // namespace clevo
