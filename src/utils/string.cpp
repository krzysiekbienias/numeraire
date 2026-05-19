#include <numeraire/utils/string.hpp>

#include <algorithm>
#include <cctype>

namespace numeraire::utils {

std::string_view TrimAscii(const std::string_view text) noexcept {
    std::string_view s = text;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

std::string TrimCopy(const std::string_view text) { return std::string(TrimAscii(text)); }

std::string ToLowerAscii(const std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace numeraire::utils
