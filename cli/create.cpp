#include "create.hpp"
#include "util.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace
{

    constexpr const char *kTemplateRepo = "https://github.com/IAmTheFool/flux.git";

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

    std::optional<std::string> resolve_latest_tag()
    {
        // Sorted descending by version; take the first line.
        auto out = run_capture(
            "git ls-remote --tags --sort=-v:refname " + std::string(kTemplateRepo) + " 2>&1");
        if (!out || out->empty())
            return std::nullopt;

        std::string first_line = out->substr(0, out->find('\n'));
        auto pos = first_line.find("refs/tags/");
        if (pos == std::string::npos)
            return std::nullopt;
        return first_line.substr(pos + std::strlen("refs/tags/"));
    }

    int clone_template(const fs::path &dest, const std::string &ref)
    {
        std::string cmd = "git clone --branch " + ref + " --depth 1 --recurse-submodules " +
                          kTemplateRepo + " \"" + dest.string() + "\" 2>&1";
        return std::system(cmd.c_str());
    }

    // Engine-repo-only content a fresh app has no use for.
    void strip_dev_only(const fs::path &root)
    {
        for (const char *rel : {"examples", "screenshots", "docs", "cli","scripts", ".git",".github"})
        {
            std::error_code ec;
            fs::remove_all(root / rel, ec);
        }
        for (const char *rel : {"CHANGELOG.md", "INSTALL.md"})
        {
            std::error_code ec;
            fs::remove(root / rel, ec);
        }
    }


    // Keep assets/ present but empty — the cloned tree ships the engine's
    // own demo media (used by examples/), which we just deleted.
    void reset_assets(const fs::path &root)
    {
        std::error_code ec;
        fs::path assets = root / "assets";
        for (const auto &entry : fs::directory_iterator(assets, ec))
        {
            fs::remove_all(entry.path(), ec);
        }
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

int cmd_create(const std::string &project_name, const std::string &ref)
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

    if (!command_exists("git"))
    {
        std::fprintf(stderr, "ERROR: git not found in PATH. Run 'flux doctor' to check your toolchain.\n");
        return 1;
    }

    std::string resolved_ref = ref;
    if (resolved_ref.empty())
    {
        auto latest = resolve_latest_tag();
        if (!latest)
        {
            std::fprintf(stderr, "ERROR: could not resolve the latest release tag; pass --ref explicitly.\n");
            return 1;
        }
        resolved_ref = *latest;
    }

    std::printf("Creating '%s' from flux %s...\n", project_name.c_str(), resolved_ref.c_str());
    if (clone_template(dest, resolved_ref) != 0)
    {
        std::fprintf(stderr, "ERROR: failed to clone flux at ref '%s'.\n", resolved_ref.c_str());
        return 1;
    }

    strip_dev_only(dest);
    reset_assets(dest);
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

    std::system(("git -C \"" + dest.string() + "\" init -q").c_str());

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