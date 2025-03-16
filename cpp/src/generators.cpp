#include "generators.hpp"
#include "arguments.hpp"
#include "binaries.hpp"
#include "libraries.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

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
    exec({"chmod", "+x", binary});
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
    auto contents = read_file<vector>(src, vectorize);
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
  void flatpak_info(const std::string_view& program, const std::string_view& instance, const TemporaryDirectory& work_dir) {
    auto info = std::ofstream(work_dir.sub(".flatpak-info"));
    info  << "[Application]\n"
          << "name=app.application." << program << '\n'

          << "[Context]\n"
          << "sockets=session-bus;system-bus;";

    if (arg::at("gui")) info << "wayland;";
    if (arg::at("pipewire")) info << "pulseaudio;";

    info  << "\n[Instance]\n"
          << "instance-id=" << instance << "\n"
          << "session-bus-proxy=True\n"
          << "system-bus-proxy=True\n";

    if (!arg::list("env").empty()) {
      info << "[Environment]\n";
      for (const auto& variables : arg::list("env")) info << variables << '\n';
    }

    info.flush();
    info.close();
  }


  std::pair<std::filesystem::path, std::future<void>> proxy_lib() {
    std::string p_hash;
    #ifdef VERSION
      p_hash = VERSION;
    #endif

    // This is the only applicable command line to the Proxy SOF.
    p_hash.append(arg::get("hardened_malloc"));
    p_hash = shared::hash(p_hash);

    auto lib_cache = libraries::hash_sof("xdg-dbus-proxy", p_hash);
    auto lib_setup = [lib_cache, p_hash = std::move(p_hash)]() {
      if (!std::filesystem::exists(lib_cache)) {
        auto cache = libraries::hash_cache("xdg-dbus-proxy", p_hash);
        if (std::filesystem::exists(cache) && !std::filesystem::is_empty(cache)) {
          log({"Using Proxy Cache"});
          libraries::resolve(read_file<vector>(cache, fd_splitter<vector, ' '>), "xdg-dbus-proxy", p_hash, false);
        }
        else {
          log({"Generating Proxy Cache"});
          libraries::lib_t libraries = {};
          if (arg::at("hardened_malloc")) libraries::get(libraries, "/usr/lib/libhardened_malloc.so");
          rinit<binaries::bin_t>(binaries::parse, "/usr/bin/xdg-dbus-proxy", libraries);
          libraries::resolve(libraries, "xdg-dbus-proxy", p_hash, false);
        }
      }
    };
    return {lib_cache, pool.submit_task(lib_setup)};
  }


  std::pair<int, std::future<int>> xdg_dbus_proxy(const std::string& program, const TemporaryDirectory& work_dir) {
    auto proxy_path = work_dir.sub("proxy", true);
    auto wd = inotify_add_watch(inotify, proxy_path.c_str(), IN_CREATE);
    if (wd < 0) throw std::runtime_error(std::string("Failed to add inotify watcher: ") + strerror(errno));

    auto proxy = [&work_dir, &program]() {
      vector command = {
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

      auto [lib_dir, lib_future] = proxy_lib();
      extend(command, {"--overlay-src", lib_dir.string(), "--tmp-overlay", "/usr/lib"});
      libraries::symlink(command);
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

      lib_future.wait();
      if (!arg::at("dry")) return exec<int>(command, get_pid);
      return -1;
    };

    return {wd,  pool.submit_task(proxy)};
  }


  // Generate the main command.
  vector cmd(const std::string& program) {

    const auto sof_dir = std::filesystem::path(arg::get("sof")) / program;
    const auto local_dir = std::filesystem::path(data) / "sb" / program;
    const auto cache_dir =  local_dir / "cache";
    std::filesystem::create_directories(sof_dir);
    std::filesystem::create_directories(local_dir);
    std::filesystem::create_directories(cache_dir);

    const auto lib_dir = libraries::hash_sof(program, arg::hash);
    const auto l_cache = cache_dir / (arg::hash + ".lib.cache");
    const auto c_cache = cache_dir / (arg::hash + ".cmd.cache");

    vector command;
    command.reserve(200);

    binaries::bin_t binaries; libraries::lib_t libraries;

    // So that we can easily query as unique, and to emplace.
    auto sys_dirs = arg::list("sys_dirs");
    auto shared = arg::list("share");

    const auto& app_dirs = arg::list("app_dirs");
    const bool lib = !sys_dirs.contains("lib"), bin = !sys_dirs.contains("bin");

    bool update_sof = arg::at("update") | !std::filesystem::exists(l_cache);
    if (lib && !update_sof) {
      if (std::filesystem::exists(c_cache)  && !std::filesystem::is_empty(c_cache)) {
        log({"Reusing existing command cache"});
        return read_file<vector>(c_cache, fd_splitter<vector, ' '>);
      }
    }
    if (update_sof) log({"Updating SOF"});

    if (!lib) extend(command, {"--overlay-src", "/usr/lib", "--tmp-overlay", "/usr/lib"});
    if (!bin) extend(command, {"--overlay-src", "/usr/bin", "--tmp-overlay", "/usr/bin"});

    // Initial command and environment.
    if (bin) {
      log({"Resolving binaries"});
      binaries::parse(binaries, arg::get("cmd"), libraries);
      single_batch(binaries::parse, binaries, arg::list("binaries"), libraries);

      // Get libraries needed for added executables.
      if (arg::at("fs") && std::filesystem::is_directory(arg::mod("fs") + "/usr/bin")) {
        single_batch(binaries::parse, binaries, exec<vector>({"find", arg::mod("fs") + "/usr/bin", "-type", "f,l", "-executable"}, vectorize, STDOUT), libraries);
      }
    }

    log({"Resolving libraries"});
    for (const auto& [lib, mod] : arg::modlist("libraries")) {
      if (mod != "x") {
        if (std::filesystem::is_directory(lib)) libraries::directories.emplace(lib);
        if (update_sof) libraries::get(libraries, lib);
      }
    }

    // Add strace if we need to.
    if (arg::at("verbose") >= "error" && bin) {
      log({"Adding strace"});
      if (bin) binaries::parse(binaries, "strace", libraries);
    }

    // Add our new home and path.
    extend(command, {"--setenv", "HOME", "/home/sb", "--setenv", "PATH", "/usr/bin"});

    // XDG Open
    if (arg::at("xdg_open")) {
      log({"Adding XDG-Open"});
      if (bin) binaries::parse(binaries, "/usr/bin/sb-open", libraries);
      extend(command, {"--symlink", "/usr/bin/sb-open", "/usr/bin/xdg-open"});
    }

    // Hardened Malloc
    if (arg::at("hardened_malloc") && update_sof) {
      log({"Adding Hardened Malloc"});
      libraries::get(libraries, "/usr/lib/libhardened_malloc.so");
    }

    // A shell
    if (arg::at("shell")) {
      log({"Adding Shell"});
      if (bin) binaries::parse(binaries, "/usr/bin/sh", libraries);
    }

    if (arg::at("include")) {
      log({"Adding C/C++ Headers"});
      batch(share, command, {"/usr/include", "/usr/local/include"}, "ro-bind");
      extend(libraries::directories, {"/usr/lib/clang", "/usr/lib/gcc"});
    }

    if (arg::at("python")) {
      log({"Adding Python"});
      std::string version = "python" + arg::get("python");
      if (bin) binaries::parse(binaries, version, libraries);
      if (update_sof) {
        libraries::get(libraries, "lib" + version);
        libraries::directories.emplace("/usr/lib/" + version);
      }
    }

    if (arg::at("electron")) {
      log({"Adding Electron"});
      batch(share, command, {"/dev/null", "/dev/urandom", "/dev/shm"}, "dev-bind");

      if (update_sof) batch(libraries::get, libraries, {"libsoftokn3*", "libfreeblpriv3*"}, "");

      // IE custom.
      if (arg::get("electron") != "true") {
        auto mod = arg::get("electron");
        if (bin) binaries::parse(binaries, "/usr/bin/electron" + mod, libraries);
        libraries::directories.emplace("/usr/lib/electron" + mod);
      }

      if (arg::at("gtk") < "3") arg::get("gtk") = "3";
      sys_dirs.emplace("proc");
      shared.emplace("user");
    }

    if (arg::at("gtk")) {
      log({"Adding GTK"});
      auto version = arg::get("gtk");

      batch(share, command, {
        "/usr/share/gtk", config + "/gtkrc", "/usr/share/glib-2.0",
        "/usr/share/gtk-2.0", home + "/.gtkrc-2.0",
        config + "/gtkrc-2.0", config + "/gtk-2.0"
      }, "ro-bind");

      if (version == "3" || version == "true")  {
        batch(share, command, {
          config + "/gtk-3.0",
          "/usr/share/gtk-3.0",
          "/etc/xdg/gtk-3.0/",
        }, "ro-bind");
        libraries::directories.emplace("/usr/lib/gtk-3.0");
        if (update_sof) batch(libraries::get, libraries, {"libgdk-3*", "libgtk-3*"}, "");
      }

      if (version == "4" || version == "true") {
        batch(share, command, {
          "/usr/share/gtk-4.0",
          config + "/gtk-4.0",
        }, "ro-bind");
        libraries::directories.emplace("/usr/lib/gtk-4.0");
        if (update_sof) batch(libraries::get, libraries, {"libgdk-4*", "libgtk-4*"}, "");
      }

      extend(command, {"--setenv", "GTK_USE_PORTAL", "1", "--setenv", "GTK_A11Y", "none"});
      arg::get("gui") = "true";
    }

    if (arg::at("qt")) {
      log({"Adding QT"});
      auto version = arg::get("qt");
      if (version == "kf6") {
        libraries::directories.emplace("/usr/lib/kf6");
        batch(libraries::get, libraries, {"*Kirigami*", "libKF" + version + "*"}, "");
        version = "6";
      }

      batch(share, command, {
        config + "/kdedefaults",
        config + "/breezerc",
        config + "/kdeglobals",
        config + "/Trolltech.conf",
        config + "/kde.org",
      }, "ro-bind");

      share(command, "/usr/share/qt" + version);
      libraries::directories.emplace("/usr/lib/qt" + version);
      if (update_sof) {
        libraries::get(libraries, "libQt" + version + "*");
       if (version != "kf6") libraries::get(libraries, "/usr/lib/kf6/kioworker");
      }
      arg::get("gui") = "true";
    }


    if (arg::at("vulkan")) {
      log({"Adding Vulkan"});
      batch(share, command, {"/etc/vulkan", "/usr/share/vulkan"}, "ro-bind");
      if (update_sof) batch(libraries::get, libraries, {"libvulkan*", "libVkLayer*"}, "");
      arg::get("gui") = "true";
    }

    if (arg::at("gui")) {
      log({"Adding GUI"});
      share(command, "/dev/dri", "dev-bind");
      batch(share, command, {
        "/sys/devices/pci0000:00",
        "/sys/dev/char",
        "/usr/share/glvnd",
        "/usr/share/libdrm",
        "/usr/share/fontconfig", "/etc/fonts",
        config + "/fontconfig", data + "/fontconfig",
        "/usr/share/fonts", home + "/.fonts",
        "/usr/share/themes", "/usr/share/color-schemes", "/usr/share/icons", "/usr/share/cursors",
        "/usr/share/pixmaps",  data+ "/pixmaps",
        "/usr/share/mime", data + "/mime",
        runtime +"/wayland-0",
        "/usr/share/X11/xkb",
      }, "ro-bind");

      extend(command, {
        "--setenv", "XDG_SESSION_TYPE", "wayland",
        "--setenv", "WAYLAND_DISPLAY", "wayland-0"
      });

      extend(libraries::directories, {"/usr/lib/dri", "/usr/lib/gbm"});
      if (update_sof) batch(libraries::get, libraries, {
        "*Mesa*", "*mesa*", "*EGL*", 
        "libva-drm*", "libva-wayland*", "libva.so*"
      }, "");
    }

    if (arg::at("pipewire")) {
      log({"Adding Pipewire"});
      batch(share, command, {
        runtime + "/pipewire-0", runtime + "/pulse",
        config + "/pulse", "/etc/pipewire", "/usr/share/pipewire"
      }, "ro-bind");
      if (update_sof) libraries::get(libraries, "libpipewire*");
      extend(libraries::directories, {"/usr/lib/pipewire-0.3", "/usr/lib/spa-0.2", "/usr/lib/pulseaudio"});

    }

    if (arg::at("locale")) {
      log({"Adding System Locale"});
      batch(share, command, {
        "/etc/locale.conf", "/etc/localtime",
        "/usr/share/zoneinfo", "/usr/share/X11/locale", "/usr/share/locale",
        config + "/plasma-localerc"
      }, "ro-bind");
      libraries::directories.emplace("/usr/lib/locale");
      if (bin) binaries::parse(binaries, "locale", libraries);
    }

    if (shared.contains("none")) command.emplace_back("--unshare-all");
    else if (!shared.contains("all")) {
      for (const auto& ns : arg::valid("share")) {
        if (ns == "all" || ns == "none") continue;
        if (!shared.contains(ns)) command.emplace_back("--unshare-" + ns);
      }
    }
    if (shared.contains("net")) {
      log({"Adding Networking"});
      batch(share, command, {
        "/etc/gai.conf", "/etc/hosts.conf", "/etc/hosts", "/etc/nsswitch.conf", "/etc/resolv.conf",
        "/etc/gnutls", "/etc/ca-certificates", "/usr/share/ca-certificates",
        "/etc/pki", "/usr/share/pki",
        "/etc/ssl", "/usr/share/ssl",
      }, "ro-bind");
      if (update_sof) libraries::get(libraries, "libnss*");
    }
    if (!shared.contains("user")) extend(command, {"--disable-userns", "--assert-userns-disabled"});

    // Parse system directories.
    if (sys_dirs.contains("dev")) extend(command, {"--dev", "/dev"});
    else batch(share, command, arg::list("devices"), "dev-bind");

    if (sys_dirs.contains("proc")) extend(command, {"--proc", "/proc"});
    if (sys_dirs.contains("etc")) extend(command, {"--overlay-src", "/etc", "--tmp-overlay", "/etc"});
    if (sys_dirs.contains("share")) extend(command, {"--overlay-src", "/usr/share", "--tmp-overlay", "/usr/share"});
    if (sys_dirs.contains("var")) extend(command, {"--overlay-src", "/var", "--tmp-overlay", "/var"});

    // Application directories.
    if (app_dirs.contains("lib") && lib) libraries::directories.emplace("/usr/lib/" + program);
    if (app_dirs.contains("share")) share(command, "/usr/share/" + program);
    if (app_dirs.contains("etc")) share(command, "/etc" + program);
    if (app_dirs.contains("opt")) libraries::directories.emplace("/opt/" + program);

    // Setup binaries and symlinks.
    if (bin) binaries::setup(binaries, command);
    binaries::symlink(command);

    if (update_sof) {
      log({"Constructing SOF"});
      for (const auto& dir : libraries::directories)
        libraries::get(libraries, dir);

      // Because updating the SOF merely writes to the SOF directory, we can freely detach it
      // into another thread, and then call pool.wait() just before running the program.
      pool.detach_task([program = std::move(program), libraries = std::move(libraries)]() {
            libraries::resolve(libraries, program, arg::hash);
        });
    }
    else log({"SOF Satisfied"});
    if (lib) extend(command, {"--overlay-src", lib_dir.string(), "--tmp-overlay", "/usr/lib"});


    // Symlink and share.
    libraries::symlink(command);
    batch(share, command, libraries::directories, "ro-bind");

    // Write out the cache.

    auto out = std::ofstream(c_cache);
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
