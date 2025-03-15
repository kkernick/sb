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

  // Get the name for a cache.
  inline std::filesystem::path cache_name(std::string library, const std::string& function) {
    std::replace(library.begin(), library.end(), '/', '.');
    library = strip<char>(library, '*');
    auto cache = std::filesystem::path(data) / "sb" / "cache" / std::string(library + '.' + function + ".lib.cache");
    return cache;
  }

  void parse_ldd(lib_t& required, const std::string& library, const std::string_view& directory) {
    lib_t libraries;

    if (!std::filesystem::exists(library)) return;

    auto cache = cache_name(std::string(library), "ldd");
    if (std::filesystem::exists(cache) && !std::filesystem::is_empty(cache))
      libraries = read_file<set>(cache, setorize);
    else {
      if (read_file<std::string>(library, head<5>) != "\177ELF\2") return;
      std::filesystem::create_directories(cache.parent_path());

      // If this is a library, add it.
      if (std::filesystem::exists(library) && library.starts_with("/usr/lib/") && directory == "") {
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
          if (std::filesystem::exists(shared_lib) && shared_lib.starts_with("/usr/lib")) libraries.emplace(shared_lib);
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
    }
    required.merge(libraries);
  }


  // Get all dependencies for a library/binary.
  void get(lib_t& libraries, const std::string_view& library, std::string directory) {
    lib_t local = {};

    // Our cached definitions
    auto cache = cache_name(std::string(library), "get");

    // If the cache exists, and we don't need to update, use it.
    if (std::filesystem::exists(cache) && !std::filesystem::is_empty(cache)) {
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
      directory = library;
    }

    // Just use the direct dependencies of the library
    else if (library.starts_with("/usr/lib")) local.emplace(library);
    
    // Binaries just exclude itself.
    else parse_ldd(libraries, library.data(), "");

    batch(parse_ldd, local, local, directory);

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
  void setup(const vector& libraries, const std::string_view& application, const std::filesystem::path& app_sof) {
    const auto share_dir = std::filesystem::path(arg::get("sof")) / "shared";
    std::filesystem::create_directories(share_dir);
    if (std::filesystem::is_directory(app_sof) && arg::at("update") < "libraries" && !std::filesystem::is_empty(app_sof)) return;
    std::filesystem::create_directories(app_sof);

    // Write Lambda. We batch writes to tremendously speed up initial SOF generation.
    auto write_library = [&share_dir, &app_sof, &libraries](const uint_fast32_t x) {
      const std::string& lib = libraries[x];

      // We only write normalized library files, directories are mounted
      if (!lib.starts_with("/usr/lib/") || std::filesystem::is_directory(lib)) return;

      const auto base = lib.substr(lib.find("/lib/") + 5);
      auto shared_path = share_dir / base;
      auto local_path = app_sof / base;

      // Files are actually written to the shared directory,
      // and then hard-linked into the respective directories.
      // This shared libraries between applications.
      if (std::filesystem::exists(local_path)) return;

      if (std::filesystem::is_symlink(lib)) {
        auto resolved = std::filesystem::read_symlink(lib);
        auto local_resolved = app_sof / resolved;

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
        std::filesystem::create_directories(app_sof / dir);
        std::filesystem::create_hard_link(shared_path, local_path);
      }
    };

    auto lock_file = share_dir / "sb.lock";
    while (std::filesystem::exists(lock_file)) {
      log({"Another instance of the application is writing to the SOF. Waiting..."});
      auto wd = inotify_add_watch(inotify, lock_file.c_str(), IN_DELETE_SELF);
      inotify_wait(wd);
    }
    std::ofstream lock(lock_file);

    // Zoom.
    //for (size_t x = 0; x < vec.size(); ++x) write_library(x);
    auto futures = pool.submit_loop(0, libraries.size(), write_library);
    futures.wait();
    std::filesystem::remove(lock_file);
  }
  void setup(const set& libraries, const std::string_view& application, const std::filesystem::path& app_sof) {
    vector vec; vec.reserve(libraries.size());
    vec.insert(vec.end(), std::make_move_iterator(libraries.begin()), std::make_move_iterator(libraries.end()));
    setup(vec, application, app_sof);
  }

  // Symlink libraries
  void symlink(vector& command) {
    extend(command, {
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });
  }
}
