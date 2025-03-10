#include <array>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <random>
#include <fstream>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>
#include <blake2.h>

#include "shared.hpp"
#include "arguments.hpp"

namespace shared {

  // Init our shared values.
  BS::thread_pool<BS::tp::none> pool = {};
  int inotify = -1;
  std::random_device TemporaryDirectory::dev = {};
  std::mt19937 TemporaryDirectory::prng(TemporaryDirectory::dev());
  std::uniform_int_distribution<uint64_t> TemporaryDirectory::rand(0);


  int null_fd = open("/dev/null", O_WRONLY);

  // Environment variables.
  const std::string
    runtime = std::getenv("XDG_RUNTIME_DIR"),
    config = std::getenv("XDG_CONFIG_HOME"),
    cache = std::getenv("XDG_CACHE_HOME"),
    data = std::getenv("XDG_DATA_HOME"),
    home = std::getenv("HOME"),
    session = std::getenv("DBUS_SESSION_BUS_ADDRESS"),
    nobody = std::to_string(getpwnam("nobody")->pw_uid),
    real = std::to_string(getuid());


  // Check if a path exists.
  bool exists(const std::string& path) {
    struct stat buffer;
    return (stat (path.c_str(), &buffer) == 0);
  }

  // Check if path is a directory
  bool is_dir(const std::string& path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) return 0;
    return S_ISDIR(statbuf.st_mode);
  }

  // Check if a path is a file
  bool is_file(const std::string& path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) return 0;
    return S_ISREG(statbuf.st_mode);
  }

  // Check if a path is a link
  bool is_link(const std::string& path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) return 0;
    return S_ISLNK(statbuf.st_mode);
  }

  // Get the basename of the path
  std::string basename(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return path;
    else return path.substr(pos + 1);
  }

  // Get the dirname of the path
  std::string dirname(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    else return path.substr(0, pos);
  }

  // Read a file
  std::string read_file(const std::string& path) {
    std::string contents;
    auto file = std::ifstream(path);
    if (file.is_open()) {
      file.seekg(0, std::ios::end);
      contents.resize(file.tellg());
      file.seekg(0, std::ios::beg);
      file.read(&contents[0], contents.size());
      file.close();
    }
    return contents;
  }

  // Construct a path
  std::string mkpath(const std::vector<std::string>& p) {
    auto ret = join(p, '/');
    if (!ret.ends_with('/')) ret += '/';
    std::filesystem::create_directories(ret);
    return ret;
  }

  // Log to output
  void log(const std::vector<std::string>& msg, const std::string& level) {
    if (arg::at("verbose").meets(level)) {
      for (const auto& x : msg)
        std::cout << x << ' ';
      std::cout << std::endl;
    }
  }

  // Execute a command.
  std::string exec(const std::vector<std::string>& cmd, exec_return policy) {
      log({"EXEC:", join(cmd, ' ')}, "debug");

      // Create our pipe to talk over
      int pipefd[2];
      if (pipe(pipefd) == -1) throw std::runtime_error(std::string("Failed to setup pipe: ") + strerror(errno));

      // For!
      auto pid = fork();
      if (pid < 0) throw std::runtime_error(std::string("Failed to fork: ") + strerror(errno));

      else if (pid == 0) {

        // The child creates a command array for execvp
        std::vector<const char*> argv; argv.reserve(cmd.size() + 1);
        for (const auto& v : cmd) argv.emplace_back(v.c_str());
        argv.emplace_back(nullptr);


        // Send STDOUT to the parent
        close(pipefd[0]);
        switch (policy) {
          case STDOUT: case ONLY_STDOUT: dup2(pipefd[1], STDOUT_FILENO); break;
          case STDERR: case ONLY_STDERR: dup2(pipefd[1], STDERR_FILENO); dup2(null_fd, STDOUT_FILENO); break;
          case ALL:  dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); break;
          default: break;
        }
        if (policy == ONLY_STDOUT)  dup2(null_fd, STDERR_FILENO);
        else if (policy == ONLY_STDERR)  dup2(null_fd, STDOUT_FILENO);

        close(pipefd[1]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
      }

      // Parent either returns immediately.
      std::string result;
      if (policy != exec_return::NONE) {

        // Or reads from the pipe until the process quits.
        close(pipefd[1]);

        // Use a 256 buffer to append to the final result.
        std::array<char, 256> buffer = {};
        size_t bytes = 0;
        try {
          do {
            bytes = read(pipefd[0], &buffer, 255);
            result.append(buffer.data(), bytes);
          } while (bytes > 0);
        }
        catch (std::length_error&) {};
        kill(pid, SIGKILL);
      }
      else {
        close(pipefd[1]);
      }

      close(pipefd[0]);
      return trim(result, "\0");
  }


  // Split the string.
  std::vector<std::string> split(const std::string& str, const std::string& delims) {
    std::vector<std::string> ret;
    for (size_t r_bound = 0, l_bound = 0; r_bound <= str.length(); ++r_bound) {
      if (delims.contains(str[r_bound])|| r_bound == str.length()) {
        if (r_bound != l_bound) ret.emplace_back(str.substr(l_bound, r_bound - l_bound));
        l_bound = r_bound + 1;
      }
    }
    return ret;
  }
  std::set<std::string> unique_split(const std::string& str, const std::string& delims) {
    std::set<std::string> ret;
    for (size_t r_bound = 0, l_bound = 0; r_bound <= str.length(); ++r_bound) {
      if (delims.contains(str[r_bound]) || r_bound == str.length()) {
        if (r_bound != l_bound) ret.emplace(str.substr(l_bound, r_bound - l_bound));
        l_bound = r_bound + 1;
      }
    }
    return ret;
  }

  // Join a vector into a string.
  template <class T> std::string join(const T& list, const char& joiner) {
    if (list.empty()) return "";
    std::stringstream in;
    for (const auto& x : list)
      in << x << joiner;
    auto str = in.str();
    return str.erase(str.length() - 1);
  }
  template std::string join(const std::vector<std::string>&, const char&);
  template std::string join(const std::set<std::string>&, const char&);


  std::string strip(const std::string& in, const std::string& to_strip) {
    std::stringstream ret;
    for (const auto& x : in)
      if (!to_strip.contains(x)) ret << x;
    return ret.str();
  }

  std::string trim(const std::string& in, const std::string& to_strip) {
    if (in.empty()) return in;

    size_t l = 0, r = in.length() - 1;
    while (l < in.length() && to_strip.contains(in[l])) ++l;
    while (r > 0 && to_strip.contains(in[r])) --r;
    return in.substr(l, r - l + 1);
  }

  std::set<std::string> wildcard(const std::string& pattern, const std::string& path, const std::vector<std::string>& args) {
    std::vector<std::string> command;
    command.reserve(4 + args.size());

    auto local = pattern.contains('/');

    command.insert(command.end(), {"find", local ? dirname(pattern) : path});
    command.insert(command.end(), args.begin(), args.end());
    command.insert(command.end(), {"-name", local ? basename(pattern) : pattern});
    return shared::unique_split(exec(command), "\n");
  };

  std::string escape(const std::string& in) {return "'" + in + "'";}

  void genv(std::vector<std::string>& command, const std::string& env) {
    if (std::getenv(env.c_str()) != nullptr)
     extend(command, {"--setenv", env, std::getenv(env.c_str())});
    else log({"Missing environment variable:", env});
  }

  void genvs(std::vector<std::string>& command, const std::vector<std::string>& envs) {
   for (const auto& env : envs) genv(command, env);
  }

  void extend(std::vector<std::string>& dest, const std::vector<std::string>& source) {
    dest.reserve(dest.size() + distance(source.begin(), source.end()));
    dest.insert(dest.end(), source.begin(), source.end());
  }

  void inotify_wait(const int& wd, const std::string& name) {
    char buffer[sizeof(struct inotify_event) + PATH_MAX + 1] = {0};
    while (true) {
      auto len = read(inotify, &buffer, sizeof(struct inotify_event) + PATH_MAX + 1);
      if (len <= 0) throw std::runtime_error("Failed to read from inotify FD");
      auto* event =  reinterpret_cast<inotify_event*>(&buffer);
      if ((name.empty() || std::string(event->name) == name) && event->wd == wd) {
        inotify_rm_watch(inotify, wd);
        return;
      }
    }
  }

  template <class T> void share(std::vector<std::string>& command, const T& paths, const std::string& mode) {
    std::vector<std::string> constructed; constructed.reserve(3 * paths.size());
    for (const auto& path : paths) {
      if (exists(path)) {extend(constructed,
          { "--" + mode,
            path,
            path.starts_with(home) ? "/home/sb" + path.substr(home.length()) : path
          });
      }
    }
    extend(command, constructed);
  }
  template void share(std::vector<std::string>&, const std::vector<std::string>&, const std::string&);
  template void share(std::vector<std::string>&, const std::set<std::string>&, const std::string&);

  void merge(std::set<std::string>& command, std::set<std::string> path) {command.merge(path);}

  std::string hash(const std::string& in) {
    const size_t hash_length = BLAKE2B_OUTBYTES;
    uint8_t hash[hash_length] = {0};

    // Call blake2b to hash the input string
    int result = blake2b(hash, in.c_str(), nullptr, hash_length, in.length(), 0);
    if (result == 0) {
      std::stringstream hex_hash;
      hex_hash << std::hex << std::setfill('0');
      for (size_t i = 0; i < hash_length; ++i)
        hex_hash << std::setw(2) << static_cast<unsigned>(hash[i]);
      return hex_hash.str();
    }
    else throw std::runtime_error("Failed to hash string!");
  }
}
