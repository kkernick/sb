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
  std::set<std::string> directories = {};

  inline std::string cache_name(const std::string& library) {
    std::string name = library;
    std::replace(name.begin(), name.end(), '/', '.');
    name = strip(name, "*");
    auto cache = std::filesystem::path(data) / "sb" / "cache" / (name + ".lib.cache");
    return cache;
  }


// Parse LDD
  std::set<std::string> parse_ldd(const std::string& library, const std::string& directory) {
    std::set<std::string> libraries;

    if (!std::filesystem::exists(library)) return libraries;

    std::string cache = cache_name(library);
    if (std::filesystem::exists(cache)) return unique_split(read_file(cache), ' ');

    // If this is a library, add it.
    if (std::filesystem::exists(library) && library.contains("/lib/") && directory == "") {
      libraries.emplace(library);
    }

    // LDD it!
    auto output = exec({"ldd", library});

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
      else log({"Failed to write cache file:", cache});
    }

    return libraries;
  }


  // Get all dependencies for a library/binary.
  std::set<std::string> get(const std::string& library, std::string directory) {
    std::set<std::string> libraries;
    std::string sub_dir = directory;
    std::vector<std::future<std::set<std::string>>> futures;

    // Our cached definitions
    std::string cache = cache_name(library);

    // If the cache exists, and we don't need to update, use it.
    if (std::filesystem::exists(cache) && arg::at("update").under("cache"))
      return unique_split(read_file(cache), ' ');

    // Resolve wildcards
    if (library.contains("*"))
      libraries = wildcard(library, "/usr/lib", {"-maxdepth", "1", "-type", "f,l", "-executable"});

    // Find all shared libraries in dir.
    else if (std::filesystem::is_directory(library)) {
      libraries = unique_split(exec({"find", library, "-type", "f,l", "-executable",}), '\n');
      sub_dir = library;
    }

    // Just use the direct dependencies of the library
    else libraries = {library};

    futures.reserve(libraries.size());
    for (const auto& lib : libraries) {

      // We use the cache as a means of only computing LDD once.
      // Therefore, we don't want to just say, "if we're updating
      // the cache, don't use an existing one," since that will
      // significantly slow down the processing as each
      // invocation to parse_ldd will require a full parsing.
      // Instead, we just delete any existing file, the first
      // invocation will generate a new file as expected,
      // and subsequent runs will just use that.
      if (arg::get("update") == "cache") {
        cache = cache_name(lib);
        if (std::filesystem::exists(cache)) std::filesystem::remove(cache);
      }
      futures.emplace_back(pool.submit_task([lib, sub_dir]{return parse_ldd(lib, sub_dir);}));
    }

    for (auto& future : futures)
      libraries.merge(future.get());

    if (!libraries.empty()) {
      auto file = std::ofstream(cache);
      if (file.is_open()) {
        file << join(libraries, ' ');
        file.close();
      }
      else log({"Failed to write cache file:", cache});
    }
    return libraries;
  }


  // Batch library lookup
  std::set<std::string> getl(const std::set<std::string>& libraries, std::string directory) {
    std::set<std::string> ret;
    for (const auto& lib : libraries) ret.merge(get(lib, directory));
    return ret;
  }


  // Setup the SOF
  void setup(const std::set<std::string>& libraries, const std::string& application) {
    const auto share_dir = std::filesystem::path(arg::get("sof")) / "shared";
    std::filesystem::create_directory(share_dir);

    const auto app_dir = std::filesystem::path(arg::get("sof")) / application / "lib";
    if (std::filesystem::is_directory(app_dir) && arg::at("update").under("libraries")) return;
    std::filesystem::create_directories(app_dir);

    auto vector = std::vector<std::string>(); vector.reserve(libraries.size());
    vector.insert(vector.begin(), libraries.begin(), libraries.end());

    // Write Lambda. We batch writes to tremendously speed up initial SOF generation.
    auto write_library = [&share_dir, &app_dir, &vector](const size_t& x) {
      const std::filesystem::path lib = vector[x];

      // We only write normalized library files, directories are mounted
      if (!lib.string().starts_with("/usr/lib/") || std::filesystem::is_directory(lib)) return;
      for (const auto& dir : directories) {
        if (lib.string().starts_with(dir))
        return;
      }

      const auto base = vector[x].substr(vector[x].find("/lib/") + 5);
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
            std::filesystem::copy_file(lib.parent_path() / resolved, shared_resolved);
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
    auto futures = pool.submit_loop(0, vector.size(), write_library);
    futures.wait();
  }

  void symlink(std::vector<std::string>& command, const std::string& application) {

    // Mount the SOF and link it to the other locations.
    extend(command, {
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });
  }

  void resolve(const std::set<std::string>& required, const std::string& program, const std::string& lib_cache, const std::string& hash) {
    log({"Resolving SOF"});

    // Generate the list of invalid entries. Because
    // we only read to the set, there is no risk in sharing it between
    // the threads, so no mutex required.
    std::set<std::string> exclusions = {};
    for (const auto& [lib, mod] : arg::modlist("libraries")) {
      if (mod == "x") {
        if (lib.contains("*")) exclusions.merge(shared::wildcard(lib, "/usr/lib", {"-maxdepth", "1", "-mindepth", "1", "-type", "f,l", "-executable"}));
        else if (std::filesystem::is_directory(lib) && libraries::directories.contains(lib)) libraries::directories.erase(lib);
        else exclusions.emplace(lib);
      }
    }

    std::set<std::string> trimmed = {};
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
