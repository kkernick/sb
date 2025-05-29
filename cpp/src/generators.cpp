#include "generators.hpp"
#include "arguments.hpp"
#include "binaries.hpp"
#include "libraries.hpp"
#include "shared.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace shared;
using namespace exec;
namespace fs = std::filesystem;

namespace generate {


  std::pair<std::string, int> xorg() {
    if (execute<std::string>({"which", "Xephyr"}, dump, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"}).empty()) {
      throw std::runtime_error("Xephyr not found!");
    }


    auto sockets = execute<set>({"find", "/tmp", "-maxdepth", "1", "-name", ".X*-lock"}, fd_splitter<set, '\n'>,  {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
    size_t display = 0;
    while (true) {
      std::string lock = "/tmp/.X" + std::to_string(display) + "-lock";
      if (!fs::exists(lock)) {
        break;
      }
      ++display;
    }
    auto d = ":" + std::to_string(display);

    vector command = {"Xephyr", "-once"};
    const auto& flags = arg::list("xorg");

    if (arg::at("xsize")) extend(command, {"-screen", arg::get("xsize")});
    else if (flags.contains("fullscreen")) command.push_back("-fullscreen");

    if (flags.contains("resize")) command.push_back("-resizeable");
    if (flags.contains("gl")) extend(command, {
      "-glamor",
      "+iglx",
      "+extension", "GLX",
      "+extension", "COMPOSITE"
    });

    command.push_back(d);
    auto pid = execute<int>(command, get_pid, {.verbose = arg::at("verbose") >= "debug"});
    return {d, pid};
  }

  // Unlock an encrypted FS
  void encrypted(const std::string_view& program) {
    const auto enc_root = fs::path(data) / "sb" / "enc";
    fs::create_directories(enc_root);
    fs::permissions(enc_root, fs::perms::owner_all);

    auto enc_dir = enc_root / program;
    fs::create_directories(enc_dir);
    fs::create_directories(app_data);

    std::string title = "SB++ — " + std::string(program);

  // Decrypt, making a copy of app_data before it gets overwritten.
    const auto& flags = arg::list("encrypt");
    bool create = flags.contains("init");
    if (create && fs::exists(app_data)) {
      if (!fs::exists(app_data / "gocryptfs.diriv")) {
        log({"Migrating existing sandbox directory"});
        fs::rename(app_data, enc_root / (std::string(program) + "-old"));
      }
      else create = false;
    }
    if (create) {
      auto pass = execute<std::string>({
        "kdialog",
        "--title", title,
        "--newpassword", "To setup an encrypted sandbox, enter a password. You will need to enter this password each time you want to run the application.",
        "--desktopfile", std::string(program) + ".desktop",

      }, one_line, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});

      execute<void>({"gocryptfs", "-init", enc_dir.string()}, wait_for, {.in = pass, .verbose = arg::at("verbose") >= "debug"});
      for (auto& c : pass) c = '\0';
    }

    if (fs::is_empty(app_data)) {

      while (true) {
        auto pass = execute<std::string>({
          "kdialog",
          "--title", title,
          "--password", "This sandbox is encrypted. Please enter its decryption password.",
          "--desktopfile", std::string(program) + ".desktop",
        }, one_line, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});

        if (pass.empty()) exit(0);

        auto mount = execute<std::string>({"gocryptfs", enc_dir.string(), app_data.string()}, dump, {STDOUT, pass, arg::at("verbose") >= "debug"});
        if (mount.contains("Filesystem mounted and ready")) break;
      }
    }
    else if (!flags.contains("persist")) {
      execute<void>({
        "kdialog", "--title", title,
        "--desktopfile", std::string(program) + ".desktop",
        "--error", "An instance of the sandbox is already running. Use the persist flag if you want to run more than one instance!"
      },
      wait_for, {.verbose = arg::at("verbose") >= "debug"});
      exit(0);
    }

   if (fs::exists(enc_root / (std::string(program) + "-old"))) {
     std::filesystem::copy(enc_root / (std::string(program) + "-old"), app_data, fs::copy_options::recursive);
     std::filesystem::remove_all(enc_root / (std::string(program) + "-old"));
   }
  }

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
    execute({"chmod", "+x", binary});
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
    auto contents = file::parse<vector>(src, vectorize);
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


  // Setup the Proxy's SOF
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
      if (!std::filesystem::exists(lib_cache) || arg::at("update") >= "libraries") {
        auto cache = libraries::hash_cache("xdg-dbus-proxy", p_hash);
        if (std::filesystem::exists(cache) && !std::filesystem::is_empty(cache)) {
          log({"Using Proxy Cache"});
          libraries::resolve(file::parse<vector>(cache, fd_splitter<vector, ' '>), "xdg-dbus-proxy", p_hash, false);
        }
        else {
          log({"Generating Proxy Cache"});
          libraries::lib_t libraries = {};
          if (arg::get("hardened_malloc") == "true") libraries::get(libraries, "/usr/lib/libhardened_malloc.so");
          else if (arg::get("hardened_malloc") == "light") libraries::get(libraries, "/usr/lib/libhardened_malloc-light.so");

          container::init<binaries::bin_t>(binaries::parse, "/usr/bin/xdg-dbus-proxy", libraries);
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
      if (!arg::at("dry")) return execute<int>(command, get_pid, {.verbose = arg::at("verbose") >= "debug"});
      return -1;
    };

    return {wd,  pool.submit_task(proxy)};
  }


  // Generate the main command.
  vector cmd(const std::string& program) {

    const auto sof_dir = std::filesystem::path(arg::get("sof")) / program;
    const auto cache_dir = app_data / "cache";
    std::filesystem::create_directories(sof_dir);
    std::filesystem::create_directories(cache_dir);

    const auto lib_dir = libraries::hash_sof(program, arg::hash);
    const auto l_cache = cache_dir / (arg::hash + ".lib.cache");
    const auto c_cache = cache_dir / (arg::hash + ".cmd.cache");

    vector command;
    command.reserve(200);

    binaries::bin_t binaries; libraries::lib_t libraries;
    set devices = arg::list("devices");

    // So that we can easily query as unique, and to emplace.
    auto sys_dirs = arg::list("sys_dirs");
    auto shared = arg::list("share");

    const auto& app_dirs = arg::list("app_dirs");
    const bool lib = !sys_dirs.contains("lib"), bin = !sys_dirs.contains("bin");

    bool update_sof = arg::at("update") | !std::filesystem::exists(l_cache) || arg::at("stats");
    if (lib && !update_sof) {
      if (std::filesystem::exists(c_cache)  && !std::filesystem::is_empty(c_cache)) {
        log({"Reusing existing command cache"});
        return file::parse<vector>(c_cache, fd_splitter<vector, ' ', true>);
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
        single_batch(
          binaries::parse, binaries,
          execute<vector>(
            {"find", arg::mod("fs") + "/usr/bin", "-type", "f,l", "-executable"},
            vectorize,
            {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"}
          ),
          libraries);
      }
    }

    log({"Resolving libraries"});
    for (const auto& [lib, mod] : arg::modlist("libraries")) {
      if (mod != "x") {
        if (std::filesystem::is_directory(lib)) libraries::directories.emplace(lib);
        if (update_sof) libraries::get(libraries, lib);
      }
    }

    if (arg::at("stats") && bin)
      binaries::parse(binaries, "fd", libraries);

    // Add strace if we need to.
    if (arg::at("verbose") >= "error" && bin) {
      log({"Adding strace"});
      if (bin) binaries::parse(binaries, "strace", libraries);
    }

    // Add our new home and path.
    extend(command, {"--setenv", "HOME", "/home/sb", "--setenv", "PATH", "/usr/bin"});

    // Spelling
    if (arg::at("spelling")) {
      const auto& engines = arg::list("spelling");
      if (engines.contains("enchant")) {
        log({"Adding enchant"});
        share(command, "/usr/share/enchant-2");
        if (update_sof) {
          libraries::get(libraries, "libenchant*");
          libraries::directories.emplace("/usr/lib/enchant-2");
        }
      }
      if (engines.contains("hunspell")) {
        log({"Adding hunspell"});
        share(command, "/usr/share/hunspell");
        if (update_sof) {
          libraries::get(libraries, "libhunspell*");
        }
      }
    }

    // XDG Open
    if (arg::at("xdg_open")) {
      log({"Adding XDG-Open"});
      if (bin) binaries::parse(binaries, "/usr/bin/sb-open", libraries);
      extend(command, {"--symlink", "/usr/bin/sb-open", "/usr/bin/xdg-open"});
    }

    // Hardened Malloc
    if (arg::at("hardened_malloc") && update_sof) {
      log({"Adding Hardened Malloc"});
      if (arg::get("hardened_malloc") == "light")
        libraries::get(libraries, "/usr/lib/libhardened_malloc-light.so");
      else libraries::get(libraries, "/usr/lib/libhardened_malloc.so");
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

    if (arg::at("nvidia")) {
      sys_dirs.emplace("dev");
      batch(share, command, {
        "/usr/share/nvidia",
        "/usr/share/glvnd",
      }, "ro-bind");

      extend(libraries::directories, {
        "/usr/lib/vdpau",
        "/usr/lib/nvidia",
      });


      if (update_sof) {
        batch(libraries::get, libraries, {
          "libcuda*", "libnvidia*", "libnvc*", "libnvoptix*"
        }, "");
      }

      arg::emplace("gui", "true");
    }

    if (arg::at("electron")) {
      log({"Adding Electron"});
      extend(devices, {"/dev/null", "/dev/urandom", "/dev/shm"});

      if (update_sof) batch(libraries::get, libraries, {"libsoftokn3*", "libfreeblpriv3*"}, "");

      // IE custom.
      if (arg::get("electron") != "true") {
        auto mod = arg::get("electron");
        if (bin) binaries::parse(binaries, "/usr/bin/electron" + mod, libraries);
        libraries::directories.emplace("/usr/lib/electron" + mod);
      }

      if (arg::at("gtk") < "3") arg::emplace("gtk", "3");
      sys_dirs.emplace("proc");
      shared.emplace("user");
    }

    if (arg::at("gtk")) {
      log({"Adding GTK"});
      set features = arg::list("gtk");

      batch(share, command, {
        "/usr/share/gtk", config + "/gtkrc", "/usr/share/glib-2.0",
        "/usr/share/gtk-2.0", home + "/.gtkrc-2.0",
        config + "/gtkrc-2.0", config + "/gtk-2.0"
      }, "ro-bind");

      if (features.contains("3"))  {
        batch(share, command, {
          config + "/gtk-3.0",
          "/usr/share/gtk-3.0",
          "/etc/xdg/gtk-3.0/",
        }, "ro-bind");
        libraries::directories.emplace("/usr/lib/gtk-3.0");
        if (update_sof) batch(libraries::get, libraries, {"libgdk-3*", "libgtk-3*"}, "");
      }

      if (features.contains("4")) {
        batch(share, command, {
          "/usr/share/gtk-4.0",
          config + "/gtk-4.0",
        }, "ro-bind");
        libraries::directories.emplace("/usr/lib/gtk-4.0");
        if (update_sof) batch(libraries::get, libraries, {"libgdk-4*", "libgtk-4*"}, "");
      }

      if (features.contains("webkit")) {
        features.emplace("gir");
        sys_dirs.emplace("proc");

        extend(devices, {"/dev/urandom", "/dev/null"});
        libraries::directories.emplace("/usr/lib/webkitgtk-6.0/");
        if (update_sof) batch(libraries::get, libraries, {"libwebkitgtk*", "libjavascriptcoregtk*"}, "");
      }

      if (features.contains("gdk")) {
        libraries::directories.emplace("/usr/lib/gdk-pixbuf-2.0");
      }

      if (features.contains("sourceview")) {
        share(command, "/usr/share/gtksourceview-5");
        if (update_sof) libraries::get(libraries, "libgtksourceview*");
      }

      if (features.contains("adwaita")) {
        if (update_sof) libraries::get(libraries, "libadwaita*");
      }

      if (features.contains("gst")) {
        features.emplace("gir");
        libraries::directories.emplace("/usr/lib/gstreamer-1.0/");
        if (update_sof) libraries::get(libraries, "libgst*");
      }

      if (features.contains("gir")) {
        share(command, "/usr/share/gir-1.0/");
        libraries::directories.emplace("/usr/lib/girepository-1.0/");
      }

      extend(command, {"--setenv", "GTK_USE_PORTAL", "1", "--setenv", "GTK_A11Y", "none"});
      arg::emplace("gui", "true");
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

      if (version == "5") {
        share(command, "/usr/share/qt");
        libraries::directories.emplace("/usr/lib/qt");
      }

      if (update_sof) {
        libraries::get(libraries, "libQt" + version + "*");
       if (version != "kf6") libraries::get(libraries, "/usr/lib/kf6/kioworker");
      }

      arg::emplace("gui", "true");
      extend(command, {"--setenv", "QT_QPA_PLATFORM", "wayland"});
    }

    if (arg::at("gui")) {
      log({"Adding GUI"});
      devices.emplace("/dev/dri");
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
      if (update_sof) batch(libraries::get, libraries, {"libMesa*", "libGL*", "libEGL*"}, "");

      const auto& flags = arg::list("gui");
      if (flags.contains("vulkan")) {
        log({"Adding Vulkan"});
        batch(share, command, {"/etc/vulkan", "/usr/share/vulkan"}, "ro-bind");
        if (update_sof) batch(libraries::get, libraries, {"libvulkan*", "libVkLayer*"}, "");
      }

      if (flags.contains("vaapi")) {
        log({"Adding VA-API"});
        if (update_sof) batch(libraries::get, libraries, {"libva-drm*", "libva-wayland*", "libva.so*"}, "");
      }
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
    else {
      batch(share, command, devices, "dev-bind");
      stats("/dev", devices.size());
    }

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
    if (bin) {
      binaries::setup(binaries, command);
      stats("/usr/bin", binaries.size());
    }
    binaries::symlink(command);

    if (update_sof) {
      log({"Constructing SOF"});
      for (const auto& dir : libraries::directories)
        libraries::get(libraries, dir);

      // Because updating the SOF merely writes to the SOF directory, we can freely detach it
      // into another thread, and then call pool.wait() just before running the program.
      pool.detach_task([program = std::move(program), libraries = std::move(libraries)]() {
            libraries::resolve(libraries, program, arg::hash);
            stats("/usr/lib", libraries.size());
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
