#include "arguments.hpp"
#include "generators.hpp"
#include "shared.hpp"
#include "syscalls.hpp"

#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/wait.h>

using namespace shared;

std::map<std::string, size_t> time_slice;

template <typename T, typename... Args> inline void profile(const std::string& name, T& func, Args... args) {
  auto begin = std::chrono::high_resolution_clock::now();
  func(args...);
  auto duration = duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - begin).count();
  time_slice["total"] += duration;
  time_slice[name] = duration;
  log({name, ":", std::to_string(duration), "us"});
}


void cleanup(int signo) {
  // For for children to die.
  for (const auto& pid : children) {
    kill(pid, SIGTERM);
  }
}

int main(int argc, char* argv[]) {
  signal(SIGABRT,cleanup);
  signal(SIGINT,cleanup);
  signal(SIGSEGV,cleanup);
  signal(SIGTERM,cleanup);

  // It seems we still must ignore chidlren lest the benchmarker make zombies.
  signal(SIGCHLD, SIG_IGN);

  // Parse those args.
  arg::args = std::vector<std::string>(argv + 1, argv + argc);
  auto parse_args = []() {
    if (arg::args.size() > 0 && arg::args[0].ends_with(".sb")) {
      auto program = arg::args[0];
      auto resolved = exists(arg::args[1]) ? program : split(exec({"which", program}), "\n")[0];
      auto contents = split(read_file(resolved), "\n");
      if (contents.size() == 2) {
        auto old = arg::args;
        arg::args = split(contents[1], " ");

        // Remove the sb
        arg::args.erase(arg::args.begin());

        for (auto& arg : arg::args) arg = trim(arg, "'\"");

        auto arg_spot = std::find(arg::args.begin(), arg::args.end(), "$@");
        if (arg_spot != arg::args.end()) arg::args.erase(arg_spot);

        std::set<std::string> ignore = {"sb", program, "--command", "-C"};
        for (const auto& arg : old) {
          if (!ignore.contains(arg))
            arg::args.emplace_back(trim(arg, "'\""));
        }
      }
    }
    arg::parse();
  };
  profile("Argument Parser", parse_args);

  auto program = basename(arg::get("cmd"));


  // Create a script file and exit.
  if (arg::at("script")) {
    auto binary = home + "/.local/bin/" + program + ".desktop.sb";
    generate::script(binary);
    exit(0);
  }

  // Create a desktop file and exit.
  if (arg::at("desktop_entry")) {
    std::string desktop_path;
    if (arg::get("desktop_entry") == "true") desktop_path = program + ".desktop";
    else desktop_path = arg::get("desktop_entry");
    generate::desktop_entry(desktop_path);
    exit(0);
  }

  // Start getting the filter in another thread.
  std::future<std::string> seccomp_future = pool.submit_task([&program]() -> std::string {return syscalls::filter(program);});


  // Initialize inotify
  inotify = inotify_init();
  if (inotify == -1) throw std::runtime_error(std::string("Failed to initialize inotify: ") + strerror(errno));

  // Get the application's directories.
  auto app_sof = mkpath({arg::get("sof"), program});
  auto local_dir = mkpath({data, "sb", program});
  auto work_dir = TemporaryDirectory(app_sof);

  // Create the ld.so.preload to ensure hardened malloc is enforced.
  if (arg::at("hardened_malloc")) {
      if (!exists("/usr/lib/libhardened_malloc.so"))
        throw std::runtime_error("Installed hardened malloc!");
      auto preload = std::ofstream(work_dir.sub("ld.so.preload"));
      preload << "/usr/lib/libhardened_malloc.so";
      preload.close();
  }

  // Defer instance dir to only when we actually need it.
  auto instance_dir = TemporaryDirectory(mkpath({runtime, ".flatpak"}), program, "", true);
  int proxy_wd = -1;
  if (arg::list("portals").empty() && arg::list("see").empty() && arg::list("talk").empty() && arg::list("own").empty())
    log({"Application does not use portals"});
  else {
    instance_dir.create();
    log({"Initializing xdg-dbus-proxy..."});
    generate::flatpak_info(program, basename(instance_dir.get_path()), work_dir);
    if (!arg::at("dry")) proxy_wd = generate::xdg_dbus_proxy(program, work_dir);
  }

  // The main program command.
  std::vector<std::string> command = {"bwrap", "--new-session", "--die-with-parent", "--clearenv", "--unshare-uts"};

  if (!arg::at("hostname")) {
    extend(command, {"--hostname", "sandbox"});
  }

  // If we are using a local file system, attach it.
  if (arg::at("fs")) {
    const auto path = arg::mod("fs");
    std::filesystem::create_directories(path);

    // Mount it.
    if (arg::get("fs") == "cache") extend(command, {"--overlay-src", arg::mod("fs"), "--tmp-overlay", "/"});
    else extend(command, {"--bind", arg::mod("fs"), "/"});

    if (is_dir(path + "/tmp")) extend(command, {"--overlay-src", arg::mod("fs") + "/tmp", "--tmp-overlay", "/tmp"});
    else extend(command, {"--tmpfs", "/tmp"});
  }
  else extend(command, {"--tmpfs", "/tmp", "--tmpfs", "/home/sb/.cache"});

  // Mount *AFTER* the root file system has been overlain to prevent it being hidden.
  if (proxy_wd != -1) {
    extend(command, {
      "--ro-bind", work_dir.sub(".flatpak-info"), "/.flatpak-info",
      "--symlink", "/.flatpak-info", "/run/user/" + real + "/flatpak-info"
    });
  }

  genv(command, "XDG_RUNTIME_DIR");
  genv(command, "XDG_CURRENT_DESKTOP");
  genv(command, "DESKTOP_SESSION");

  // If we are using a shell, create a dummy passwd file.
  if (arg::at("shell")) {
    const auto passwd = work_dir.sub("passwd");
    auto out =  std::ofstream(passwd);
    out << "[Application]\n" << "sb:x:" << real << ":" << "real" << ":SB:/home/sb:/usr/bin/sh\n";
    out.close();
    extend(command, {
      "--ro-bind", passwd, "/etc/passwd",
      "--setenv", "SHELL", "/usr/bin/sh"
    });
  }

  // Hardened malloc.
  if (arg::at("hardened_malloc")) extend(command, {"--ro-bind", work_dir.sub("ld.so.preload"), "/etc/ld.so.preload"});

  // Various environment variables; we don't want to cache these.
  if (arg::get("qt") == "kf6") genvs(command, {"KDE_FULL_SESSION", "KDE_SESSION_VERSION"});;
  if (arg::at("gui")) genvs(command, {"XDG_SESSION_DESKTOP", "WAYLAND_DISPLAY"});
  if (arg::at("locale")) genvs(command, {"LANG", "LANGUAGE", "LC_ALL"});

  // Setup the bwrap json output, and mount for portals.
  int bwrap_fd = -1;
  if (proxy_wd != -1) {
    bwrap_fd = creat(instance_dir.sub("bwrapinfo.json").c_str(), S_IRUSR | S_IWUSR | S_IROTH);
    if (bwrap_fd == -1) throw std::runtime_error(std::string("Failed to create bwrapinfo: ") + strerror(errno));
    extend(command, {
      "--dir", runtime,
      "--chmod", "0700", runtime,
      "--ro-bind", work_dir.sub("proxy/bus"), runtime + "/bus",
      "--bind", runtime + "/doc", runtime + "/doc",
      "--ro-bind", "/run/dbus", "/run/dbus",
      "--json-status-fd", std::to_string(bwrap_fd),
    });
    genv(command, "DBUS_SESSION_BUS_ADDRESS");
  }

  // Otherwise make the application run as nobody.
  else extend(command, {"--uid", nobody, "--gid", nobody});

  // Only one instance has control off the SOF to prevent races.
  auto lock_file = app_sof + "sb.lock";
  while (is_file(lock_file)) {
    log({"Another instance of the application is writing to the SOF. Waiting..."});
    auto wd = inotify_add_watch(inotify, lock_file.c_str(), IN_DELETE_SELF);
    inotify_wait(wd);
  }

  // Add strace if we need to.
  if (arg::get("seccomp") == "strace" && arg::at("verbose").under("errors")) arg::get("verbose") = "errors";

  // Lock and Generate
  auto generate_command = [&lock_file, &program, &command]() {
    std::ofstream lock(lock_file);
    try {
      auto next = generate::cmd(program);
      command.insert(command.end(), next.begin(), next.end());
    }
    catch (std::exception& e) {
      std::cerr << "Failed to generate command: " << e.what() << std::endl;
      std::filesystem::remove(lock_file);
      exit(1);
    }
    std::filesystem::remove(lock_file);
  };
  profile("Command Generation", generate_command);


  //Passthrough any and all files.
  std::vector<std::string> post = {};
  auto passthrough = [&command](const std::string& path, const std::string& policy) {
    if (arg::at("file_passthrough") || !arg::list("files").empty()) {

      // RO/RW are bound.
      if (policy == "ro") command.emplace_back("--ro-bind");
      else if (policy == "rw") command.emplace_back("--bind");

      // Discard, which only applies on folders, puts a TMPFS overlay to make directories appear
      // writable within the sandbox, but to which said changes are discarded.
      else if (policy == "discard") {
        if (!is_dir(path)) throw std::runtime_error("Discard policy can only apply to directories!");
        extend(command, {"--overlay-src", path, "--tmp-overlay", "/enclave" + path});
        return;
      }

      else if (policy == "false") throw std::runtime_error("--file-passthrough must be set if --files does not use modifiers!");

      // Otherwise complain if the policy is invalid.
      else throw std::runtime_error("Invalid policy: " + policy);

      // Give it within the enclave.
      extend(command, {path, "/enclave/" + path});
    }
    else throw std::runtime_error("Please provide a passthrough mode!");

  };

  // The files argument supports modifiers for fine-grained policy.
  // This means that we can specify ~/files:ro. If a modifier is not present,
  // it defaults to the --file-passthrough value, which can merely be false.
  // Therefore, either every path needs a modifier ,or --file-passthrough
  // needs to be set to provide a default.
  for (const auto& [val, mod] : arg::modlist("files")) {
    if (mod.empty()) passthrough(val, arg::get("file_passthrough"));
    else passthrough(val, mod);
  }

  // Unknown arguments do not get parsed.
  for (const auto& arg : arg::unknown) {
    if (exists(arg)) {
      passthrough(arg, arg::get("file_passthrough"));
      post.emplace_back("/enclave/" + arg);
    }
    else post.emplace_back(arg);
  }

  // Get the seccomp filter and add it.
  auto seccomp_fd = -1;

  auto seccomp_setup = [&seccomp_future, &seccomp_fd, &command]() {
    auto seccomp_filter = seccomp_future.get();
    if (!seccomp_filter.empty()) {
      seccomp_fd = open(seccomp_filter.c_str(), O_RDONLY);
      extend(command, {"--seccomp", std::to_string(seccomp_fd)});
    }
  };
  profile("Exclusive SECCOMP Setup", seccomp_setup);

  // Final command args. Debug Shell replaces the actual app
  if (arg::get("shell") == "debug") command.emplace_back("sh");
  else {
    // Strace just prepends itself before running the command.
    // --shell=debug --strace will just debug shell; why would you
    // need to strace the debug shell?
    if (arg::at("verbose").meets("error")) {
      extend(command, {"strace", "-ffy"});
      if (arg::at("verbose").under("strace")) command.emplace_back("-Z");

    }
    command.emplace_back(arg::get("cmd"));
    extend(command, post);

    // Add flags
    auto flags = local_dir + "/flags.conf";
    if (is_file(flags)) extend(command, split(read_file(flags), " \n"));
  }

  if (!arg::at("dry")) {

    // Wait for the proxy to setup.
    auto wait = [&proxy_wd]() {
      if (proxy_wd != -1) {
        log({"Waiting for Proxy..."});
        inotify_wait(proxy_wd);
      }
    };
    profile("Exclusive Proxy Setup", wait);

    // Wait for any tasks in the pool to complete.
    log({"Waiting for auxiliary tasks to complete.."});
    pool.wait();

    // If there's a post command, the main command is non-blocking,
    // we launch the post command, and wait on that.
    if (arg::at("post")) {

      // Run the sandbox non-blocking
      exec(command, NONE);

      // Assemble the post-command; args are provided in the modifier.
      command = {arg::get("post")};
      extend(command, split(arg::mod("post"), " "));
    }

    if (arg::get("seccomp") == "strace") syscalls::update_policy(program, exec(command, STDERR));
    else exec(command, STDOUT);
  }

  // Cleanup FD.
  if (seccomp_fd != -1) close(seccomp_fd);
  if (bwrap_fd != -1) close(bwrap_fd);

  // Cleanup files
  if (arg::at("fs")) {
    exec({"find", arg::mod("fs"), "-size", "0", "-delete"});
    exec({"find", arg::mod("fs"), "-empty", "-delete"});
    exec({"find", arg::mod("fs"), "-type", "l", "-delete"});

    // These only persist if --fs=persist, but dev sometimes has /dev/null
    // files which can be *massive* if the app crashes. We want to remove those.
    // Given that both /dev and /run are ephemeral directories (/dev is a devfs and
    // /run is a tmpfs, users shoudln't be putting anything in them to begin with).
    if (is_dir(arg::mod("fs") + "/dev"))
      std::filesystem::remove_all(arg::mod("fs") + "/dev");
    if (is_dir(arg::mod("fs") + "/run"))
      std::filesystem::remove_all(arg::mod("fs") + "/run");
  }

  if (arg::at("verbose")) {
  for (const auto& [key, value] : time_slice)
    std::cout << key << ": " << value << "us (" << (float(value) / float(time_slice["total"])) * 100 << "%)" << std::endl;
  }
  cleanup(0);
}
