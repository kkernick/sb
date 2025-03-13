/**
 * @brief Shared functionality.
 */

#pragma once

#include <filesystem>
#include <initializer_list>
#include <set>
#include <sys/inotify.h>
#include <random>

#ifdef PROFILE
#include <map>
#endif

// https://github.com/bshoshany/thread-pool
#include "third_party/BS_thread_pool.hpp"

namespace shared {

  extern BS::thread_pool<BS::tp::wait_deadlock_checks> pool;
  extern const std::string runtime, config, cache, data, home, session, nobody, real;
  extern int inotify;

  typedef enum {NONE, STDOUT, STDERR, ALL} exec_return;

  using set = std::set<std::string>;
  using vector = std::vector<std::string>;
  using list = std::initializer_list<std::string_view>;


  template <class T, class A, class ...Args> T init(const A& fun, const Args&... args) {
    T ret;
    fun(ret, args...);
    return ret;
  }
  template <class T, class A, class ...Args, class ...Refs> T init(const A& fun, const Args&... args, Refs&... refs) {
    T ret;
    fun(ret, args..., refs...);
    return ret;
  }

  template <class T, class A, class L = list, class ...Args> void batch(const A& fun, T& accum, const L& mem, Args... args) {
    for (const auto& val : mem) fun(accum, val, args...);
  }


  /**
   * @brief Read a file completely.
   * @param path: The path to the file.
   * @returns The file contents.
   */
  std::string read_file(const std::filesystem::path& path);

  /**
   * @brief A Temporary Directory
   * A directory that destroys itself upon falling out of scope.
   */
  class TemporaryDirectory {
    private:
      static std::random_device dev;
      static std::mt19937 prng;
      static std::uniform_int_distribution<uint64_t> rand;

      std::string path;

      void generate(const std::string_view& parent, const std::string_view& prefix, const std::string_view& suffix) {
        std::stringstream path_stream;
        path_stream << parent;
        if (!parent.ends_with('/')) path_stream << '/';
        if (!prefix.empty()) path_stream << prefix << '-';
        path_stream << std::hex << rand(prng);
        if (!suffix.empty()) path_stream << '-' << suffix;
        path = path_stream.str();
      }

    public:

    TemporaryDirectory(
        const std::string& parent = std::filesystem::temp_directory_path(),
        const std::string_view& prefix = "", const std::string_view& suffix = "",
        const bool& defer = false
    ) {
      do {
        generate(parent, prefix, suffix);
      } while (std::filesystem::is_directory(path));
      if (!defer) create();
    }

    ~TemporaryDirectory() {
      std::filesystem::remove_all(path);
    }

    const std::string& get_path() const {return path;}

    void create() const {std::filesystem::create_directories(path);}

    std::string sub(const std::string& name, const bool& dir=false) const {
      const auto new_path = path + "/" + name;
      if (dir) std::filesystem::create_directory(new_path);
      return new_path;
    }
  };


  /**
   * @brief Log output to console, if verbose.
   * @param msg: A list of strings to be printed.
   */
  void log(const list& msg, const std::string& level="log");

  template <class T = list> int detach(const T& cmd);
  template <class T = list> int wait_for(const T& cmd);
  template <class C = vector, class T = list> C exec(const T& cmd, const exec_return& policy = STDOUT);
  template <class T = list> set uexec(const T& cmd, const exec_return& policy = STDOUT);
  template <class T = list> std::string dump(const T& cmd, const exec_return& policy = STDOUT);
  template <class T = list> std::string one_line(const T& cmd, const exec_return& policy = STDOUT);


  /**
   * @brief Split a string on a set of delimiters.
   * @param str: The string
   * @param delim: The delimiters.
   * @returns: A vector of each sub-string.
   */
   void split(vector& working, const std::string_view& str, const char& delim, const bool& escape = false);
   void splits(vector& working, const std::string_view& str, const std::string_view& delim, const bool& escape = false);
   void usplit(set& working, const std::string_view& str, const char& delim, const bool& escape = true);
   void usplits(set& working, const std::string_view& str, const std::string_view& delim, const bool& escape = true);


  /**
   * @brief Join a vector into a string.
   * @tparam The container. Defaults to vector of strings, but can also be set.
   * @param list: The list to join.
   * @param joiner: The character to join each member.
   * @returns: The joined string.
   */
  template <class T = vector> std::string join(const T& list, const char& joiner =  ' ');

  /**
   * @brief Strip all instance of character from a string.
   * @param in: The input string.
   * @param to_strip: A list of characters to remove
   * @returns The stripped string.
   */
  std::string strip(const std::string_view& in, const std::string_view& to_strip);

  /**
   * @brief Trim characters from the front and end of a string.
   * @param in: The input string.
   * @param to_strip: The list of characters to trim.
   * @returns The trimmed string.
   * @info trim only removes from the front and end, stopping after
   * encountered a non-to_strip character, whereas strip removes
   * all instances regardless.
   */
  std::string trim(const std::string& in, const std::string_view& to_strip);

  /**
   * @brief Resolve wildcard patterns.
   * @param pattern: The pattern to resolve
   * @param path: The path to look in
   * @param args: Any additional arguments to find.
   * @returns: All unique matches.
   */
  set wildcard(const std::string_view& pattern, const std::string_view& path, const list& args = {});

  /**
   * @brief Escape a string.
   * @param in: The string to escape.
   * @returns: The escaped string.
   */
  std::string escape(const std::string& in);


  /**
   * @brief Extend a container in place.
   * @tparam T: The container type for both dest and source.
   * @param dest: The container to extend.
   * @param source: The values to pull from.
   */
  template <class T = list> void extend(vector& dest, const T& source);
  template <class T = list> void extend(set& dest, const T& source);
  void extend(set& dest, set source);


  /**
   * @brief Share a path with the sandbox using a mode.
   * @param command: The command to append to.
   * @param path: The path to share.
   * @param mode: The mode to use to share.
   */
  template <class T = list> void share(vector& command, const T& path, const std::string& mode = "ro-bind");

  /**
   * @brief Merge two sets together.
   * @param command: The set to be extended.
   * @param path: The set to merge into the first.
   * @info This function exists because C++ cannot deduce bracket initializers.
   */
  void merge(set& command, set path);

  /**
   * @brief Attach an environment variable to the sandbox.
   * @param command: The command to append to.
   * @param env: The environment variable to add.
   * @info: The value of the variable is the actual value.
   */
  void genv(vector& command, const std::string_view& env);
  void genvs(vector& command, const list& envs);

  /**
   * @brief Wait for an inotify watcher.
   * @param wd: The inotify FD for a specific watch.
   * @param name: The optional name to look out for.
   */
  void inotify_wait(const int& wd, const std::string_view& name = "");

  std::string hash(const std::string_view& in);


  #ifdef PROFILE
  extern std::map<std::string, size_t> time_slice;

  template <typename T> inline void profile(const std::string& name, T func) {
    auto begin = std::chrono::high_resolution_clock::now();
    func();
    auto duration = duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - begin).count();
    time_slice["total"] += duration;
    time_slice[name] = duration;
    log({name, ":", std::to_string(duration), "us"});
  }
  #else
    #define profile(name, func) func();
  #endif
}
