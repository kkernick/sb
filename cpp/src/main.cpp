#include "arguments.hpp"
#include "generators.hpp"
#include "shared.hpp"
#include "syscalls.hpp"
#include "libraries.hpp"

#include <cstring>
#include <filesystem>
#include <sys/wait.h>

using namespace shared;
using namespace exec;
namespace fs = std::filesystem;

// The WD of the proxy
int proxy_pid = -1;

bool kdialog = fs::exists("/usr/bin/kdialog");

// Cleanup children.
static void child_handler(int sig) {
    pid_t pid;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0);
}


// Cleanup the sandbox.
static void cleanup(int sig) {
  // Please die.
  if (proxy_pid != -1) {
    kill(proxy_pid, sig == SIGTERM ? SIGKILL : SIGTERM);
    while (wait(NULL) != -1 || errno == EINTR);
  }

  if (arg::get("fs") == "persist") {
    auto path = arg::mod("fs");
    execute<void>({"find", path, "-type", "l", "-exec", "rm", "-f", "{}", ";"}, wait_for, {.verbose = arg::at("verbose") >= "debug"});

    vector files;
    do {
      files = execute<vector>({"find", path, "-empty"}, vectorize, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
      pool.submit_loop(0, files.size(), [&files](size_t x) {fs::remove(files[x]);}).wait();
    } while (!files.empty());


    execute<void>({"find", path, "-type", "f", "-empty", "-exec", "rm", "-f", "{}", ";"}, wait_for, {.verbose = arg::at("verbose") >= "debug"});
    for (const auto& junk : {"/dev", "/sys", "/run"}) {
      if (fs::exists(path + junk))
        fs::remove_all(path + junk);
    }
  }

  auto sof = arg::get("sof");
  if (fs::exists(sof + "/sb.lock")) fs::remove(sof + "/sb.lock");

  if (arg::at("encrypt") && !arg::list("encrypt").contains("persist")) {
    log({"Unmounting encrypted root at", app_data.string()});
    std::string stderr;
    do {
      stderr = execute<std::string>({"fusermount", "-u", app_data.string()}, dump, {.cap = STDERR, .verbose = arg::at("verbose") >= "debug"});
    } while (!stderr.empty());
  }
}

void warning(const list& msg, const bool& error = true) {
  if (kdialog)
    exec::execute<void>({"kdialog", "--title", arg::get("cmd"), "--error", join<list>(msg, ' ')}, wait_for);
  else {
    if (error) std::cerr << "ERROR: ";
    else std::cerr << "WARN: ";
    for (const auto& x : msg) std::cerr << x << ' ';
    std::cerr << std::endl;
  }
  if (error) {
    cleanup(0);
    exit(0);
  }
}


// Main
int main(int argc, char* argv[]) {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = child_handler;
  sigaction(SIGCHLD, &sa, NULL);

  sa.sa_handler = cleanup;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);

  // Parse those args.
  arg::args = vector(argv + 1, argv + argc);
  auto parse_args = []() {
    if (arg::args.size() > 0 && arg::args[0].ends_with(".sb")) {
      auto program = arg::args[0];
      auto resolved = fs::exists(arg::args[1]) ? program : execute<std::string>({"which", program}, one_line, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
      auto contents = file::parse<vector>(resolved, vectorize);
      if (contents.size() == 2) {
        auto old = arg::args;
        arg::args = container::init<vector>(container::split<vector, char>, contents[1], ' ', false);

        // Remove the sb
        arg::args.erase(arg::args.begin());

        for (auto& arg : arg::args) arg = trim<std::string_view>(arg, "'\"");

        auto arg_spot = std::find(arg::args.begin(), arg::args.end(), "$@");
        if (arg_spot != arg::args.end()) arg::args.erase(arg_spot);

        set ignore = {"sb", program, "--command", "-C"};
        for (const auto& arg : old) {
          if (!ignore.contains(arg))
            arg::args.emplace_back(trim<std::string_view>(arg, "'\""));
        }
      }
    }
    try {
      arg::parse_args();
    }
    catch (std::runtime_error& e) {
      warning({"Failed to parse arguments", e.what()});
    }
  };
  profile("Argument Parser", parse_args);

  if (!arg::at("cmd")) {
    warning({"No program provided!"});
  }

  auto program = fs::path(arg::get("cmd")).filename().string();
  auto lib_cache = libraries::hash_cache(program, arg::hash);
  auto lib_sof = libraries::hash_sof(program, arg::hash);
  bool startup = arg::at("startup") && arg::at("dry_startup");

  app_data = fs::path(data) / "sb" / program;
  try {
    if (arg::at("encrypt") || fs::exists(app_data / "gocryptfs.diriv")) {
      // We won't prompt for a password on startup, nor refresh it in batch.
      if (arg::at("startup") || arg::mod("update") == "batch") return 0;

      arg::emplace("encrypt", "true");
      generate::encrypted(program);
    }
    else app_data = data_dir / program;
  }
  catch (const std::runtime_error& e) {
    log({e.what()});
    cleanup(0);
    return 0;
  }

  auto app_sof = fs::path(arg::get("sof")) / program;
  fs::create_directories(app_sof);
  auto work_dir = TemporaryDirectory(app_sof);

  if (
    fs::exists(lib_cache) &&
    !fs::is_empty(lib_cache) &&
    !arg::at("update") &&
    !fs::is_directory(lib_sof)
  ) {
    log({"Using cached SOF"});
    libraries::resolve(file::parse<vector>(lib_cache, fd_splitter<vector, ' '>), program, arg::hash);
    auto [dir, future] = generate::proxy_lib();
    if (startup) future.wait();
  }

  // Just populate the cache and exit.
  if (startup) {
    cleanup(0);
    return 0;
  }

  // Create a script file and exit.
  if (arg::at("script")) {
    auto binary = fs::path(home) / ".local" / "bin" / (program + ".desktop.sb");
    generate::script(binary);
    cleanup(0);
    return 0;
  }

  // Create a desktop file and exit.
  if (arg::at("desktop_entry")) {
    std::string desktop_path;
    if (arg::get("desktop_entry") == "true") desktop_path = program + ".desktop";
    else desktop_path = arg::get("desktop_entry");
    generate::desktop_entry(desktop_path);
    cleanup(0);
    return 0;
  }

  if (arg::at("update") >= "cache") {

    // So sb-refresh can reuse caches populated by other sandboxes.
    // We don't need to repeat the work.
    if (arg::mod("update") != "batch")
      fs::remove_all(fs::path(data) / "sb" / "cache");

    try {
      fs::remove_all(fs::path(data) / "sb" / program / "cache");
    }
    catch (fs::filesystem_error&) {}

    if (arg::get("update") == "clean")
      fs::remove_all(fs::path(arg::get("sof")) / program / "lib");
    else fs::remove_all(libraries::hash_sof(program, arg::hash));

    if (arg::mod("update") == "exit") {cleanup(0); return 0;}
  }

  // Start getting the filter in another thread.
  std::future<std::string> seccomp_future = pool.submit_task([&program]() -> std::string {return syscalls::filter(program);});

  // Initialize inotify
  inotify = inotify_init();
  if (inotify == -1) warning({"Failed to initialize inotify: ", strerror(errno)});


  // Create the ld.so.preload to ensure hardened malloc is enforced.
  if (arg::at("hardened_malloc")) {
      if (!fs::exists("/usr/lib/libhardened_malloc.so"))
        warning({"Install hardened malloc!"});
      auto preload = std::ofstream(work_dir.sub("ld.so.preload"));
      if (arg::get("hardened_malloc") == "light")
        preload << "/usr/lib/libhardened_malloc-light.so";
      else
        preload << "/usr/lib/libhardened_malloc.so";
      preload.close();
  }

  // Defer instance dir to only when we actually need it.
  auto instance_dir = TemporaryDirectory(runtime + "/.flatpak", program, "", true);
  std::pair<int, std::future<int>> proxy_pair;

  if (arg::list("portals").empty() && arg::list("see").empty() && arg::list("talk").empty() && arg::list("own").empty()) {
    log({"Application does not use portals"});

    // Elegant? Hell no, but we NEED to cart around the proxy's future, otherwise we would need to wait for the
    // program to launch within this scope. If that requires a dud async call so that we don't get invalid futures,
    // then so be it.
    proxy_pair = {-1, std::async(std::launch::async, []() {return -1;})};
  }
  else {
    instance_dir.create();
    log({"Initializing xdg-dbus-proxy..."});
    generate::flatpak_info(program, instance_dir.get_name(), work_dir);
    proxy_pair = generate::xdg_dbus_proxy(program, work_dir);
  }

  // The main program command.
  vector command;
  command.reserve(300);
  extend(command, {"bwrap", "--new-session", "--die-with-parent", "--clearenv", "--unshare-uts"});

  if (!arg::at("hostname")) {
    extend(command, {"--hostname", "sandbox"});
  }

  // If we are using a local file system, attach it.
  if (arg::at("fs")) {
    const auto path = arg::mod("fs");
    fs::create_directories(path);

    // Mount it.
    if (arg::get("fs") == "cache") extend(command, {"--overlay-src", arg::mod("fs"), "--tmp-overlay", "/"});
    else extend(command, {"--bind", arg::mod("fs"), "/"});

    if (fs::is_directory(path + "/tmp")) extend(command, {"--overlay-src", arg::mod("fs") + "/tmp", "--tmp-overlay", "/tmp"});
    else extend(command, {"--tmpfs", "/tmp"});
  }
  else extend(command, {"--tmpfs", "/tmp"});
  extend(command, {"--tmpfs", "/home/sb/.cache"});

  // Mount *AFTER* the root file system has been overlain to prevent it being hidden.
  if (std::get<0>(proxy_pair) != -1) {
    extend(command, {
      "--ro-bind", work_dir.sub(".flatpak-info"), "/.flatpak-info",
      "--symlink", "/.flatpak-info", "/run/user/" + real + "/flatpak-info"
    });
  }

  // Add environment variables.
  batch(genv, command, {"XDG_RUNTIME_DIR", "XDG_CURRENT_DESKTOP", "DESKTOP_SESSION"});

  for (const auto& env : arg::list("env")) {
    auto split = container::init<vector>(container::split<vector, char>, env, '=', false);
    if (split.size() == 2)
      extend(command, {"--setenv", split[0], split[1]});
  }

  // If we are using a shell, create a dummy passwd file.
  if (arg::at("shell")) {
    const auto passwd = work_dir.sub("passwd");
    auto out =  std::ofstream(passwd);
    out << "[Application]\n" << "sb:x:" << real << ":" << real << ":SB:/home/sb:/usr/bin/sh\n";
    out.close();
    extend(command, {
      "--ro-bind", passwd, "/etc/passwd",
      "--setenv", "SHELL", "/usr/bin/sh",
    });
  }

  // Hardened malloc.
  if (arg::at("hardened_malloc")) extend(command, {"--ro-bind", work_dir.sub("ld.so.preload"), "/etc/ld.so.preload"});

  // Various environment variables; we don't want to cache these.
  if (arg::get("qt") == "kf6") batch(genv, command, {"KDE_FULL_SESSION", "KDE_SESSION_VERSION"});
  if (arg::at("gui")) batch(genv, command, {"XDG_SESSION_DESKTOP", "WAYLAND_DISPLAY"});
  if (arg::at("locale")) batch(genv, command, {"LANG", "LANGUAGE", "LC_ALL"});

  // Setup the bwrap json output, and mount for portals.
  int bwrap_fd = -1;
  if (std::get<0>(proxy_pair) != -1) {
    bwrap_fd = creat(instance_dir.sub("bwrapinfo.json").c_str(), S_IRUSR | S_IWUSR | S_IROTH);
    if (bwrap_fd == -1) warning({"Failed to create bwrapinfo: ", strerror(errno)});
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

  // Add strace if we need to.
  if ((arg::get("seccomp") == "strace") && arg::at("verbose") < "error")
    arg::emplace("verbose", "error");

  auto generate_command = [&program, &command]() {
    try {
      auto next = generate::cmd(program);
      command.insert(command.end(), next.begin(), next.end());
    }
    catch (std::exception& e) {
      warning({"Failed to generate command: ", e.what()});
    }
  };
  profile("Command Generation", generate_command);


  //Passthrough any and all files.
  vector post = {};
  auto passthrough = [&command](const std::string& path, const std::string& policy) {
    if (!fs::exists(path)) return;
    else if (arg::at("file_passthrough") || !arg::list("files").empty()) {

      // RO/RW are bound.
      if (policy == "ro") command.emplace_back("--ro-bind");
      else if (policy == "rw") command.emplace_back("--bind");

      // Discard, which only applies on folders, puts a TMPFS overlay to make directories appear
      // writable within the sandbox, but to which said changes are discarded.
      else if (policy == "discard") {
        if (!fs::is_directory(path)) warning({"Discard policy can only apply to directories!"});
        extend(command, {"--overlay-src", path, "--tmp-overlay", "/enclave" + path});
        return;
      }

      else if (policy == "false") warning({"--file-passthrough must be set if --files does not use modifiers!"});

      // Otherwise complain if the policy is invalid.
      else warning({"Invalid policy for ", path, ":", policy});

      // Give it within the enclave.
      extend(command, {path, "/enclave/" + path});
    }
    else warning({"Please provide a passthrough mode!"});

  };

  // The files argument supports modifiers for fine-grained policy.
  // This means that we can specify ~/files:ro. If a modifier is not present,
  // it defaults to the --file-passthrough value, which can merely be false.
  // Therefore, either every path needs a modifier ,or --file-passthrough
  // needs to be set to provide a default.
  for (const auto& [val, mod] : arg::modlist("files")) {
    if (mod.empty()) passthrough(val, arg::get("file_passthrough"));
    else if (mod == "do") share(command, {val});
    else if (mod == "dw") share(command, {val}, "bind");
    else passthrough(val, mod);
  }

  // Unknown arguments do not get parsed.
  for (const auto& arg : arg::unknown) {
    if (fs::exists(arg)) {
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

  int xephr = -1;
  if (arg::at("xorg")) {
    try {
      auto pair = generate::xorg();
      auto display = pair.first;
      xephr = pair.second;
      extend(command, {"--setenv", "DISPLAY", display});
    }
    catch (std::runtime_error& e) {warning({"Failed to setup xorg: ", e.what()});}
  }

  // Final command args. Debug Shell replaces the actual app
  if (arg::at("stats")) {
    extend(command, {"fd", ".", "/", "-E", "run", "-H"});
  }
  else if (arg::get("shell") == "debug") command.emplace_back("sh");
  else {
    // Strace just prepends itself before running the command.
    // --shell=debug --strace will just debug shell; why would you
    // need to strace the debug shell?
    if (arg::at("verbose") >= "error") {
      extend(command, {"strace", "-ffy"});
      if (arg::at("verbose") < "strace") command.emplace_back("-Z");

    }
    command.emplace_back(arg::get("cmd"));
    extend(command, post);

    // Add flags
    if (!arg::at("no-flags")) {
      auto flags = app_data / "flags.conf";
      if (fs::exists(flags)) container::split(command, file::parse<std::string>(flags.string(), dump), " \n");
    }
  }

  // Do this before our auxiliary wait.
  if (std::get<0>(proxy_pair) != -1) {
    try {
      proxy_pid = std::get<1>(proxy_pair).get();
    }
    catch (std::future_error&) {}
  }

  // Wait for any tasks in the pool to complete.
  auto task_profile = []() {
    log({"Waiting for auxiliary tasks to complete.."});
    pool.wait();
  };
  profile("Auxiliary tasks", task_profile);

  if (!arg::at("dry")) {
    // Wait for the proxy to setup.
    auto wait = [&proxy_pair]() {
      if (std::get<0>(proxy_pair) != -1) {
        log({"Waiting for Proxy..."});
        inotify_wait(std::get<0>(proxy_pair));
      }
    };
    profile("Exclusive Proxy Setup", wait);

    if (arg::at("stats")) {
      auto files = execute<vector>(command, vectorize, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
      auto total = execute<vector>({"fd", ".", "/", "-E", "run", "-H"}, vectorize, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
      double start = total.size(), final = files.size();
      std::cout << "Reduced Files By: " << ((start - final) / fabs(start)) * 100.0f << "%"
                 << " (" << start << " to " << final << ")" << std::endl;
    }

    else {
      // If there's a post command, the main command is non-blocking,
      // we launch the post command, and wait on that.
      if (arg::at("post")) {

        // Run the sandbox non-blocking
        execute(command);

        // Assemble the post-command; args are provided in the modifier.
        command = {arg::get("post")};
        container::split(command, arg::mod("post"), ' ');
      }

      if (arg::get("seccomp") == "strace")
        syscalls::update_policy(program, execute<vector>(command, vectorize, {.cap = STDERR, .verbose = arg::at("verbose") >= "debug"}));
      else execute<void>(command, wait_for, {.verbose = arg::at("verbose") >= "debug"});
    }
  }

  // Cleanup FD.
  if (seccomp_fd != -1) close(seccomp_fd);
  if (bwrap_fd != -1) close(bwrap_fd);

  if (xephr != -1) kill(xephr, SIGTERM);

  #ifdef PROFILE
  for (const auto& [key, value] : time_slice)
    std::cout << key << ": " << value << "us (" << (float(value) / float(time_slice["total"])) * 100 << "%)" << std::endl;
  #endif

  cleanup(0);
}
