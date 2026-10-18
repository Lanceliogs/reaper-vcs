#include <iostream>

#include "utils.h"

int main(int argc, char** argv)
{
    std::string filename(argv[1]);

    std::cout << filename << ":" << std::endl;
    std::cout << "SHA256: " << utils::hashfile(filename) << std::endl;

    return 0;
}