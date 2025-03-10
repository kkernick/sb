#include <algorithm>
#include <fstream>
#include <regex>

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
    std::string cache = mkpath({data, "sb", "cache/"});
    cache += name;
    if (!cache.ends_with('.')) cache += '.';
    cache += "lib.cache";
    return cache;
  }


// Parse LDD
  std::set<std::string> parse_ldd(const std::string& library, const std::string& directory) {
    std::set<std::string> libraries;

    if (!exists(library)) return libraries;

    std::string cache = cache_name(library);
    if (is_file(cache)) return unique_split(read_file(cache), " ");

    // If this is a library, add it.
    if (is_file(library) && library.contains("/lib/") && directory == "") {
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

        if (shared_lib.contains("..")) shared_lib = "/usr/lib/" + basename(shared_lib);


        // Ensure it's actually here.
        if (exists(shared_lib)) libraries.emplace(shared_lib);
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
    std::set<std::string> libraries, direct;
    std::string sub_dir = directory;
    std::vector<std::future<std::set<std::string>>> futures;

    // Our cached definitions
    std::string cache = cache_name(library);

    // If the cache exists, and we don't need to update, use it.
    if (is_file(cache) && arg::at("update").under("cache"))
      return unique_split(read_file(cache), " ");

    // Resolve wildcards
    if (library.contains("*"))
      direct = wildcard(library, "/usr/lib", {"-maxdepth", "1", "-type", "f,l", "-executable"});

    // Find all shared libraries in dir.
    else if (is_dir(library)) {
      direct = unique_split(exec({"find", library, "-type", "f,l", "-executable",}), "\n");
      sub_dir = library;
    }

    // Just use the direct dependencies of the library
    else direct = {library};

    futures.reserve(direct.size());
    for (const auto& lib : direct) {

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
        if (is_file(cache)) std::filesystem::remove(cache);
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
    const auto share_dir = mkpath({arg::get("sof"), "shared"});
    const auto app_dir = mkpath({arg::get("sof"), application, "lib"});

    if(is_dir(app_dir) && arg::at("update").under("libraries")) return;

    auto vector = std::vector<std::string>(); vector.reserve(libraries.size());
    vector.insert(vector.begin(), libraries.begin(), libraries.end());

    // Write Lambda. We batch writes to tremendously speed up initial SOF generation.
    auto write = [&share_dir, &app_dir, &vector](const size_t& x){
      const auto& lib = vector[x];

      // We only write normalized library files, directories are mounted
      if (!lib.starts_with("/usr/lib")) return;
      for (const auto& dir : directories) if (lib.starts_with(dir)) return;

      const auto base = lib.substr(lib.find("/lib/") + 5);
      const auto dir = dirname(base);

      // Files are actually written to the shared directory,
      // and then hard-linked into the respective directories.
      // This shared libraries between applications.
      if (is_file(app_dir + base)) return;
      if (!is_file(share_dir + base)) {
        std::filesystem::create_directories(share_dir + dir);
        std::filesystem::copy_file(lib, share_dir + base);
      }
      std::filesystem::create_directories(app_dir + dir);
      std::filesystem::create_hard_link(share_dir + base, app_dir + base);
    };

    // Zoom.
    auto futures = pool.submit_loop(0, vector.size(), write);
    futures.wait();
  }

  void symlink(std::vector<std::string>& command, const std::string& application) {
    const auto app_dir = mkpath({arg::get("sof"), application, "lib"});

    if (is_dir(app_dir))
      extend(command, {"--overlay-src", app_dir, "--tmp-overlay", "/usr/lib"});

    // Mount the SOF and link it to the other locations.
    extend(command, {
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });
  }
}
