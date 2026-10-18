#pragma once

#include <string>
#include <vector>
#include <fstream>

#define RVCS_FOLDER ".reaper-vcs" 

namespace rvcs {

struct MediaFileEntry
{
    std::string filename;
    uint64_t hash;
};

class Repository
{
public:
    Repository();
    ~Repository() = default;

    int init();
    int load();
    int save() const;

    int add(const std::string& path) const;
    int restore(const std::string path) const;

private:
    std::vector<MediaFileEntry> m_files;
};

}