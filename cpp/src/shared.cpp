#include <cstring>
#include <pwd.h>
#include <sys/wait.h>
#include <blake2.h>

#include "shared.hpp"
#include "arguments.hpp"

namespace shared {

  // Init our shared values.
  BS::thread_pool<BS::tp::wait_deadlock_checks> pool = {};
  int inotify = -1;
  std::random_device TemporaryDirectory::dev = {};
  std::mt19937 TemporaryDirectory::prng(TemporaryDirectory::dev());
  std::uniform_int_distribution<uint64_t> TemporaryDirectory::rand(0);

  constexpr uint_fast16_t buffer_size = 1024;

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


  // Log to output
  void log(const list& msg, const std::string& level) {
    if (arg::at("verbose") >= level) {
      for (const auto& x : msg)
        std::cout << x << ' ';
      std::cout << std::endl;
    }
  }


  // Helper functions.
  bool contains(const char& v, const char& d) {return v == d;}
  bool contains(const char& v, const std::string_view& d) {return d.contains(v);}
  void emplace(set& working, const std::string_view& val) {working.emplace(val);}
  void emplace(vector& working, const std::string_view& val) {working.emplace_back(val);}

  // Zip a list/vector into a format exec can use.
  template <class T> std::vector<const char*> zip(const T& cmd) {
    std::vector<const char*> argv; argv.reserve(cmd.size() + 1);
    if (arg::at("verbose") >= "debug") {
      std::cout << "EXEC: ";
       for (const std::string_view& v : cmd) {
         std::cout << v << ' ';
         argv.emplace_back(v.data());
       }
       std::cout << std::endl;
    }
    else for (const std::string_view& v : cmd) argv.emplace_back(v.data());
    argv.emplace_back(nullptr);
    return argv;
  }
  template std::vector<const char*> zip(const list&);
  template std::vector<const char*> zip(const vector&);

  // Blocking Parser that merely waits until the pid closes.
  void wait_for(const int& fd, const int& pid) {close(fd); int state; waitpid(pid, &state, 0);}

  // Non-Blocking Parser that detaches and returns the PID.
  int get_pid(const int& fd, const int& pid) {close(fd); return pid;}

  // Blocking Parser that splits output into a vector based on newlines.
  vector vectorize(const int& fd, const int& pid) {return fd_splitter<vector, '\n'>(fd, pid);}

  // Blocking Parser that splits output into a set based on spaces.
  set setorize(const int& fd, const int& pid) {return fd_splitter<set, ' '>(fd, pid);}

  // Blocking Parser that dumps output into a single string.
  std::string dump(const int& fd, const int& pid) {
    std::string result;
    std::array<char, buffer_size> buffer;
    int bytes = read(fd, buffer.data(), buffer_size);
    while (bytes > 0) {
      result.reserve(bytes);
      result.append(buffer.data(), bytes);
      bytes = read(fd, buffer.data(), buffer_size);
    }
    close(fd);
    return result;
  }

  // Blocking Parser that dumps the first line of output and returns immediately.
  std::string one_line(const int& fd, const int& pid) {
    std::string result;
    std::array<char, buffer_size> buffer = {0};
    int bytes = read(fd, buffer.data(), buffer_size), index = 0;
    while (bytes > 0 && index != std::string::npos)  {
      result.reserve(buffer_size);
      result.append(buffer.data(), bytes);
      bytes =  read(fd, buffer.data(), buffer_size);
      index = result.rfind('\n');
    }
    close(fd);
    if (pid > 0) kill(pid, SIGTERM);
    return result.substr(0, index);
  }

  // Splitter utility for splitting strings into containers.
  template <typename C, typename T> void splitter(C& working, const std::string_view& str, const T& delim, const bool& escape) {

    // Ignore our delim if its contained within quotes.
    bool wrapped = false;
    for (size_t r_bound = 0, l_bound = 0; r_bound <= str.length(); ++r_bound) {
      if (!wrapped && (contains(str[r_bound], delim) || r_bound == str.length())) {
        if (r_bound != l_bound) emplace(working, std::string_view(&str[l_bound], r_bound - l_bound));
        l_bound = r_bound + 1;
      }

      // If we hit a quote, we ignore the delimeters until we reach the match.
      // We also skip over the quote itself when emplacing.
      else if (escape && str[r_bound] == '\'') {
        if (r_bound == l_bound)
          ++l_bound;
        else if (contains(str[r_bound + 1], delim)) {
          emplace(working, std::string_view(&str[l_bound], r_bound - l_bound - 1));
          l_bound = r_bound + 1;
        }
        wrapped ^= 1;
      }
    }
  }
  template void splitter(vector&, const std::string_view&, const char&, const bool&);
  template void splitter(vector&, const std::string_view&, const std::string_view&, const bool&);
  template void splitter(set&, const std::string_view&, const char&, const bool&);
  template void splitter(set&, const std::string_view&, const std::string_view&, const bool&);

  // Split a string with a character.
  void split(vector& working, const std::string_view& str, const char& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }

  // Split a string with a collection of delims.
  void splits(vector& working, const std::string_view& str, const std::string_view& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }

  // Split into a set, using a character
  void usplit(set& working, const std::string_view& str, const char& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }

  // Split into a set, using a string.
  void usplits(set& working, const std::string_view& str, const std::string_view& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }


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
    return exec<set>(command, fd_splitter<set, '\n'>, STDOUT);
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
