#include "audiovcs/pointer.h"

#include <cstring>
#include <sstream>

namespace audiovcs {
namespace pointer {

static constexpr const char* PREFIX = "version audiovcs/";

bool is_pointer(const void* data, size_t len)
{
    if (len < 20) return false;
    return std::memcmp(data, PREFIX, std::strlen(PREFIX)) == 0;
}

bool read(std::istream& in, Pointer& ptr)
{
    std::string line;

    // Line 1: "version audiovcs/1"
    if (!std::getline(in, line)) return false;
    if (line != VERSION_LINE) return false;

    // Line 2: "oid sha256:<hex>"
    if (!std::getline(in, line)) return false;
    const std::string oid_prefix = "oid sha256:";
    if (line.substr(0, oid_prefix.size()) != oid_prefix) return false;
    ptr.hash = line.substr(oid_prefix.size());

    // Line 3: "size <n>"
    if (!std::getline(in, line)) return false;
    const std::string size_prefix = "size ";
    if (line.substr(0, size_prefix.size()) != size_prefix) return false;
    ptr.size = std::stoull(line.substr(size_prefix.size()));

    return true;
}

bool write(std::ostream& out, const Pointer& ptr)
{
    out << VERSION_LINE << '\n'
        << "oid sha256:" << ptr.hash << '\n'
        << "size " << ptr.size << '\n';
    return out.good();
}

} // namespace pointer
} // namespace audiovcs
