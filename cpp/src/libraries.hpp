#pragma once
/**
 * @brief Shared-Library Dependency Resolution
 * This header contains all the relevant functions for resolving shared-libraries needed
 * by ELF binaries. It does this through through threaded calls to LDD. Unlike
 * Python-SB, SB++ computes dependencies in one shot, including both wildcards and directories.
 * Therefore, there is no need for additional computation upon calling setup(), it can simply
 * copy the found files to the SOF. LDD is the major bottle neck for speed, and thus
 * the two implementations are comparable on speed on library resolution.
 */

#include "shared.hpp"
#include "arguments.hpp"

#include <fstream>

namespace libraries {

  using lib_t = shared::set;
  extern lib_t directories;

  /**
   * @brief Recursively resolve all shared-libraries needed by a library.
   * @param library: The path to the library.
   * @param directory: The sub-directory within /usr/lib. Used internally.
   * @returns: A set of all shared libraries used by the program and its dependencies.
   * @note library can be any executable file, such as binaries in /usr/bin, but only
   * shared-libraries in /usr/lib will be included in the return.
   */
   void get(lib_t& libraries, const std::string_view& library, std::string directory = "");

  /**
   * @brief Setup an SOF directory
   * @param libraries: The libraries to copy into the SOF
   * @param sof: The path to the SOF directory
   * @param application: The name of the app.
   * @param command: The command vector to append the needed bwrap args to link to the SOF.
   */
   void setup(const lib_t& libraries, const std::string_view& application);

   /**
    * @brief Add symlink commands.
    * @param command: The command to append to
    * @param application: The running application.
    * @info This function symlinks /lib /lib64 and /usr/lib64 to /usr/lib, where
    * the SOF is mounted
    */
  void symlink(shared::vector& command);

  template <class C> void resolve(const C& required, const std::string_view& program, const std::string& lib_cache, const std::string_view& hash) {
    shared::log({"Resolving SOF"});

    // Generate the list of invalid entries. Because
   // we only read to the set, there is no risk in sharing it between
   // the threads, so no mutex required.
    shared::set exclusions = {};
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
