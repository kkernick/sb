#include <cstring>
#include <pwd.h>
#include <sys/wait.h>
#include <blake2.h>

#include "shared.hpp"
#include "arguments.hpp"

using namespace exec;

namespace shared {

  // Init our shared values.
  BS::thread_pool<BS::tp::wait_deadlock_checks> pool = {};
  int inotify = -1;
  std::random_device TemporaryDirectory::dev = {};
  std::mt19937 TemporaryDirectory::prng(TemporaryDirectory::dev());
  std::uniform_int_distribution<uint64_t> TemporaryDirectory::rand(0);

  #ifdef PROFILE
  std::map<std::string, uint_fast32_t> time_slice;
  #endif


  // Environment variables.
  const std::string
    home = std::getenv("HOME"),
    runtime = std::getenv("XDG_RUNTIME_DIR"),
    session = std::getenv("DBUS_SESSION_BUS_ADDRESS"),

    config = std::getenv("XDG_CONFIG_HOME") == nullptr ? home + "/.config/" : std::getenv("XDG_CONFIG_HOME"),
    cache = std::getenv("XDG_CACHE_HOME") == nullptr ? home + "/.cache/" : std::getenv("XDG_CACHE_HOME"),
    data = std::getenv("XDG_DATA_HOME") == nullptr ? home + "/.local/share/" : std::getenv("XDG_DATA_HOME"),
    nobody = std::to_string(getpwnam("nobody")->pw_uid),
    real = std::to_string(getuid());

  std::filesystem::path data_dir = std::filesystem::path(data) / "sb", app_data = "";

  // Log to output
  void log(const list& msg, const std::string& level) {
    if (arg::at("verbose") >= level) {
      for (const auto& x : msg)
        std::cout << x << ' ';
      std::cout << std::endl;
    }
  }

  bool contains(const char& v, const char& d) {return v == d;}
  bool contains(const char& v, const std::string_view& d) {return d.contains(v);}
  void emplace(std::set<std::string>& working, const std::string_view& val) {working.emplace(val);}
  void emplace(std::vector<std::string>& working, const std::string_view& val) {working.emplace_back(val);}

  // Join a container into a string.
  template <class T> std::string join(const T& list, const char& joiner) {
    if (list.empty()) return "";
    std::stringstream in;
    for (const auto& x : list)
      in << x << joiner;
    auto str = in.str();
    return str.erase(str.length() - 1);
  }
  template std::string join(const vector&, const char&);
  template std::string join(const set&, const char&);


  // Strip all instances of characters from a string
  template <typename T> std::string strip(const std::string_view& in, const T& to_strip) {
    std::stringstream ret;
    for (const auto& x : in) {
      if (!contains(x, to_strip)) ret << x;
    }
    return ret.str();
  }
  template std::string strip(const std::string_view&, const char&);
  template std::string strip(const std::string_view&, const std::string_view&);


  // Trim characters from start and end.
  template <typename T> std::string trim(const std::string& in, const T& to_strip) {
    if (in.empty()) return in;
    size_t l = 0, r = in.length() - 1;
    while (l < in.length() && contains(in[l], to_strip)) ++l;
    while (r > 0 && contains(in[r], to_strip)) --r;
    return in.substr(l, r - l + 1);
  }
  template std::string trim(const std::string&, const char&);
  template std::string trim(const std::string&, const std::string_view&);


  // Resolve wildcards.
  set wildcard(const std::string_view& pattern, const std::string_view& path, const list& args) {
    vector command;
    command.reserve(4 + args.size());

    auto local = pattern.contains('/');

    command.insert(command.end(), {"find", local ? std::filesystem::path(pattern).parent_path().string() : path.data()});
    command.insert(command.end(), args.begin(), args.end());
    command.insert(command.end(), {"-name", local ? std::filesystem::path(pattern).filename().string() : pattern.data()});
    return execute<set>(command, fd_splitter<set, '\n'>, {.cap = STDOUT, .verbose = arg::at("verbose") >= "debug"});
  };


  // Provide the sandbox with an environment variable.
  void genv(vector& command, const std::string_view& env) {
    if (std::getenv(env.data()) != nullptr)
     extend(command, {"--setenv", env, std::getenv(env.data())});
    else log({"Missing environment variable:", env});
  }


  // Extend a vector with a container.
  template <class T> void extend(vector& dest, T source) {
    dest.reserve(source.size());
    dest.insert(dest.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
  }
  template void extend(vector&, list);
  template void extend(vector&, vector);
  template void extend(vector&, set);

  // Extend a set with a container.
  template <class T> void extend(set& dest, const T source) {
    dest.insert(source.begin(), source.end());
  }
  template void extend(set&, const list);
  template void extend(set&, const vector);

  // Compat function, merges two sets.
  void extend(set& dest, set source) {dest.merge(source);}


  // Wait for an event on a WD.
  void inotify_wait(const int& wd, const std::string_view& name) {
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

  // Share a path.
  void share(vector& command, const std::string_view& path, const std::string& mode) {
    command.reserve(3);
    if (std::filesystem::exists(path)) {
        extend(command, {"--" + mode, path});
        if (path.starts_with(home))
          command.emplace_back(std::string(path.substr(home.length())).insert(0, "/home/sb/"));
        else command.emplace_back(path);
    }
  }

  // Hash a string.
  std::string hash(const std::string_view& in) {
    const size_t hash_length = BLAKE2B_OUTBYTES;
    uint8_t hash[hash_length] = {0};

    // Call blake2b to hash the input string
    int result = blake2b(hash, in.data(), nullptr, hash_length, in.length(), 0);
    if (result == 0) {
      std::stringstream hex_hash;
      hex_hash << std::hex << std::setfill('0');
      for (size_t i = 0; i < hash_length; ++i)
        hex_hash << std::setw(2) << static_cast<unsigned>(hash[i]);
      return hex_hash.str().substr(0, 20);
    }
    else throw std::runtime_error("Failed to hash string!");
  }
}
