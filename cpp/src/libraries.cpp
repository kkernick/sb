#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <filesystem>

#include "shared.hpp"
#include "libraries.hpp"
#include "arguments.hpp"

using namespace shared;

namespace libraries {

  // Directories, so we can mount them after discovering dependencies.
  lib_t directories = {};

  inline std::filesystem::path cache_name(const std::string_view& library) {
    auto name = std::string(library);
    std::replace(name.begin(), name.end(), '/', '.');
    name = strip(name, "*");
    auto cache = std::filesystem::path(data) / "sb" / "cache" / std::string(name + ".lib.cache");
    return cache;
  }


  lib_t parse_ldd(const std::string_view& library, const std::string_view& directory) {
    lib_t libraries;

    if (!std::filesystem::exists(library)) return libraries;
    if (read_file<std::string>(library, head<4>) != "\0x7fELF")
      return libraries;

    auto cache = cache_name(library);
    if (std::filesystem::exists(cache)) return read_file<set>(cache, setorize);
    std::filesystem::create_directories(cache.parent_path());

    // If this is a library, add it.
    if (std::filesystem::exists(library) && library.contains("/lib/") && directory == "") {
      libraries.emplace(library);
    }


    // LDD it!
    auto output = exec<std::string>({"ldd", library}, dump, STDOUT);

    // Parse it in a single pass, rather than splitting.
    for (size_t x = output.find('/'); x != std::string::npos; x = output.find('/', x + 1)) {
      auto end = output.find(' ', x);
      if (end < output.size()) {
        auto shared_lib = output.substr(x, end - x);

        // Normalize.
        shared_lib = std::regex_replace(shared_lib, std::regex("lib64"), "lib");
        if (!shared_lib.starts_with("/usr/lib/")) {
          if (shared_lib.starts_with("/lib/")) shared_lib.insert(0, "/usr");
          else if (shared_lib.starts_with("/")) shared_lib.insert(0, "/usr/lib");
        }

        // Avoid resolving libraries in the directory itself.
        if (!directory.empty()) {
          if (shared_lib.starts_with(directory)) continue;
        }

        if (shared_lib.contains("..")) shared_lib = "/usr/lib/" + shared_lib.substr(shared_lib.rfind('/') + 1);

        // Ensure it's actually here.
        if (std::filesystem::exists(shared_lib)) libraries.emplace(shared_lib);
      }
    }

    if (!libraries.empty()) {
      auto file = std::ofstream(cache);
      if (file.is_open()) {
        file << join(libraries, ' ');
        file.close();
      }
      else log({"Failed to write cache file:", cache.string()});
    }

    return libraries;
  }


  // Get all dependencies for a library/binary.
  void get(lib_t& libraries, const std::string_view& library, const std::string& directory) {
    std::string sub_dir = directory;
    lib_t local = {};

    // Our cached definitions
    auto cache = cache_name(std::string(library));

    // If the cache exists, and we don't need to update, use it.
    if (std::filesystem::exists(cache) && arg::at("update").under("cache")) {
      libraries.merge(read_file<set>(cache, setorize));
      return;
    }

    std::filesystem::create_directories(cache.parent_path());

    // Resolve wildcards
    if (library.contains("*"))
     local.merge(wildcard(library, "/usr/lib", {"-maxdepth", "1", "-type", "f,l", "-executable"}));

    // Find all shared libraries in dir.
    else if (std::filesystem::is_directory(library)) {
      local.merge(exec<lib_t>({"find", library, "-type", "f,l", "-executable"}, fd_splitter<lib_t, '\n'>, STDOUT));
      sub_dir = library;
    }

    // Just use the direct dependencies of the library
    else local.emplace(library);

    std::vector<std::future<lib_t>> futures;
    futures.reserve(local.size());
    for (const auto& lib : local) {
      if (arg::at("update").meets("cache")) {
        cache = cache_name(lib);
        if (std::filesystem::exists(cache)) std::filesystem::remove(cache);
      }
      futures.emplace_back(pool.submit_task([lib, sub_dir]{return parse_ldd(lib, sub_dir);}));
    }

    for (auto& future : futures)
      local.merge(future.get());

    if (!local.empty()) {
      auto file = std::ofstream(cache);
      if (file.is_open()) {
        file << join(local, ' ');
        file.close();
      }
      else log({"Failed to write cache file:", cache.string()});
    }
    libraries.merge(local);
  }

  // Setup the SOF
  void setup(const lib_t& libraries, const std::string_view& application) {
    const auto share_dir = std::filesystem::path(arg::get("sof")) / "shared";
    std::filesystem::create_directory(share_dir);

    const auto app_dir = std::filesystem::path(arg::get("sof")) / application / "lib";
    if (std::filesystem::is_directory(app_dir) && arg::at("update").under("libraries")) return;
    std::filesystem::create_directories(app_dir);

    vector vec; vec.reserve(libraries.size());
    vec.insert(vec.end(), std::make_move_iterator(libraries.begin()), std::make_move_iterator(libraries.end()));

    // Write Lambda. We batch writes to tremendously speed up initial SOF generation.
    auto write_library = [&share_dir, &app_dir, &vec](const size_t& x) {
      const std::string& lib = vec[x];

      // We only write normalized library files, directories are mounted
      if (!lib.starts_with("/usr/lib/") || std::filesystem::is_directory(lib)) return;
      for (const auto& dir : directories) {
        if (lib.starts_with(dir))
          return;
      }

      const auto base = lib.substr(lib.find("/lib/") + 5);
      auto shared_path = share_dir / base;
      auto local_path = app_dir / base;

      // Files are actually written to the shared directory,
      // and then hard-linked into the respective directories.
      // This shared libraries between applications.
      if (std::filesystem::exists(local_path)) return;

      if (std::filesystem::is_symlink(lib)) {
        auto resolved = std::filesystem::read_symlink(lib);
        auto local_resolved = app_dir / resolved;

        if (!std::filesystem::exists(local_resolved)) {
          auto shared_resolved = share_dir / resolved;
          if (!std::filesystem::exists(shared_resolved)) {
            std::filesystem::copy_file(lib.substr(0, lib.rfind('/')) / resolved, shared_resolved);
          }
          std::filesystem::create_hard_link(shared_resolved, local_resolved);
        }
        std::filesystem::create_symlink(resolved, local_path);
      }

      else {
        const auto dir = std::filesystem::path(base).parent_path();
        if (!std::filesystem::exists(shared_path)) {
          std::filesystem::create_directories(share_dir / dir);
          std::filesystem::copy_file(lib, shared_path);
        }
        std::filesystem::create_directories(app_dir / dir);
        std::filesystem::create_hard_link(shared_path, local_path);
      }
    };

    // Zoom.
    auto futures = pool.submit_loop(0, libraries.size(), write_library);
    futures.wait();
  }

  void symlink(vector& command) {

    // Mount the SOF and link it to the other locations.
    extend(command, {
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });
  }

  void resolve(const lib_t& required, const std::string_view& program, const std::string& lib_cache, const std::string_view& hash) {
    log({"Resolving SOF"});

    // Generate the list of invalid entries. Because
    // we only read to the set, there is no risk in sharing it between
    // the threads, so no mutex required.
    set exclusions = {};
    for (const auto& [lib, mod] : arg::modlist("libraries")) {
      if (mod == "x") {
        if (lib.contains("*"))
          exclusions.merge(shared::wildcard(lib, "/usr/lib", {"-maxdepth", "1", "-mindepth", "1", "-type", "f,l", "-executable"}));
        else if (std::filesystem::is_directory(lib) && directories.contains(lib))
          directories.erase(lib);
        else exclusions.emplace(lib);
      }
    }

    lib_t trimmed = {};
    for (const auto& lib : required) {
      if (!exclusions.contains(lib)) trimmed.emplace(lib);
    }

    libraries::setup(trimmed, program);
    auto lib_out = std::ofstream(lib_cache);
    lib_out << hash << '\n';
    for (const auto& arg : trimmed) {
      lib_out << arg << ' ';
    }
    lib_out.close();
  }
}
