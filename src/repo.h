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
    Repository() = default;
    ~Repository() = default;

    int init();
    int load();
    int save() const;

    int compress() const;
    int restore(const std::string& commit) const;

private:
    std::string m_root;
    std::vector<MediaFileEntry> m_files;
};

}