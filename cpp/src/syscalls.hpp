#pragma once
/**
 * @brief Syscall Filter Generation
 * This header includes the filter() command for generating a SECCOMP-BPF filter to restrict syscall
 * within the sandbox. Moving to C/C++ allows us to use the libseccomp library directory, which is already
 * installed on any systemd system, allowing us to avoid a dependency where the python bindings are packaged
 * separately.
 * Implementation wise, the groups have been removed, as have numerical syscall numbers. Groups were a bad idea
 * from the start; any system that combines syscalls will introduce more attack surface, especially for a
 * grouping to be broad enough that a filter can be made by hand--the original goal. This also means
 * that offering syscalls on the command line are not supported. Secondly, while portability
 * of the application folders isn't a must, numerical syscalls make the files nebulous and system-specific.
 */

#include "shared.hpp"

namespace syscalls {

  /**
   * @brief Generate a syscall filter.
   * @param application: The application.
   * @returns The path to the BPF Filter.
   */
  std::string filter(const std::string& application);

  /**
   * @brief Append an existing policy with newly discovered syscalls.
   * @param application: The name of the application, to locate the syscalls.txt
   * @param strace: The output of an straced-run.
   * @note This function subsumes sb-seccomp in a far better way. Rather than parsing
   * the Audit logs (Requiring root permission), we just call --seccomp=strace which
   * will run the program with strace, capture STDERR (The channel strace logs to),
   * and then parse that output to update the file.
   */
  void update_policy(const std::string& application, const shared::vector &straced);
}
