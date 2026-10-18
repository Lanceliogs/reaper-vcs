#include <iostream>

#include "hash.h"

int main(int argc, char** argv)
{
    std::string filename(argv[1]);

    std::cout << filename << ":" << std::endl;
    std::cout << "SHA256: " << hash::hashfile(filename) << std::endl;

    return 0;
}