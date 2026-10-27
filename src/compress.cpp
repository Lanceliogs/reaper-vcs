#include <iostream>

#include "hash.h"
#include "wvra.h"

int main(int argc, char** argv)
{
    std::string srcPath(argv[1]);
    std::string dstPath(argv[2]);

    std::cout << srcPath << ":" << std::endl;
    std::cout << "SHA256: " << hash::hashfile(srcPath) << std::endl;
    std::cout << std::endl;

    bool success = wvra::compress(srcPath, dstPath);
    std::cout << "Compression returned: " << (success ? "SUCCESS" : "FAILURE") << std::endl;

    return 0;
}