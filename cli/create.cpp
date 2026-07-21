#include "create.hpp"
#include "util.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cstdint>

extern const unsigned char* flux_template_data();
extern size_t flux_template_size();

namespace
{


    bool is_valid_project_name(const std::string &name)
    {
        if (name.empty())
            return false;
        for (char c : name)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
                return false;
        }
        return std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_';
    }

    std::string slugify(const std::string &name)
    {
        std::string out;
        for (char c : name)
            out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '-';
        return out;
    }

    // Reads the FTPL archive embedded in this binary (see
    // cli/tools/embed_template.cpp for the writer / format) and writes its
    // entries out under `dest`. Fully offline.
    bool extract_embedded_template(const fs::path &dest)
    {
        const unsigned char *p = flux_template_data();
        const unsigned char *end = p + flux_template_size();

        if (flux_template_size() < 8 || std::memcmp(p, "FTPL", 4) != 0)
        {
            std::fprintf(stderr, "flux: embedded template archive is missing or corrupt.\n");
            return false;
        }
        p += 4;
        uint32_t count;
        std::memcpy(&count, p, 4);
        p += 4;

        for (uint32_t i = 0; i < count; ++i)
        {
            if (p + 2 > end) return false;
            uint16_t path_len;
            std::memcpy(&path_len, p, 2);
            p += 2;

            if (p + path_len > end) return false;
            std::string rel(reinterpret_cast<const char *>(p), path_len);
            p += path_len;

            if (p + 1 > end) return false;
            char type = static_cast<char>(*p);
            p += 1;

            if (p + 8 > end) return false;
            uint64_t len;
            std::memcpy(&len, p, 8);
            p += 8;

            if (p + len > end) return false;

            fs::path out_path = dest / rel;
            if (type == 1)
            {
                fs::create_directories(out_path);
            }
            else
            {
                fs::create_directories(out_path.parent_path());
                std::ofstream f(out_path, std::ios::binary);
                if (!f)
                {
                    std::fprintf(stderr, "flux: failed to write %s\n", out_path.string().c_str());
                    return false;
                }
                if (len)
                    f.write(reinterpret_cast<const char *>(p), static_cast<std::streamsize>(len));
            }
            p += len;
        }
        return true;
    }


    bool patch_app_config(const fs::path &root, const std::string &project_name)
    {
        fs::path cfg_path = root / "config" / "AppConfig.json";
        auto cfg = load_json_file(cfg_path);
        if (!cfg)
            return false;

        (*cfg)["name"] = project_name;
        (*cfg)["bundleId"] = "com.example." + slugify(project_name);
        (*cfg)["version"] = std::string("1.0.0");
        (*cfg)["build"] = 1;

        return write_json_file(cfg_path, *cfg);
    }

    bool reset_manifest(const fs::path &root)
    {
        json fresh;
        fresh["packages"] = json::object();
        return write_json_file(root / "flux.deps.json", fresh);
    }

    void write_readme(const fs::path &root, const std::string &project_name)
    {
        std::ofstream out(root / "README.md");
        if (out)
        {
            out << "# " << project_name << "\n\n"
                << "A FluxUI project.\n\n"
                << "## Setup and the flux CLI\n\n"
                << "Download `flux` from https://github.com/IAmTheFool/flux/releases, "
                   "put it somewhere on your PATH, then run `flux run windows` "
                   "(or `linux`/`macos`) from this directory.\n\n"
                << "Full docs: https://github.com/IAmTheFool/flux/blob/main/INSTALL.md\n";
        }
    }

} // namespace

int cmd_create(const std::string &project_name)
{
    if (!is_valid_project_name(project_name))
    {
        std::fprintf(stderr,
                     "flux: invalid project name '%s' (letters, digits, '-', '_' only, must not start with a digit).\n",
                     project_name.c_str());
        return 1;
    }

    fs::path dest = fs::current_path() / project_name;
    if (fs::exists(dest))
    {
        std::fprintf(stderr, "flux: '%s' already exists in the current directory.\n", project_name.c_str());
        return 1;
    }

    std::printf("Creating '%s'...\n", project_name.c_str());
    if (!extract_embedded_template(dest))
    {
        std::fprintf(stderr, "ERROR: failed to extract embedded template.\n");
        std::error_code ec;
        fs::remove_all(dest, ec);
        return 1;
    }

    write_readme(dest, project_name);


    if (!patch_app_config(dest, project_name))
    {
        std::fprintf(stderr, "ERROR: cloned successfully but failed to patch config/AppConfig.json\n");
        return 1;
    }
    if (!reset_manifest(dest))
    {
        std::fprintf(stderr, "ERROR: cloned successfully but failed to reset flux.deps.json\n");
        return 1;
    }

    if (command_exists("git"))
    {
        std::system(("git -C \"" + dest.string() + "\" init -q").c_str());
    }

    std::printf("\nCreated %s.\n", project_name.c_str());
    std::printf("  cd %s\n", project_name.c_str());
    std::printf("  flux run %s\n",
#if defined(_WIN32)
                "windows"
#elif defined(__APPLE__)
                "macos"
#else
                "linux"
#endif
    );
    return 0;
}