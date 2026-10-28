#include "audiovcs/hash.h"

#include <openssl/evp.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace audiovcs {
namespace hash {

struct CtxDeleter {
    void operator()(EVP_MD_CTX* p) { EVP_MD_CTX_free(p); }
};

std::string file(const std::string& path)
{
    std::unique_ptr<EVP_MD_CTX, CtxDeleter> ctx(EVP_MD_CTX_new());
    if (!ctx) return "";

    if (!EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
        return "";

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "hash: cannot open " << path << std::endl;
        return "";
    }

    char buf[8192];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        if (!EVP_DigestUpdate(ctx.get(), buf, f.gcount()))
            return "";
        if (f.eof()) break;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (!EVP_DigestFinal_ex(ctx.get(), digest, &len))
        return "";

    std::ostringstream ss;
    for (unsigned int i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(digest[i]);

    return ss.str();
}

} // namespace hash
} // namespace audiovcs
