/**
 * @brief Binary Dependency Resolution
 * This header contains the functions needed to determine the dependencies needed for
 * binary files. It does this both against traditional ELF binaries, leveraging LDD in
 * libraries.hpp, but also supports tokenizing and parsing shell scripts to extract shebangs
 * and commands used for dynamic dependency resolution. This support is far more nuanced than
 * in Python-SB, as we incorporate variables and using the shell interpreter itself to
 * uncover libraries and binaries inaccessible to the old tokenizer.
 */

#include "shared.hpp"
#include "libraries.hpp"

namespace binaries {

  using bin_t = shared::set;

  /**
   * @brief Parse a binary to determine dependencies.
   * @param path: The path to the binary.
   * @param libraries: The current list of libraries.
   * @returns A set of all binaries used by the program, including itself.
   * @info libraries is updated.
   */
  void parse(bin_t& required, const std::string_view& p, libraries::lib_t& libraries);

  /**
   * @brief Setup the sandbox for the used binaries.
   * @param binaries: The binaries to link into the sandbox.
   * @param application: The application to link to this SOF.
   * @param command: The sandbox command
   */
   void setup(const bin_t& binaries, shared::vector& command);

   void symlink(shared::vector& command);
}
