#include "audiovcs/filter.h"
#include "audiovcs/store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace fs = std::filesystem;

static void usage()
{
    std::cerr
        << "audiovcs - lossless audio version control\n"
        << "\n"
        << "Commands:\n"
        << "  install               Set up audiovcs in the current git repo\n"
        << "  clean  -- <file>      Git clean filter  (internal)\n"
        << "  smudge -- <file>      Git smudge filter (internal)\n"
        << "  snapshot <message>    Save a snapshot (git add + commit)\n"
        << "  history               List snapshots\n"
        << "  restore <ref>         Restore a snapshot\n"
        << "  status                Show audio file status\n";
}

static int cmd_install()
{
    fs::path store = fs::current_path() / audiovcs::store::STORE_DIR;
    fs::path objects = store / "objects";

    if (fs::exists(store)) {
        std::cout << "Already initialised." << std::endl;
    } else {
        fs::create_directories(objects);
        std::cout << "Created " << store.string() << std::endl;
    }

    // Append to .gitignore
    {
        fs::path gitignore = fs::current_path() / ".gitignore";
        std::string entry = std::string(audiovcs::store::STORE_DIR) + "/";
        bool already = false;

        if (fs::exists(gitignore)) {
            std::ifstream f(gitignore);
            std::string line;
            while (std::getline(f, line)) {
                if (line == entry) { already = true; break; }
            }
        }

        if (!already) {
            std::ofstream f(gitignore, std::ios::app);
            f << "\n" << entry << "\n";
            std::cout << "Added " << entry << " to .gitignore" << std::endl;
        }
    }

    // Write .gitattributes
    {
        fs::path attrs = fs::current_path() / ".gitattributes";
        const char* patterns[] = {
            "*.wav", "*.aif", "*.aiff",
            "*.mp3", "*.ogg", "*.flac", "*.opus",
            nullptr
        };

        std::string existing;
        if (fs::exists(attrs)) {
            std::ifstream f(attrs);
            std::ostringstream ss;
            ss << f.rdbuf();
            existing = ss.str();
        }

        std::ofstream f(attrs, std::ios::app);
        for (const char** p = patterns; *p; ++p) {
            std::string rule = std::string(*p) + " filter=audiovcs -text";
            if (existing.find(rule) == std::string::npos)
                f << rule << "\n";
        }
        std::cout << "Updated .gitattributes" << std::endl;
    }

    // Configure git filters
    std::system("git config filter.audiovcs.clean \"audiovcs clean -- %f\"");
    std::system("git config filter.audiovcs.smudge \"audiovcs smudge -- %f\"");
    std::system("git config filter.audiovcs.required true");
    std::cout << "Configured git filters." << std::endl;

    return 0;
}

static int cmd_snapshot(const std::string& message)
{
    std::string cmd = "git add -A && git commit -m \"" + message + "\"";
    return std::system(cmd.c_str());
}

static int cmd_history()
{
    return std::system("git log --oneline --decorate");
}

static int cmd_restore(const std::string& ref)
{
    std::string cmd = "git checkout " + ref;
    return std::system(cmd.c_str());
}

static int cmd_status()
{
    return std::system("git status");
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Git pipes are binary — prevent \n → \r\n corruption
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    if (argc < 2) {
        usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "install") {
        return cmd_install();
    }
    else if (cmd == "clean" && argc >= 4) {
        // audiovcs clean -- <file>
        return audiovcs::filter::clean(argv[3]);
    }
    else if (cmd == "smudge" && argc >= 4) {
        // audiovcs smudge -- <file>
        return audiovcs::filter::smudge(argv[3]);
    }
    else if (cmd == "snapshot" && argc >= 3) {
        return cmd_snapshot(argv[2]);
    }
    else if (cmd == "history") {
        return cmd_history();
    }
    else if (cmd == "restore" && argc >= 3) {
        return cmd_restore(argv[2]);
    }
    else if (cmd == "status") {
        return cmd_status();
    }
    else {
        usage();
        return 1;
    }
}
