#pragma once

/**
 * @brief Generator functions.
 * This header contains generator functions, either used to
 * assemble the bwrap command, or auxiliary functions.
 */

#include "shared.hpp"

namespace generate {

  /**
   * @brief Generate a script for the sandbox
   * @param binary: Where to write the script to.
   */
  void script(const std::string& binary);

  /**
   * @brief Generate a desktop file for the sandbox
   * @param path: The desktop name.
   */
  void desktop_entry(const std::string& name);

  /**
   * @brief Create a .flatpak-info file for the program.
   * @param program: The name of the program.
   * @param instance: The instance name, located in $XDG_RUNTIME_DIR/.flatpak.
   * @param work_dir: The working dir for the program instance.
   */
  void flatpak_info(const std::string_view& program, const std::string_view& instance, const shared::TemporaryDirectory& work_dir);

  /**
   * @brief Spawn an instance of the xdg-dbus-proxy for the application instance.
   * @param program: The name of the program attached to this proxy.
   * @param work_dir: The application's instance work dir.
   * @returns A pair, including the wd to wait for the bus, and a future to return the pid of the proxy.
   */
  std::pair<int, std::future<int>> xdg_dbus_proxy(const std::string& program, const shared::TemporaryDirectory& work_dir);

  /**
   * @brief Generate the bulk of the main program command.
   * @param program: The name of the program.
   * @returns The generated command.
   * @info Parts of the command that should be computed at runtime, like environment
   * variables, should be done outside this function.
   */
  shared::vector cmd(const std::string& program);
}
