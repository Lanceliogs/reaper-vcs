#include <iostream>

#include <wavpack.h>

#include "third-party/json.hpp"
using json = nlohmann::json;

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::string hello = R"({"msg": "Hello, World!"})";
    json a = json::parse(hello);
    std::cout << std::string(a["msg"]) << std::endl;

    return 0;
}