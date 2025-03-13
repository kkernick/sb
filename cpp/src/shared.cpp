#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <random>
#include <fstream>
#include <pwd.h>
#include <string_view>
#include <unistd.h>
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

  constexpr size_t buffer_size = 1024;

  #ifdef PROFILE
  std::map<std::string, size_t> time_slice;
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
    if (arg::at("verbose").meets(level)) {
      for (const auto& x : msg)
        std::cout << x << ' ';
      std::cout << std::endl;
    }
  }

  bool contains(const char& v, const char& d) {return v == d;}
  bool contains(const char& v, const std::string_view& d) {return d.contains(v);}
  void emplace(set& working, const std::string_view& val) {working.emplace(val);}
  void emplace(vector& working, const std::string_view& val) {working.emplace_back(val);}

  template <class T> std::vector<const char*> zip(const T& cmd) {
    // The child creates a command array for execvp
    std::vector<const char*> argv; argv.reserve(cmd.size() + 1);
    if (arg::at("verbose").meets("debug")) {
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

  void wait_for(const int& fd, const int& pid) {close(fd); int state; waitpid(pid, &state, 0);}
  int get_pid(const int& fd, const int& pid) {close(fd); return pid;}

  vector vectorize(const int& fd, const int& pid) {return fd_splitter<vector, '\n'>(fd, pid);}
  set setorize(const int& fd, const int& pid) {return fd_splitter<set, ' '>(fd, pid);}

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
    return result.substr(0, index);
  }

  template <typename C, typename T> void splitter(C& working, const std::string_view& str, const T& delim, const bool& escape) {
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

  void split(vector& working, const std::string_view& str, const char& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }
  void splits(vector& working, const std::string_view& str, const std::string_view& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }
  void usplit(set& working, const std::string_view& str, const char& delim, const bool& escape) {
    splitter(working, str, delim, escape);
  }
  void usplits(set& working, const std::string_view& str, const std::string_view& delim, const bool& escape) {
    splitter(working, str, delim, escape);
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
  template std::string join(const vector&, const char&);
  template std::string join(const set&, const char&);


  std::string strip(const std::string_view& in, const std::string_view& to_strip) {
    std::stringstream ret;
    for (const auto& x : in)
      if (!to_strip.contains(x)) ret << x;
    return ret.str();
  }

  std::string trim(const std::string& in, const std::string_view& to_strip) {
    if (in.empty()) return in;

    size_t l = 0, r = in.length() - 1;
    while (l < in.length() && to_strip.contains(in[l])) ++l;
    while (r > 0 && to_strip.contains(in[r])) --r;
    return in.substr(l, r - l + 1);
  }

  set wildcard(const std::string_view& pattern, const std::string_view& path, const list& args) {
    vector command;
    command.reserve(4 + args.size());

    auto local = pattern.contains('/');

    command.insert(command.end(), {"find", local ? std::filesystem::path(pattern).parent_path().string() : path.data()});
    command.insert(command.end(), args.begin(), args.end());
    command.insert(command.end(), {"-name", local ? std::filesystem::path(pattern).filename().string() : pattern.data()});
    return exec<set>(command, fd_splitter<set, '\n'>, STDOUT);
  };


  void genv(vector& command, const std::string_view& env) {
    if (std::getenv(env.data()) != nullptr)
     extend(command, {"--setenv", env, std::getenv(env.data())});
    else log({"Missing environment variable:", env});
  }

  void genvs(vector& command, const list& envs) {
   for (const auto& env : envs) genv(command, env);
  }

  template <class T> void extend(vector& dest, const T& source) {
    dest.reserve(dest.size() + distance(source.begin(), source.end()));
    dest.insert(dest.end(), source.begin(), source.end());
  }
  template void extend(vector&, const list&);
  template void extend(vector&, const vector&);
  template void extend(vector&, const set&);


  template <class T> void extend(set& dest, const T& source) {
    dest.insert(source.begin(), source.end());
  }
  template void extend(set&, const list&);
  template void extend(set&, const vector&);
  void extend(set& dest, set source) {dest.merge(source);}


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

  template <class T> void share(vector& command, const T& paths, const std::string& mode) {
    vector constructed; constructed.reserve(3 * paths.size());
    for (const auto& path : paths) {
      if (std::filesystem::exists(path)) {
          extend(constructed, {"--" + mode, path});
          if (path.starts_with(home))
            constructed.emplace_back(std::string(path.substr(home.length())).insert(0, "/home/sb/"));
          else constructed.emplace_back(path);
      }
    }
    extend(command, constructed);
  }
  template void share(vector&, const list&, const std::string&);
  template void share(vector&, const vector&, const std::string&);
  template void share(vector&, const set&, const std::string&);

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
      return hex_hash.str();
    }
    else throw std::runtime_error("Failed to hash string!");
  }
}
