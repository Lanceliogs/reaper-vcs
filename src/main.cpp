#include <iostream>

#include "hash.h"
#include "wvra.h"

int main(int argc, char** argv)
{
    std::string filename(argv[1]);

    std::cout << filename << ":" << std::endl;
    std::cout << "SHA256: " << hash::hashfile(filename) << std::endl;
    std::cout << std::endl;

    bool success = wvra::analyse(filename);
    std::cout << "Analyse returned: " << (success ? "SUCCESS" : "FAILURE") << std::endl;

    return 0;
}