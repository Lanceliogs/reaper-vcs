#include "hash.h"

#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <memory>
#include <iomanip>
#include <iostream>

struct OpenSSLFree {
    void operator()(void* ptr) {
        EVP_MD_CTX_free((EVP_MD_CTX*)ptr);
    }
};

template <typename T>
using OpenSSLPointer = std::unique_ptr<T, OpenSSLFree>;

std::string hash::hashfile(const std::string& path)
{
    OpenSSLPointer<EVP_MD_CTX> ctx(EVP_MD_CTX_new());
    if(ctx.get() == nullptr) {
        std::cout << "Could not init ctx" << std::endl;
        return "";
    }

    std::ifstream f(path, std::fstream::binary);
    if (f.bad()) {
        std::cout << "Could not open " << path << std::endl;
        return "";
    }

    if(!EVP_DigestInit_ex(ctx.get(), EVP_sha256(), NULL)) {
        std::cout << "Could not init digest for sha256" << std::endl;
        return "";
    }

    while (true)
    {
        char chunk[1024] = {0};
        f.read(chunk, sizeof(chunk));
        if(!EVP_DigestUpdate(ctx.get(), chunk, sizeof(chunk)))
            return "";
        if (f.eof())
            break;
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if(!EVP_DigestFinal_ex(ctx.get(), hash, &length))
        return "";

    std::stringstream ss;
    for(unsigned int i = 0; i < length; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    return ss.str();
}
