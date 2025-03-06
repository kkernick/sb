#include <algorithm>
#include <fstream>
#include <regex>

#include "shared.hpp"
#include "libraries.hpp"
#include "arguments.hpp"

using namespace shared;

namespace libraries {


  // Already searched libraries. Reset() to prevent poisoning.
  std::set<std::string> searched = {};
  std::mutex searched_mutex;

  // Directories, so we can mount them after discovering dependencies.
  std::set<std::string> directories = {};

  // Things we don't want to include.
  std::set<std::string> excluded = {};

// Parse LDD
  std::set<std::string> parse_ldd(const std::string& library, const std::string& directory) {
    std::set<std::string> libraries;

    if (!exists(library)) return libraries;

    // This is threaded, so we mediate the search list with a mutex.
    searched_mutex.lock();
    if (searched.contains(library)) {
      searched_mutex.unlock();
      return libraries;
    }
    searched.emplace(library);
    searched_mutex.unlock();

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
    return libraries;
  }


  // Get all dependencies for a library/binary.
  std::set<std::string> get(const std::string& library, std::string directory) {
    std::set<std::string> libraries, direct;
    std::string sub_dir = directory;
    std::vector<std::future<std::set<std::string>>> futures;

    // Our cached definitions
    std::string name = library;
    std::replace(name.begin(), name.end(), '/', '.');
    name = strip(name, "*");
    std::string cache = mkpath({data, "sb", "cache/"});
    cache += name;
    if (!cache.ends_with('.')) cache += '.';
    cache += "lib.cache";

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
    for (const auto& lib : direct)
      futures.emplace_back(pool.submit_task([lib, sub_dir]{return parse_ldd(lib, sub_dir);}));
    for (auto& future : futures) libraries.merge(future.get());


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
  void setup(const std::set<std::string>& libraries, const std::string& application, std::vector<std::string>& command) {
    const auto share_dir = mkpath({arg::get("sof"), "shared"});
    const auto app_dir = mkpath({arg::get("sof"), application, "lib"});
    auto vector = std::vector<std::string>(); vector.reserve(libraries.size());
    vector.insert(vector.begin(), libraries.begin(), libraries.end());

    // Generate the list of invalid entries. Because
    // we only read to the set, there is no risk in sharing it between
    // the threads, so no mutex required.
    std::set<std::string> exclusions = {};
    for (const auto& [lib, mod] : arg::modlist("libraries")) {
      if (mod == "x") {
        if (lib.contains("*")) exclusions.merge(shared::wildcard(lib, "/usr/lib", {"-maxdepth", "1", "-mindepth", "1", "-type", "f,l", "-executable"}));
        else if (is_dir(lib)) directories.emplace(lib);
        else exclusions.emplace(lib);
      }
    }

    // Write Lambda. We batch writes to tremendously speed up initial SOF generation.
    auto write = [&share_dir, &app_dir, &vector, &exclusions](const size_t& x){
      const auto& lib = vector[x];

      // We only write normalized library files, directories are mounted
      if (!lib.starts_with("/usr/lib")) return;
      for (const auto& dir : directories) if (lib.starts_with(dir)) return;
      if (exclusions.contains(lib)) return;

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
    pool.detach_loop(0, vector.size(), write);
    pool.wait();

    // Mount the SOF and link it to the other locations.
    extend(command, {
      "--overlay-src", app_dir, "--tmp-overlay", "/usr/lib",
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });
  }

  // Reset, so our caches aren't poisoned by the Proxy.
  void reset() {searched = {}; directories = {};}
}
