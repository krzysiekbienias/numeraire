#pragma once

#include <string>
#include <string_view>

namespace numeraire::utils {

/// Leading/trailing ASCII whitespace removed; view into `text` or a substring thereof.
[[nodiscard]] std::string_view TrimAscii(std::string_view text) noexcept;

/// Returns a new string with leading/trailing ASCII whitespace removed.
[[nodiscard]] std::string TrimCopy(std::string_view text);

/// Returns a lowercased copy (per-byte `std::tolower` on unsigned char).
[[nodiscard]] std::string ToLowerAscii(std::string_view text);

}  // namespace numeraire::utils
