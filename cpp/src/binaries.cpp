#include "binaries.hpp"
#include "arguments.hpp"

#include <regex>
#include <fstream>

using namespace shared;

namespace binaries {

  // Binaries that have already been searched
  set searched = {};
  set builtins = {"printf", "echo"};

  // Parse the commands used in a binary.
  void parse(bin_t& required, std::string path, libraries::lib_t& libraries) {

    // Required binaries
    bin_t local = {};

    // Don't do work more than once
    if (path.empty() || searched.contains(path)) return;
    searched.emplace(path);

    // Get libraries dependencies for libraries.
    // We don't check the ELF header since libraries already does that,
    // we only do it later because we already have the file opened.
    if (path.contains("/lib/")) {
      libraries::get(libraries, path);
      return;
    }

    // Resolve.
    if (!std::filesystem::exists(path)) {
        auto resolved = path.insert(0, "/usr/bin/");
        if (!std::filesystem::exists(path))
          throw std::runtime_error("Could not locate binary: " + path);
        else path = resolved;
    }

    // Normalize to /usr
    if (path.starts_with("/bin/")) path = path.insert(0, "/usr");
    local.emplace(path);

    // Grab the header, so we don't ready the whole library if it's an ELF.
    auto header = read_file<std::string>(path, head<4>);
    if (header.empty()) return;

    // Shell script
    // Shell scripts are parsed line-by-line, tokenizing the string and extracting
    // values from within. It extract the shebang, environment variables needed to
    // resolve other binaries, and other commands directly used.
    //
    // Unlike Python-SB, SB++ resolves environment variables, which allows for far
    // more nuanced parsing. For example, resolving an electron script, we can
    // resolve /usr/lib/{name}/electron, and get all the dependencies involved,
    // which means that the --electron-version flag is entirely unnecessary since
    // we can parse the required files straight from the shell script.
    if (header.starts_with("#!")) {

      // Now dump the entire file.
      auto contents = read_file<std::string>(path, dump);

      // Check whether we've cached it.
      std::string name = path;
      std::replace(name.begin(), name.end(), '/', '.');
      std::replace(name.begin(), name.end(), '*', '.');
      auto cache = std::filesystem::path(data) / "sb" / "cache" / std::string(name + ".bin.cache");

      // If the cache exists, and we don't need to update, use it.
      if (std::filesystem::exists(cache) && arg::at("update") < "cache") {
        local = read_file<bin_t>(cache, setorize);
        single_batch(parse, required, local, libraries);
      }
      else {
        std::filesystem::create_directories(cache.parent_path());

        // Variables defined
        std::map<std::string, std::string> variables = {};

        // Resolve environment variables in a token
        auto resolve_environment = [&variables](std::string value) {

          // Yeah, no. If a variable needs to run more commands, then it doesn't get
          // resolved.
          if (value.contains("$(")) {
            log({"Refusing to resolve variable that can arbitrarily execute"});
            return value;
          }

          // Replace all existing variables we know about.
          for (const auto& [k, v] : variables) {
            if (value.contains(k)) {

              size_t pos = value.find(k);

              // Variables can be used like $TEST
              if (value[pos - 1] == '$') value.erase(pos - 1, 1);

              // These can also be used like ${TEST}
              else if (value[pos - 1] == '{') {
                value.erase(pos - 2, 2);

                // Our tokenization isn't perfect, so sometimes
                // we can hack an environment variable in half.
                // We just throw it away if that's the case.
                auto close = value.find('}', pos);
                if (close == std::string::npos) return value;
                value.erase(close, 1);
              }

              else continue;
              value = std::regex_replace(value, std::regex(k), v);
            }
          }

          // Ensure that the value isn't stupid.
          if (
            (value.contains('{') && !value.contains('}')) ||
            (value.starts_with('(') && value.ends_with(')'))
          ) return value;

          // Let the shell resolve the nuances.
          return exec<std::string>({"echo", value}, one_line, STDOUT);
        };

        vector tokens;
        set discovered;

        auto tokenize = [&variables, &resolve_environment, &local, &required, &libraries, &tokens, &discovered](const uint_fast8_t& x) {
          auto token = tokens[x];

          // If we already know it's garbage, don't bother trying again.
          // If the token is just an environment variable, we don't need to try anything
          if (variables.contains(token) || builtins.contains(token)) return;

          // If we use a variable, resolve it.
          if (token.contains("${")) token = resolve_environment(token);

          // If the token is empty, or its just a switch, ignore it
          if (token.empty() || token[0] == '-') return;

          // Ask which if it knows what this token is.
          auto binary = trim<std::string_view>(std::filesystem::exists(token) ? token : token.insert(0, "/usr/bin/"), "\n\t ");

          // If it does, add it and resolve it
          if (std::filesystem::exists(binary) && binary.contains("/bin/")) {
            if (binary.starts_with("/bin/")) binary = binary.insert(0, "/usr");
            auto base = std::filesystem::path(binary).filename();
            if (discovered.contains(base)) return;
            discovered.emplace(base);
            auto parsed = rinit<binaries::bin_t>(parse, binary, libraries);

            local.emplace(binary);
            required.merge(parsed);
          }
          if (binary.contains("/lib/")) libraries::get(libraries, binary);
        };


        // Get the shebang from the top.
        auto lines = init<vector>(split, contents, '\n', false);
        auto shebang = strip<std::string_view>(lines[0], "#! \t\n");
        local.emplace(shebang);

        if (shebang.starts_with("/bin/")) shebang = shebang.insert(0, "/usr");
        parse(required, shebang, libraries);

        // Go through the other lines.
        for (uint_fast16_t x = 0; x < lines.size(); ++x) {

          // Trim whitespace
          const auto& line = trim<std::string_view>(lines[x], " \t");

          // Remove blanks or empties.
          if (line.empty() || line[0] == '#') continue;

          // If we have a variable declaration, parse and store it so we can resolve
          // future use.
          if (line.contains('=')) {
            auto keypair = init<vector>(split, line, '=', false);
            const auto& key = keypair[0], val = keypair[1];
            if (!key.contains(' ') && !val.contains(' ')  && val != "()") variables[key] = resolve_environment(val);
          }

          // Tokenize the string
          tokens = init<vector>(splits, line, " \t()=;\"", false);
          auto future = pool.submit_loop(0, tokens.size(), tokenize);
          future.wait();
        }

        // Update the cache; note libraries are already cached by libraries::get.
        auto file = std::ofstream(cache);
        if (file.is_open()) {
          file << join(local, ' ');
          file.close();
        }
        else log({"Failed to write cache file:", cache.string()});
      }
    }

    // Actual executables have their shared libraries parsed.
    else if (header == "\177ELF") libraries::get(libraries, path);

    // Merge and return.
    required.merge(local);
  }


  // Setup the binaries.
  void setup(const bin_t& binaries, vector& command) {
    extend(command, {"--dir", "/usr/bin"});
    for (const auto& binary : binaries) {
      if (std::filesystem::is_symlink(binary)) {
        auto dest = std::filesystem::read_symlink(binary).string();

        if (!dest.contains('/')) dest.insert(0, "/usr/bin/");
        else if (dest.starts_with("/bin")) dest.insert(0, "/usr");

        extend(command, {
          "--ro-bind", dest, dest,
          "--symlink", dest, binary.starts_with("/bin") ? "/usr" + binary : binary
        });
      }
      else {
        if (binary.contains("bin")) extend(command, {"--ro-bind", binary, "/usr/bin/" + binary.substr(binary.rfind('/'))});
        else extend(command, {"--ro-bind", binary, binary});
      }
    }
  }

  void symlink(vector& command) {
    extend(command, {
      "--symlink", "/usr/bin", "/bin",
      "--symlink", "/usr/bin", "/sbin",
      "--symlink", "/usr/bin", "/usr/sbin"
    });
  }
}
