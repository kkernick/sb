#include "generators.hpp"
#include "shared.hpp"
#include "arguments.hpp"
#include "libraries.hpp"
#include "binaries.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace shared;

namespace generate {

  // Generate the script file
  void script(const std::string& binary) {
    auto arguments = arg::args;

    // Carve out the redundant options
    for (auto iter = arguments.begin(); iter != arguments.end();) {
      if (iter->contains("--desktop-entry") || iter->contains("--script")) {
        arguments.erase(iter);
        if (!iter->starts_with("-")) arguments.erase(iter);
      }
      else ++iter;
    }

    // Write out
    auto out = std::ofstream(binary);
    out << "#!/bin/sh\n";
    out << "sb " << join(arguments, ' ') << " -- \"$@\"";
    out.close();
    exec_pid({"chmod", "+x", binary});
  }

  // Generate the desktop file
  void desktop_entry(const std::string& name) {

    // Ensure the source exists.
    auto src = "/usr/share/applications/" + name;
    if (!std::filesystem::exists(src)) {
      std::cerr << "Missing application: " << src << std::endl;
      exit(1);
    }

    // Ensure there's a script file to link to
    auto binary = home + "/.local/bin/" + name + ".sb";
    if (!std::filesystem::exists(binary)) script(binary);

    // Modify the Exec and DBus lines
    auto contents = split(read_file(src), '\n');
    for (auto& line : contents) {
      if (line.starts_with("Exec="))
        line = "Exec=" + binary + (line.contains(' ') ? line.substr(line.find(' ')) : "\n");
      else if (line.starts_with("TryExec="))
        line = "Exec=" + binary + (line.contains(' ') ? line.substr(line.find(' ')) : "\n");
      else if (line.starts_with("DBusActivatable="))
        line = "DBusActivatable=false\n";
    }

    // Write out to a local copy.
    auto local = data + "/applications/" + name;
    auto local_file = std::ofstream(local);
    local_file << join(contents, '\n');
    local_file.close();
  }

  // Generate the .flatpak-info file
  void flatpak_info(const std::string& program, const std::string& instance, const TemporaryDirectory& work_dir) {
    auto info = std::ofstream(work_dir.sub(".flatpak-info"));
    info  << "[Application]\n"
          << "name=app.application." << program << '\n';

    info  << "[Context]\n"
          << "sockets=session-bus;system-bus;";

    if (arg::at("gui")) info << "wayland;";
    if (arg::at("pipewire")) info << "pulseaudio;";

    info  << "\n[Instance]\n"
          << "instance-id=" << instance << "\n"
          << "session-bus-proxy=True\n"
          << "system-bus-proxy=True\n";

    if (!arg::list("env").empty()) info << "[Environment]\n";
    for (const auto& variables : arg::list("env")) info << variables << '\n';

    info.flush();
    info.close();
  }


  std::pair<int, std::future<int>> xdg_dbus_proxy(const std::string& program, const TemporaryDirectory& work_dir) {
    auto proxy_path = work_dir.sub("proxy", true);
    auto wd = inotify_add_watch(inotify, proxy_path.c_str(), IN_CREATE);
    if (wd < 0) throw std::runtime_error(std::string("Failed to add inotify watcher: ") + strerror(errno));

    auto proxy = [&work_dir, &program]() {
      const auto app_dir = join({arg::get("sof"), "xdg-dbus-proxy", "lib"}, '/');
      std::vector<std::string> command = {
        "bwrap",
        "--new-session",
        "--clearenv",
        "--setenv", "PATH", "/usr/bin",
        "--disable-userns",
        "--assert-userns-disabled",
        "--unshare-all",
        "--unshare-user",
        "--bind", runtime, runtime,
        "--ro-bind", work_dir.sub(".flatpak-info"), "/.flatpak-info",
        "--symlink", "/.flatpak-info", "/run/user/" + real + "/flatpak-info",
        "--bind", work_dir.get_path() + "/proxy", runtime + "/app/app.application." + program,
        "--die-with-parent",
        "--ro-bind", "/usr/bin/xdg-dbus-proxy", "/usr/bin/xdg-dbus-proxy",
      };

      if (arg::at("hardened_malloc"))
        extend(command, {"--ro-bind", work_dir.sub("ld.so.preload"), "/etc/ld.so.preload"});

      std::set<std::string> libraries = {};
      if (arg::at("hardened_malloc")) libraries.merge(libraries::get("/usr/lib/libhardened_malloc.so"));
      binaries::parse("/usr/bin/xdg-dbus-proxy", libraries);
      libraries::setup(libraries, "xdg-dbus-proxy");

      extend(command, {"--overlay-src", app_dir, "--tmp-overlay", "/usr/lib"});
      libraries::symlink(command, "xdg-dbus-proxy");
      binaries::symlink(command);

      extend(command, {
        "--",
        "/usr/bin/xdg-dbus-proxy",
        std::getenv("DBUS_SESSION_BUS_ADDRESS"),
        runtime + "/app/app.application." + program + "/bus",
        "--filter",
        "--call=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.Read@/org/freedesktop/portal/desktop",
        "--broadcast=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.SettingChanged@/org/freedesktop/portal/desktop",
      });

      if (arg::at("verbose")) command.emplace_back("--log");
      if (arg::at("xdg_open")) command.emplace_back("--talk=org.freedesktop.portal.OpenURI");
      for (const auto& portal : arg::list("portals")) command.emplace_back("--talk=org.freedesktop.portal." + portal);
      for (const auto& portal : arg::list("see")) command.emplace_back("--see=" + portal);
      for (const auto& portal : arg::list("talk")) command.emplace_back("--talk=" + portal);
      for (const auto& portal : arg::list("own")) command.emplace_back("--own=" + portal);

      return exec_pid(command);
    };

    return {wd,  pool.submit_task(proxy)};
  }


  std::vector<std::string> cmd(const std::string& program) {
    auto sof_dir = std::filesystem::path(arg::get("sof")) / program;
    auto local_dir = std::filesystem::path(data) / "sb" / program;

    auto lib_dir = sof_dir / "lib";
    auto lib_cache = local_dir / "lib.cache";
    auto cmd_cache = local_dir / "cmd.cache";

    std::vector<std::string> command;
    command.reserve(200);
    std::set<std::string> binaries, libraries;

    // So that we can easily query as unique, and to emplace.
    auto sys_dirs = arg::list("sys_dirs");
    auto app_dirs = arg::list("app_dirs");
    auto shared = arg::list("share");

    bool lib = !sys_dirs.contains("lib"), bin = !sys_dirs.contains("bin");

    std::string arguments;
    const std::set<std::string> omitted = {"startup", "dry", "update", "dry_startup", "verbose"};
    for (auto& [key, value] : arg::switches) {
      if (!omitted.contains(key)) {
        if (value.is_list()) arguments += join(value.get_list(), ' ');
        else arguments += value.get();
      }
    }
    arguments += arg::at("verbose").under("error");
    auto hash = shared::hash(arguments);

    bool update_sof = arg::at("update");
    if (lib && !update_sof) {
      if (std::filesystem::exists(lib_cache)) {
        auto lib_file = split(read_file(lib_cache), '\n');
        if (lib_file.size() == 2 && hash == lib_file[0]) {

          // We should check the command cache here since it's predicated
          // on use not needing to update anything.
          if (std::filesystem::exists(cmd_cache) && std::filesystem::is_directory(lib_dir)) {
            auto cmd_file = split(read_file(cmd_cache), '\n');
            if (cmd_file.size() == 2 && hash == cmd_file[0]) {
              log({"Reusing existing command cache"});
              return split(cmd_file[1], ' ', true);
            }
          }
          log({"Reusing existing library cache"});
        libraries = split<std::set<std::string>>(lib_file[1], ' ');
        }
        else {
          log({"Library cache out of date!"});
          update_sof = true;
        }
      }
      else {
        log({"Library cache missing!"});
        update_sof = true;
      }
    }
    if (update_sof) log({"Updating SOF"});

    if (!lib) extend(command, {
      "--overlay-src", "/usr/lib", "--tmp-overlay", "/usr/lib",
      "--symlink", "/usr/lib", "/lib64",
      "--symlink", "/usr/lib", "/lib",
      "--symlink", "/usr/lib", "/usr/lib64",
    });

    if (!bin) extend(command, {
      "--overlay-src", "/usr/bin", "--tmp-overlay", "/usr/bin",
      "--symlink", "/usr/bin", "/bin",
      "--symlink", "/usr/bin", "/sbin",
      "--symlink", "/usr/bin", "/usr/sbin"
    });

    // Initial command and environment.
    if (bin) {
      log({"Resolving binaries"});
      binaries.merge(binaries::parse(arg::get("cmd"), libraries));
      binaries.merge(binaries::parsel(arg::list("binaries"), libraries));

      // Get libraries needed for added executables.
      if (arg::at("fs") && std::filesystem::is_directory(arg::mod("fs") + "/usr/bin")) {
        binaries::parsel(split<std::set<std::string>>(exec({"find", arg::mod("fs") + "/usr/bin", "-type", "f,l", "-executable"}), '\n'), libraries);
      }
    }
    if (update_sof) {
      log({"Resolving libraries"});
      for (const auto& [lib, mod] : arg::modlist("libraries")) {
        if (mod != "x") libraries.merge(libraries::get(lib));
      }
    }

    if (arg::at("verbose").meets("error") && bin) binaries.merge(binaries::parse("strace", libraries));
    extend(command, {"--setenv", "HOME", "/home/sb", "--setenv", "PATH", "/usr/bin"});

    // XDG Open
    if (arg::at("xdg_open")) {
      log({"Adding XDG-Open"});
      if (bin) binaries.merge(binaries::parse("/usr/bin/sb-open", libraries));
      extend(command, {
        "--symlink", "/usr/bin/sb-open", "/usr/bin/xdg-open",
        "--ro-bind", "/usr/bin/sb-mime", "/usr/bin/xdg-mime"
      });
    }

    // Hardened Malloc
    if (arg::at("hardened_malloc") && update_sof) {
      log({"Adding Hardened Malloc"});
      libraries.merge(libraries::get("/usr/lib/libhardened_malloc.so"));
    }

    // A shell
    if (arg::at("shell")) {
      log({"Adding Shell"});
      if (bin) binaries.merge(binaries::parse("/usr/bin/sh", libraries));
      share(command, {
        "/etc/shells",
        "/etc/profile", "/usr/share/terminfo", "/var/run/utmp",
        "/etc/group", config + "/environment.d",
      });
    }

    if (arg::at("include")) {
      log({"Adding C/C++ Headers"});
      share(command, {"/usr/include", "/usr/local/include"});
      extend(libraries::directories, {"/usr/lib/clang", "/usr/lib/gcc"});
    }

    if (arg::at("electron")) {
      log({"Adding Electron"});
      share(command, {"/sys/block", "/sys/dev"});
      share(command, {"/dev/null", "/dev/urandom", "/dev/shm"}, "dev-bind");

      if (update_sof) libraries.merge(libraries::getl({"libsoftokn3*", "libfreeblpriv3*", "libsensors*", "libnssckbi*", "libsmime3*"}));

      // IE custom.
      if (arg::get("electron") != "true") {
        auto mod = arg::get("electron");
        if (bin) binaries.merge(binaries::parse("/usr/bin/electron" + mod, libraries));
        libraries::directories.emplace_back("/usr/lib/electron" + mod);
      }
      arg::get("gtk") = true;
      sys_dirs.emplace("proc");
      shared.emplace("user");
    }

    if (arg::at("gtk")) {
      log({"Adding GTK"});
      share(command, {
        home + "/.gtkrc-2.0", config + "/gtkrc", config + "/gtkrc-2.0",
        config + "/gtk-2.0", config + "/gtk-3.0", config + "/gtk-4.0",
        config + "/dconf",
        "/usr/share/gtk-2.0", "/usr/share/gtk-3.0", "/usr/share/gtk-4.0", "/usr/share/gtk",
        "/usr/share/glib-2.0",
        "/etc/xdg/gtk-3.0/",
        "/usr/share/gir-1.0/",
      });
      extend(command, {"--setenv", "GTK_USE_PORTAL", "1", "--setenv", "GTK_A11Y", "none"});

      extend(libraries::directories, {
        "/usr/lib/gdk-pixbuf-2.0/", "/usr/lib/gtk-3.0", "/usr/lib/tinysparql-3.0/",
        "/usr/lib/gtk-4.0", "/usr/lib/gio", "/usr/lib/gvfs",
        "/usr/lib/girepository-1.0"
      });

      if (update_sof) libraries.merge(libraries::getl({"libgvfs*", "librsvg*", "libgio*", "libgdk*", "libgtk*"}));
      arg::get("gui") = true;
    }

    if (arg::at("qt")) {
      log({"Adding QT"});
      auto version = arg::get("qt");
      if (version == "kf6") {
        libraries::directories.emplace_back("/usr/lib/kf6");
        libraries.merge(libraries::getl({"*Kirigami*", "libKF" + version + "*"}));
        version = "6";
      }

      share(command, {
        config + "/kdedefaults",
        config + "/breezerc",
        config + "/kdeglobals",
        config + "/Trolltech.conf",
        config + "/kde.org",
      });

      share(command, {"/usr/share/qt" + version});
      libraries::directories.emplace_back("/usr/lib/qt" + version);
      if (update_sof) {
        libraries.merge(libraries::get("libQt" + version + "*"));
       if (version != "kf6") libraries.merge(libraries::get("/usr/lib/kf6/kioworker"));
      }
      arg::get("gui") = true;
    }

    if (arg::at("gui")) {
      log({"Adding GUI"});
      share(command, {"/dev/dri", "/dev/udmabuf"}, "dev-bind");
      share(command, {
        "/sys/devices",
        "/sys/dev",
        "/etc/vulkan",
        "/usr/share/glvnd",
        "/usr/share/vulkan",
        "/usr/share/libdrm",
        "/usr/share/fontconfig", "/etc/fonts",
        config + "/fontconfig", data + "/fontconfig",
        "/usr/share/themes", "/usr/share/color-schemes", "/usr/share/icons", "/usr/share/cursors",
        "/usr/share/pixmaps",  data+ "/pixmaps",
        "/usr/share/mime", data + "/mime",
        runtime +"/wayland-0",
        "/usr/share/X11/xkb", "/etc/xkb"
      });
      extend(command, {"--setenv", "XDG_SESSION_TYPE", "wayland"});

      share(command, {"/usr/share/fonts", home + "/.fonts"});

      extend(libraries::directories, {"/usr/lib/dri", "/usr/lib/gbm"});
      if (update_sof) {
        libraries.merge(libraries::getl({
          "libvulkan*", "libglapi*", "*mesa*", "*Mesa*", "libdrm*", "libGL*", "libVkLayer*", "libgbm*",
          "libva*", "*egl*", "*EGL*"}
        ));
      }
    }

    if (arg::at("pipewire")) {
      log({"Adding Pipewire"});
      share(command, {
        runtime + "/pipewire-0", runtime + "/pulse",
        config + "/pulse", "/etc/pipewire", "/usr/share/pipewire"
      });
      if (update_sof) libraries.merge(libraries::get("libpipewire*"));
      extend(libraries::directories, {"/usr/lib/pipewire-0.3", "/usr/lib/spa-0.2", "/usr/lib/pulseaudio"});

    }

    if (arg::at("locale")) {
      log({"Adding System Locale"});
      share(command, {
        "/etc/locale.conf", "/etc/localtime",
        "/usr/share/zoneinfo", "/usr/share/X11/locale", "/usr/share/locale",
        config + "/plasma-localerc"
      });
      libraries::directories.emplace_back("/usr/lib/locale");
      if (bin) binaries.merge(binaries::parse("locale", libraries));
    }

    if (shared.contains("none")) command.emplace_back("--unshare-all");
    else for (const auto& ns : arg::valid("share")) {
      if (ns == "all" || ns == "none") continue;
      if (!shared.contains(ns)) command.emplace_back("--unshare-" + ns);
    }
    if (shared.contains("net")) {
      log({"Adding Networking"});
      share(command, {
        "/etc/gai.conf", "/etc/hosts.conf", "/etc/hosts", "/etc/nsswitch.conf", "/etc/resolv.conf",
        "/etc/gnutls", "/etc/ca-certificates", "/usr/share/ca-certificates",
        "/etc/pki", "/usr/share/pki",
        "/etc/ssl", "/usr/share/ssl",
        "/usr/share/p11-kit"
      });
      if (update_sof) libraries.merge(libraries::getl({"libnss*", "libgnutls*"}));
      libraries::directories.emplace_back("/usr/lib/pkcs11");
    }
    if (!shared.contains("user")) extend(command, {"--disable-userns", "--assert-userns-disabled"});

    // Parse system directories.
    if (sys_dirs.contains("dev")) extend(command, {"--dev", "/dev"});
    else share(command, arg::list("devices"), "dev-bind");

    if (sys_dirs.contains("proc")) extend(command, {"--proc", "/proc"});
    if (sys_dirs.contains("etc")) extend(command, {"--overlay-src", "/etc", "--tmp-overlay", "/etc"});
    if (sys_dirs.contains("share")) extend(command, {"--overlay-src", "/usr/share", "--tmp-overlay", "/usr/share"});
    if (sys_dirs.contains("var")) extend(command, {"--overlay-src", "/var", "--tmp-overlay", "/var"});

    // Application directories.
    if (app_dirs.contains("lib") && lib) libraries::directories.emplace_back("/usr/lib/" + program);
    if (app_dirs.contains("share")) share(command, {"/usr/share/" + program});
    if (app_dirs.contains("etc")) share(command, {"/etc" + program});
    if (app_dirs.contains("opt")) libraries::directories.emplace_back("/opt/" + program);

    if (bin) binaries::setup(binaries, command);
    binaries::symlink(command);

    if (update_sof || !std::filesystem::is_directory(lib_dir)) {
      for (const auto& dir : libraries::directories)
        libraries.merge(libraries::get(dir));

      // Because updating the SOF merely writes to the SOF directory, we can freely detach it
      // into another thread, and then call pool.wait() just before running the program.
      pool.detach_task([program, lib_cache, hash, libraries]() {libraries::resolve(libraries, program, lib_cache, hash);});
      extend(command, {"--overlay-src", lib_dir.string(), "--tmp-overlay", "/usr/lib"});
    }

    libraries::symlink(command, program);
    share(command, libraries::directories);

    auto out = std::ofstream(cmd_cache);
    out << hash << '\n';
    for (const auto& arg : command) {
      if (arg.contains(' '))
        out << '\'' << arg << '\'';
      else out << arg;
      out << ' ';
    }
    out.close();

    return command;
  }
}
