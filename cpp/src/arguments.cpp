#include "arguments.hpp"


#ifndef VERSION
#define VERSION "Unknown"
#endif

using namespace shared;
using namespace exec;

namespace arg {

  vector unknown = {}, args;

  std::string hash = "";


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
      .valid={"vulkan", "vaapi"},
      .flag_set = true,
      .help="Give the sandbox access to wayland and DRI libraries to render GUIs.",
      .updates_sof = true,
    }},
    {"pipewire", arg::config{
      .l_name="--pipewire", .s_name="-p",
      .valid={"false", "true"},
      .help="Give the sandbox access to pipewire for audio/screenshare/camera.",
      .updates_sof = true
    }},
    {"hardened_malloc", arg::config{
      .l_name="--hardened-malloc", .s_name="-m",
      .valid={"false", "true", "light"},
      .help="Enforce hardened malloc within the sandbox.",
      .updates_sof = true
    }},
    {"xdg_open", arg::config{
      .l_name="--xdg-open", .s_name="-x",
      .valid={"false", "true"},
      .help="Provide xdg-open semantics within the sandbox.",
      .updates_sof = true
    }},
    {"shell", arg::config{
      .l_name="--shell", .s_name="-s",
      .valid={"false", "true", "debug"},
      .help="Provide /usr/bin/sh, optionally drop into it",
      .updates_sof = true
    }},
    {"locale", arg::config{
      .l_name="--locale", .s_name="-l",
      .valid={"false", "true"},
      .help="System locale",
      .updates_sof = true
    }},
    {"no-flags", arg::config{
      .l_name="--no-flags", .s_name="",
      .valid={"false", "true"},
      .help="Don't use a flag file if it exists",
      .updates_sof = true
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
      .updates_sof = true
    }},
    {"include", arg::config{
      .l_name="--include", .s_name="-i",
      .valid={"false", "true"},
      .help="/usr/include for C/C++ headers (IE for clang).",
      .updates_sof = true
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
    {"stats", arg::config{
      .l_name="--stats", .s_name="",
      .valid = {"false", "true"},
      .help="Compute sandbox statistics.",
      .updates_sof = true
    }},

    // Discrete, Single-Value Switches.
    {"update", arg::config{
      .l_name="--update", .s_name="",
      .valid={"libraries", "cache", "all", "clean"},
      .custom = custom_policy::MODIFIABLE,
      .help="Update caches, even if they exist. Use :batch to exclude cache deletion, and :exit to quit after cleaning.",
    }},
    {"seccomp", arg::config{
      .l_name="--seccomp", .s_name="",
      .valid={"permissive", "enforcing", "strace"},
      .help="Use a BPF-SECCOMP Filter to restrict syscalls.",
      .updates_sof = true,
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
      .updates_sof = true
    }},
    {"python", arg::config{
      .l_name="--python", .s_name="",
      .help="Provide the specified version of python.",
      .updates_sof = true
    }},
    {"xorg", arg::config{
      .l_name="--xorg", .s_name="-X",
      .valid={"resize", "fullscreen", "gl"},
      .flag_set = true,
      .help="Provide an isolated Xorg instance for the application, giving the resolution.",
    }},
    {"xsize", arg::config{
      .l_name="--xsize", .s_name="",
      .help="The size of the X-Window. Requires --xorg",
    }},


    // Lists of Values.
    {"libraries", arg::config{
      .l_name="--libraries", .s_name="",
      .custom = custom_policy::MODIFIABLE,
      .list=true,
      .help="Additional libraries to be provided in the sandbox. Use the :x modifier to exclude paths.",
      .updates_sof = true
    }},
    {"binaries", arg::config{
      .l_name="--binaries", .s_name="",
      .list=true,
      .help="Additional libraries to be provided in the sandbox",
      .updates_sof = true
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
      .updates_sof = true
    }},
    {"sys_dirs", arg::config{
      .l_name="--sys-dirs", .s_name="",
      .valid = {"dev", "proc", "lib", "bin", "etc", "share", "var"},
      .list=true,
      .help="Application specific folders to add to the sandbox",
      .updates_sof = true
    }},
    {"share", arg::config{
      .l_name="--share", .s_name="",
      .def = "none",
      .valid = {"user", "ipc", "pid", "net", "cgroup", "none", "all"},
      .list=true,
      .help="Share namespaces.",
      .updates_sof = true,
    }},
    {"encrypt", arg::config{
      .l_name="--encrypt", .s_name="-E",
      .valid={"persist", "init"},
      .flag_set = true,
      .help="Encrypt the profile's configuration and filesystems as $XDG_DATA_HOME.",
      .updates_sof = true
    }},
    {"spelling", arg::config{
      .l_name="--spelling", .s_name="",
      .valid = {"hunspell", "enchant"},
      .list=true,
      .help="Spell checking support",
      .updates_sof = true
    }},
    {"gtk", arg::config{
      .l_name="--gtk", .s_name="",
      .valid={"3", "4", "gdk", "sourceview", "adwaita", "gir", "webkit", "gst"},
      .list = true,
      .help="For gtk applications. Implies --gui",
      .updates_sof = true
    }},
    {"no-tmpfs", arg::config{
      .l_name="--no-tmpfs", .s_name="",
      .valid = {"tmp", "cache"},
      .list=true,
      .help="Keep temporary files",
      .updates_sof = true
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
      .def = "usr",
      .valid={"usr", "tmp", "data", "zram"},
      .custom = custom_policy::TRUE,
      .help="The SOF backing medium. Optionally provide an explicit path",
      },

      [](const std::string_view& v) -> std::string {
        if (v == "tmp") return "/tmp/sb";
        else if (v == "data") return data + "/sb";
        else if (v == "zram") return "/run/sb";
        else if (v == "usr") return "/usr/share/sb";
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
        if (switches.contains("cmd")) {
          auto basename = arg::get("cmd");
          if (basename.contains('/')) basename = basename.substr(basename.rfind('/') + 1);
          return std::filesystem::path(data) / "sb" / basename / v;
        }
        return std::string(v);
      }
    }},
  };


  // Parse arguments
  void parse_args() {

    // Parse a .conf file. They set defaults.
    auto c = std::filesystem::path(shared::config) / "sb" / "sb.conf";
    if (std::filesystem::exists(c)) {
      for (const auto& conf : file::parse<vector>(c, vectorize)) {
        if (!conf.contains("=")) {
          std::cout << "Invalid configuration: " << conf << std::endl;
        }
        else {
          auto s = container::init<vector>(container::split<vector, char>, conf, '=', false);
          auto k = s[0], v = s[1];
          std::transform(k.begin(), k.end(), k.begin(), ::tolower);

          if (!switches.contains(k)) std::cout << "Unrecognized configuration: " << conf << std::endl;
          else if (!valid(k).contains(v)) std::cout << "Invalid option for " << k << ": " << v << std::endl;
          else emplace(k, v);
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
    hash = VERSION;
    for (auto& [key, value] : switches) {
      value.update();
      if (value.updates_sof()) hash.append(value.is_list() ? join(value.get_list(), ' ') : value.get());
      else if (key == "verbose") hash.append(std::to_string(value >= "error"));
    }
    hash = shared::hash(hash);

    // Report the values.
    if (get("verbose") == "debug") {
      std::cout << "Arguments: " << std::endl;
      for (auto& [key, value] : switches) {
        std::cout << key << ": ";
        if (value.is_list()) {
          for (const auto& x : value.get_list()) std::cout << x << ' ';
        }
        else if (value.is_flagset()) {
          if (value) {
            std::cout << "true";
            if (value.get_list().size()) {
              std::cout << ": ";
              for (const auto& x : value.get_list()) std::cout << x << '|';
            }
          }
          else std::cout << "false";
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
