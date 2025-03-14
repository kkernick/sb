#include "arguments.hpp"


#ifndef VERSION
#define VERSION "Unknown"
#endif

using namespace shared;

namespace arg {

  vector unknown = {}, args;

  // All switches
  std::map<std::string, arg::Arg> switches = {

    // The command itself.
    // You can either provide it via position 0, or through flags --cmd/-C
    {"cmd", arg::config{.l_name="--cmd", .s_name="-C", .position=0, .help="The command to sandbox"}},

    // Switches that just check if they are set.
    {"help", arg::config{.l_name="--help", .s_name="-h", .help="Print this message"}},
    {"version", arg::config{.l_name="--version", .s_name="-V", .help="Get the version"}},
    {"verbose", arg::config{.l_name="--verbose", .s_name="-v", .valid={"log", "debug", "error", "strace"}, .help="Print verbose information"}},

    // True/False options; these get top priority for lower cases short-switches so that they can be chained together.
    {"gui", arg::config{
      .l_name="--gui", .s_name="-g",
      .valid={"false", "true"},
      .help="Give the sandbox access to wayland and DRI libraries to render GUIs.",
    }},
    {"pipewire", arg::config{
      .l_name="--pipewire", .s_name="-p",
      .valid={"false", "true"},
      .help="Give the sandbox access to pipewire for audio/screenshare/camera.",
    }},
    {"hardened_malloc", arg::config{
      .l_name="--hardened-malloc", .s_name="-m",
      .valid={"false", "true"},
      .help="Enforce hardened malloc within the sandbox.",
    }},
    {"xdg_open", arg::config{
      .l_name="--xdg-open", .s_name="-x",
      .valid={"false", "true"},
      .help="Provide xdg-open semantics within the sandbox.",
    }},
    {"shell", arg::config{
      .l_name="--shell", .s_name="-s",
      .valid={"false", "true", "debug"},
      .help="Provide /usr/bin/sh, optionally drop into it",
    }},
    {"locale", arg::config{
      .l_name="--locale", .s_name="-l",
      .valid={"false", "true"},
      .help="System locale",
    }},
    {"dry", arg::config{
      .l_name="--dry", .s_name="-d",
      .valid={"false", "true"},
      .help="Generate a sandbox, but don't actually run the application.",
    }},
    {"electron", arg::config{
      .l_name="--electron", .s_name="-e",
      .valid={"false", "true"},
      .custom=custom_policy::TRUE,
      .help="For electron applications. Implies --gtk --proc and --share user. Can also set to custom version.",
    }},
    {"include", arg::config{
      .l_name="--include", .s_name="-i",
      .valid={"false", "true"},
      .help="/usr/include for C/C++ headers (IE for clang).",
    }},
    {"dry_startup", arg::config{
      .l_name="--dry-startup", .s_name="-u",
      .valid={"false", "true"},
      .help="Populate the program's SOF on startup (Enable sb.service)",
    }},
    {"startup", arg::config{
      .l_name="--startup", .s_name="",
      .valid={"false", "true"},
      .help="If the service is running at startup (Do not use).",
    }},
    {"hostname", arg::config{
      .l_name="--hostname", .s_name="-n",
      .valid = {"false", "true"},
      .help="Pass the real hostname to the sandbox",
    }},
    {"refresh", arg::config{
      .l_name="--refresh", .s_name="-r",
      .valid={"true", "false"},
      .help="Refresh caches destructively (Removes cache and SOF)",
    }},
    {"vulkan", arg::config{
      .l_name="--vulkan", .s_name="",
      .valid={"false", "true"},
      .help="Give the sandbox access to Vulkan. Implies --gui",
    }},

    // Discrete, Single-Value Switches.
    {"update", arg::config{
      .l_name="--update", .s_name="",
      .valid={"libraries", "cache", "all"},
      .custom = custom_policy::MODIFIABLE,
      .help="Update caches, even if they exist. Use the dirty modifier to preserve existing SOFs.",
    }},
    {"seccomp", arg::config{
      .l_name="--seccomp", .s_name="",
      .valid={"permissive", "enforcing", "strace"},
      .help="Use a BPF-SECCOMP Filter to restrict syscalls.",
    }},
    {"file_passthrough", arg::config{
      .l_name="--file-passthrough", .s_name="",
      .valid={"ro", "rw", "discard"},
      .help="Policy for files passthrough through the command line or via --files.",
    }},
    {"post", arg::config{
      .l_name="--post", .s_name="-P",
      .custom = custom_policy::MODIFIABLE,
      .help="A command to run after the sandbox, with args as a modifier.",
    }},
    {"qt", arg::config{
      .l_name="--qt", .s_name="",
      .valid = {"false", "5", "6", "kf6"},
      .help="Share QT libraries.",
    }},
    {"gtk", arg::config{
      .l_name="--gtk", .s_name="",
      .valid={"false", "true", "3", "4"},
      .help="For gtk applications. Implies --gui",
    }},
    {"python", arg::config{
      .l_name="--python", .s_name="",
      .help="Provide the specified version of python.",
    }},

    // Lists of Values.
    {"libraries", arg::config{
      .l_name="--libraries", .s_name="",
      .custom = custom_policy::MODIFIABLE,
      .list=true,
      .help="Additional libraries to be provided in the sandbox. Use the :x modifier to exclude paths.",
    }},
    {"binaries", arg::config{
      .l_name="--binaries", .s_name="",
      .list=true,
      .help="Additional libraries to be provided in the sandbox",
    }},
    {"devices", arg::config{
      .l_name="--devices", .s_name="",
      .list=true,
      .help="Devices to add to the sandbox. Ignored if --dev",
    }},
    {"env", arg::config{
      .l_name="--env", .s_name="",
      .list=true,
      .help="Environment variables KEY=VALUE to pass into the sandbox.",
    }},
    {"files", arg::config{
      .l_name="--files", .s_name="",
      .custom = custom_policy::MODIFIABLE,
      .list=true,
      .help="Files to provide into the sandbox's enclave. You can provide do/dw for direct read/write",
    }},
    {"app_dirs", arg::config{
      .l_name="--app-dirs", .s_name="",
      .valid = {"etc", "lib", "share", "opt"},
      .list=true,
      .help="Application specific folders to add to the sandbox",
    }},
    {"sys_dirs", arg::config{
      .l_name="--sys-dirs", .s_name="",
      .valid = {"dev", "proc", "lib", "bin", "etc", "share", "var"},
      .list=true,
      .help="Application specific folders to add to the sandbox",
    }},
    {"share", arg::config{
      .l_name="--share", .s_name="",
      .def = "none",
      .valid = {"user", "ipc", "pid", "net", "cgroup", "none", "all"},
      .list=true,
      .help="Share namespaces.",
    }},

    // Portals.
    {"portals", arg::config{
      .l_name="--portals", .s_name="",
      .list=true,
      .help="Freedesktop portals to pass to the sandbox",
    }},
    {"see", arg::config{.l_name="--see", .s_name="", .list=true, .help="Busses visible in the sandbox"}},
    {"talk", arg::config{.l_name="--talk", .s_name="", .list=true, .help="Busses that can be communicated within the sandbox"}},
    {"own", arg::config{.l_name="--own", .s_name="", .list=true, .help="Busses that can be owned within the sandbox"}},


    // Make the Desktop/Script
    {"desktop_entry", arg::config{
      .l_name="--desktop-entry", .s_name="-D",
      .valid={"true"},
      .custom=custom_policy::TRUE,
      .help="Create a desktop entry for the application",
    }},
    {"script", arg::config{.l_name="--script", .s_name="-S", .valid={"true"}, .help="Create a script file for the application"}},


    // SOF directory, which resolves the actual path.
    {"sof", {arg::config{
      .l_name="--sof", .s_name="",
      .def = "tmp",
      .valid={"tmp", "data", "zram"},
      .help="Create a script file for the application",
      },

      [](const std::string_view& v) -> std::string {
        if (v == "tmp") return "/tmp/sb";
        else if (v == "data") return data + "/sb";
        else if (v == "zram") return "/run/sb";
        return std::string(v);
      }
    }},
    {"fs", {arg::config{
      .l_name="--fs", .s_name="", .mod="fs",
      .valid={"persist", "cache"},
      .custom = custom_policy::MODIFIABLE,
      .help="Create a script file for the application",
      },
      [](const std::string_view& v) -> std::string {return std::string(v);},
      [](const std::string_view& v) -> std::string {
        if (switches.contains("cmd") && !v.starts_with('/'))
          return std::filesystem::path(data) / "sb" / std::filesystem::path(arg::get("cmd")).filename() / v;
        return std::string(v);
      }
    }},
  };


  // Parse arguments
  void parse_args() {

    // Parse a .conf file. They set defaults.
    auto c = std::filesystem::path(shared::config) / "sb" / "sb.conf";
    if (std::filesystem::exists(c)) {
      for (const auto& conf : read_file<vector>(c, vectorize)) {
        if (!conf.contains("=")) {
          std::cout << "Invalid configuration: " << conf << std::endl;
        }
        else {
          auto s = init<vector>(split, conf, '=', false);
          auto k = s[0], v = s[1];
          std::transform(k.begin(), k.end(), k.begin(), ::tolower);

          if (!switches.contains(k)) std::cout << "Unrecognized configuration: " << conf << std::endl;
          else if (!valid(k).contains(v)) std::cout << "Invalid option for " << k << ": " << v << std::endl;
          else get(k) = v;
        }
      }
    }

    // Digest the arguments.
    for (uint_fast8_t x = 0; x < args.size(); ++x) {
      bool match = false;
      for (auto& [key, value] : switches) {
        try {
          if (value.digest(args, x)) {
            match = true;
            break;
          }
        }
        catch (std::runtime_error& e) {
          std::stringstream out;
          if (std::string(e.what()) == "Help!") {
            out << "SB++ v" << VERSION << '\n' << "Run applications in a sandbox\n";

            auto compare = [](const Arg& a, const Arg& b){return a.position() < b.position();};
            std::multiset<Arg, decltype(compare)> ordered;
            for (const auto& [key, value] : switches)
              ordered.emplace(value);
            for (const auto& value : ordered)
              out << value.get_help();
          }
          else if (std::string(e.what()) == "Version") out << VERSION << std::endl;
          else out << e.what() << std::endl;

          std::cout << out.str();
          exit(0);

        }
      }
      if (!match) unknown.emplace_back(args[x]);
    }

    // Post update.
    for (auto& [key, value] : switches) value.update();

    // Report the values.
    if (get("verbose") == "debug") {
      std::cout << "Arguments: " << std::endl;
      for (auto& [key, value] : switches) {
        std::cout << key << ": ";
        if (value.is_list()) {
          for (const auto& x : value.get_list()) std::cout << x << ' ';
        }
        else std::cout << value.get();
        if (!value.mod().empty()) std::cout << '(' << value.mod() << ')';
        std::cout << std::endl;
      }
      std::cout <<  "Unknown: ";
      for (const auto& value : unknown)
        std::cout << value << ' ';
      std::cout << std::endl;
    }
  }
}
