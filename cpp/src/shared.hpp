#pragma once
/**
 * @brief Shared functionality.
 */


#include <filesystem>
#include <set>
#include <sys/inotify.h>
#include <random>
#include <fcntl.h>
#include <csignal>

#ifdef PROFILE
#include <map>
#endif

// https://github.com/bshoshany/thread-pool
#include "third_party/BS_thread_pool.hpp"

namespace shared {

  // The thread pool
  extern BS::thread_pool<BS::tp::wait_deadlock_checks> pool;

  // Environment variables
  extern const std::string runtime, config, cache, data, home, session, nobody, real;

  // Our inotify watch
  extern int inotify;

  /**
   * @brief policy for the executor, describing what should be captured.
   */
  typedef enum {NONE, STDOUT, STDERR, ALL} exec_return;

  // Definitions.
  using set = std::set<std::string>;
  using vector = std::vector<std::string>;
  using list = std::initializer_list<std::string_view>;


  /**
   * @brief A Temporary Directory
   * A directory that destroys itself upon falling out of scope.
   */
  class TemporaryDirectory {
    private:
      static std::random_device dev;
      static std::mt19937 prng;
      static std::uniform_int_distribution<uint64_t> rand;

      // The path, and the name
      std::string path;
      std::string name;

      /**
       * @brief Generate a temporary path.
       * @param parent: The directory to create the temp dir.
       * @param prefix: A prefix to put before the random identifier on the directory name.
       * @param suffix: A suffix to put after the random identifier.
       */
      void generate(const std::string_view& parent, const std::string_view& prefix, const std::string_view& suffix) {
        path.reserve(parent.length() + prefix.length() + suffix.length() + 17);
        path.append(parent); if (!parent.ends_with('/')) path += '/';

        std::stringstream name_stream;
        if (!prefix.empty()) name_stream << prefix << '-';
        name_stream << std::hex << rand(prng);
        if (!suffix.empty()) name_stream << '-' << suffix;
        name = name_stream.str();
        path += name;
      }

    public:

    /**
     * @brief Construct a Temporary Directory.
     * @param parent: The path to create the directory at.
     * @param prefix: A prefix for the name.
     * @param suffix: A suffix for the name.
     * @param defer: Defer creation. Call create().
     * @info The directory deletes itself after falling out of scope.
     */
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

    // Obliterate the directory.
    ~TemporaryDirectory() {std::filesystem::remove_all(path);}

    /**
     * @brief Return the path to the directory.
     * @returns The path
     */
    const std::string& get_path() const {return path;}

    /**
     * @brief Create the directory.
     */
    void create() const {std::filesystem::create_directories(path);}

    /**
     * @brief Create a subdirectory/file in the temp dir.
     * @param name: The name of the new entity.
     * @param dir: Whether the entity is a directory, and thus should be
     * created.
     * @returns The path to the new entity.
     */
    std::string sub(const std::string& name, const bool& dir = false) const {
      const auto new_path = path + "/" + name;
      if (dir) std::filesystem::create_directory(new_path);
      return new_path;
    }

    /**
     * @brief Return the name of the directory, excluding the path.
     */
    const std::string& get_name() const {return name;}
  };


  /**
   * @brief Log output to console, if verbose.
   * @param msg: A list of strings to be printed.
   */
  void log(const list& msg, const std::string& level="log");

  /**
   * @brief Extend a container in place.
   * @tparam T: The container type for both dest and source.
   * @param dest: The container to extend.
   * @param source: The values to pull from.
   */
  template <class T = list> void extend(vector& dest, T source);
  template <class T = list> void extend(set& dest, T source);
  void extend(set& dest, set source);

  /**
   * @brief Split a string
   * @tparam C: The accumulator container type.
   * @tparam T: The delimiter type, can either be a single character, or a string of characters.
   * @param working: The accumulator.
   * @param str: The string to split.
   * @param delim: The delimiter to split on
   * @param escape: Ignore delimiters bound in quotes.
   */
  template <typename C, typename T> void splitter(C& working, const std::string_view& str, const T& delim, const bool& escape);
  void split(vector& working, const std::string_view& str, const char& delim, const bool& escape = false);
  void splits(vector& working, const std::string_view& str, const std::string_view& delim, const bool& escape = false);
  void usplit(set& working, const std::string_view& str, const char& delim, const bool& escape = true);
  void usplits(set& working, const std::string_view& str, const std::string_view& delim, const bool& escape = true);

  // Compat definitions.
  bool contains(const char& v, const char& d);
  bool contains(const char& v, const std::string_view& d);
  void emplace(set& working, const std::string_view& val);
  void emplace(vector& working, const std::string_view& val);

  /**
   * @brief Zip a container for exec.
   * @param cmd: The container to zip.
   * @returns The zipped contents.
   */
  template <class T> std::vector<const char*> zip(const T& cmd);

  /*
   * Parsers are used in exec and read_file, where the latter discards
   * the PID.
   */

  /**
   * @brief A Blocking Parser that waits for the PID to exit.
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   */
  void wait_for(const int& fd, const int& pid = -1);

  /**
   * @brief A Non-Blocking Parser that returns the PID.
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns: The PID
   */
  int get_pid(const int& fd, const int& pid = -1);

  /**
   * @brief A Blocking Parser that splits the output into a vector
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns The output, split on newlines.
   * @info If you need to split on something other than '\n', use fd_splitter
   */
  vector vectorize(const int& fd, const int& pid = -1);

  /**
   * @brief A Blocking Parser that splits the output into a set
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns The output, split on spaces.
   * @info If you need to split on something other than ' ', use fd_splitter
   */
  set setorize(const int& fd, const int& pid = -1);

  /**
   * @brief A Blocking Parser that returns the output in a single string.
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns: The output
   */
  std::string dump(const int& fd, const int& pid = -1);

  /**
   * @brief A Blocking Parser that returns the first line of output.
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns: The first line.
   */
  std::string one_line(const int& fd, const int& pid = -1);

  /**
   * @brief A Blocking Parser that returns the first count of the output.
   * @tparam count: The amount of characters to return.
   * @param fd: The FD on the attached pipe.
   * @param pid: The PID of the process.
   * @returns: The output.
   */
  template <uint_fast8_t count> std::string head(const int& fd, const int& pid) {
    std::array<char, count> buffer;
    if (read(fd, buffer.data(), count) != count)
      throw std::runtime_error("Failed to read file!");
    close(fd);
    if (pid > 0) kill(pid, SIGTERM);
    return std::string(buffer.data(), count);
  }

  /**
   * @brief Execute stuff.
   * @tparam R: What to return from the child.
   * @tparam T: The container holding the command.
   * @param cmd: The command to run.
   * @param parser: The Parser to use on the output.
   * @param policy: What to capture.
   * @returns The selected output from the parser.
   */
  template <class R = void, class T = list> R exec(
      const T& cmd,
      const std::function<R(const int&, const int&)>& parser = [](const int& fd, const int& pid) {close(fd);},
      const exec_return& policy = NONE
  )  {

    // Open a pipe for communication.
    int pipefd[2];
    if (pipe(pipefd) == -1) throw std::runtime_error("Failed to setup pipe");

    auto pid = fork();
    if (pid < 0) throw std::runtime_error("Failed to fork");
    else if (pid == 0) {

      // Zip the command, open our standard files to the pipe.
      auto argv = zip(cmd);

      close(pipefd[0]);
      switch (policy) {
        case STDOUT: dup2(pipefd[1], STDOUT_FILENO); break;
        case STDERR: dup2(pipefd[1], STDERR_FILENO); break;
        case ALL:  dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); break;
        default: break;
      }

      // Close the pipe and execute.
      close(pipefd[1]);
      execvp(argv[0], const_cast<char* const*>(argv.data()));
    }

    // Close the pipe and parser the other side.
    close(pipefd[1]);
    return parser(pipefd[0], pid);
  }

  /**
   * @brief Split the contents of an FD.
   * @tparam C: The container to return.
   * @tparam delim: The delimiter to use.
   * @param fd: The FD of the file/process.
   * @param pid: The PID of the process.
   * @returns The split output/contents.
   */
  template <class C, char delim> C fd_splitter(const int& fd, const int& pid) {
    C result;
    auto contents = dump(fd, pid);
    splitter<C, char>(result, contents, delim, false);
    return result;
  }


  /**
   * @brief Read a file
   * @tparam R: The return type.
   * @param path: The path to the file.
   * @param parser: The parser to use on the file.
   * @returns The contents.
   */
  template <class R> R read_file(const std::filesystem::path& path, const std::function<R(const int&, const int&)>& parser) {
    return parser(open(path.c_str(), O_RDONLY), -1);
  }


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
  template <typename T = char> std::string strip(const std::string_view& in, const T& to_strip);


  /**
   * @brief Trim characters from the front and end of a string.
   * @param in: The input string.
   * @param to_strip: The list of characters to trim.
   * @returns The trimmed string.
   * @info trim only removes from the front and end, stopping after
   * encountered a non-to_strip character, whereas strip removes
   * all instances regardless.
   */
  template <typename T = char> std::string trim(const std::string& in, const T& to_strip);


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
   * @brief Share a path with the sandbox using a mode.
   * @param command: The command to append to.
   * @param path: The path to share.
   * @param mode: The mode to use to share.
   */
  void share(vector& command, const std::string_view& path, const std::string& mode = "ro-bind");


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


  /**
   * @brief Wait for an inotify watcher.
   * @param wd: The inotify FD for a specific watch.
   * @param name: The optional name to look out for.
   */
  void inotify_wait(const int& wd, const std::string_view& name = "");

  /**
   * @brief Hash a string
   * @param in: The input string.
   * @returns The hex digest.
   */
  std::string hash(const std::string_view& in);


  /**
   * @brief Compose a return value from an accumulator function.
   * @tparam T The return type (IE the accumulator type).
   * @tparam A The function to wrap.
   * @tparam ...Args Additional arguments to the function.
   * @param fun: The function.
   * @param args: Additional arguments.
   * @returns The accumulated results from the function.
   */
  template <class T, class A, class ...Args> T init(const A& fun, const Args&... args) {
    T ret;
    fun(ret, args...);
    return ret;
  }

  // Modified init that takes mutable refrences after the constant references.
  template <class T, class A, class ...Args, class ...Refs> T rinit(const A& fun, const Args&... args, Refs&... refs) {
    T ret;
    fun(ret, args..., refs...);
    return ret;
  }

  /**
   * @brief Batch multiple iterations of an accumulator function together.
   * @tparam T The accumulator type.
   * @tparam A: The function type.
   * @tparam L: The container holding each value.
   * @tparam ...Args: Additional arguments.
   * @param fun: The function to call
   * @param accum: The accumulator passed to the function.
   * @param mem: The list of values to emplace.
   * @param args: Additional arguments.
   *
   */
  template <class T, class A, class L = list, class ...Args, class ...Refs> void single_batch(const A& fun, T& accum, const L& mem, Args... args, Refs&... refs) {
    for (const auto& val : mem) fun(accum, val, args..., refs...);
  }

  /**
   * @brief Batch multiple iterations of an accumulator function together, threaded.
   * @tparam T: The accumulator type.
   * @tparam A: The function type
   * @tparam L: The container holding each value.
   * @tparam ...Args: Additional arguments.
   * @param fun: The function to call.
   * @param accum: The accumulator to append to.
   * @param mem: The list of values to emplace.
   * @param args: Additional arguments.
   * @warning All arguments must be immutable, as instances are threaded together.
   * If the ordering matters, or residue arguments are accumulators, use single_batch
   */
  template <class T, class A, class L = list, class ...Args> void batch(const A& fun, T& accum, const L& mem, const Args&... args) {
    std::vector<std::future<T>> futures; futures.reserve(mem.size());
    for (auto& val : mem) {
      futures.emplace_back(pool.submit_task([val = std::move(val), &fun, &args...]() {return init<T>(fun, val, args...);}));
    }
    for (auto& future : futures) extend(accum, future.get());
  }


  /**
   * @brief Batch multiple extend calls into the thread pool.
   * @tparam T: The list type.
   * @param dest: The accumulator.
   * @param source: A list of lists that need to be extended.
   */
  void extend(vector& dest, const std::initializer_list<const list>& source);


  // Profiling stuff :)
  #ifdef PROFILE
  extern std::map<std::string, uint_fast32_t> time_slice;

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
