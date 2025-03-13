/**
 * @brief A general purpose, flexible command-line argument handler.
 * This file includes definitions to create a powerful command-line argument handler. `--switches` and `-s`
 * formatting is supported, as is specifying multiple short-switches together a la `-vvu`. Additionally,
 * lists are supported, and values can be provided either with an = key, a space, or with multiple invocations
 * of the switch for lists. Support for mandatory arguments is also supported.
 * Additionally, modifier tags and lambdas can extend argument functionality tremendously.
 */

#pragma once

#include <map>

#include "shared.hpp"


namespace arg {

  // Custom Policy
  // FALSE: If there are valid values, they are are enforced.
  // TRUE: The user can provide custom values outside of the valid ones.
  // MODIFIABLE: Valid values are needed, but can be modified via VALUE:MOD
  enum struct custom_policy {FALSE, TRUE, MODIFIABLE};


  /**
   * Arguments passed to Argument.
   */
  typedef struct config {
    const std::string& l_name;
    const std::string& s_name;
    const std::string& def = "false";
    const std::string& mod = "";
    const std::vector<std::string> valid = {};
    const bool& must_set = false;
    const custom_policy& custom = custom_policy::FALSE;
    const bool& list = false;
    const size_t& position = -1;
    const std::string& help;
  } config;


  /**
   * @brief A command-line argument.
   */
  class Arg {
    private:

      // Long name, like --verbose, short-name, like -v
      std::string long_name, short_name;

      // What the default value is.
      std::string value, def, modifier;

      // The help string!
      std::string help;

      // A path for if wildcards are encountered.
      bool list = false;

      // The set of valid values. Empty means any value is acceptable.
      // list_val is only used when list=true.
      std::set<std::string> valid = {}, list_val = {};
      std::vector<std::string> order = {};

      // If the flag is mandatory, it must be set.
      bool mandatory = false, set = false;

      // The custom policy.
      custom_policy custom = custom_policy::FALSE;

      // Set a mandatory position for the argument.
      size_t positional = -1;

      // Special logic to parse command lines!
      std::function<std::string(const std::string&)> parser, m_parser;

      /**
       * @brief Digest a key and value.
       * @param key: The key, which needs to be long_name or short_name to be consumed.
       * @param val: The new value, which must be valid.
       * @param pos: The current position, for positional arguments.
       * @returns Whether the keypair was consumed.
       */
      bool digest_keypair(const std::string& key, const std::string& val, const size_t& pos) {

        // Single value modifiers are easy to split.
        if (!list && custom == custom_policy::MODIFIABLE && val.contains(':')) {

          // Split on colons.
          auto s = shared::init<shared::vector>(shared::split, val, ':', false);
          if (s.size() < 2) throw std::runtime_error("Invalid modifier: " + val);

          // Grab the modifier keypair
          auto v = s[0]; auto mod = s[1];

          // If there are no valids, or it is valid, modify.
          if (valid.size() <= 1 || valid.contains(v)) {
            value = parser(v);
            modifier = m_parser(mod);
            return true;
          }
        }

        // Ensure the key matches this argument
        else if (key == long_name || key == short_name) {
          if (long_name == "--help") throw std::runtime_error("Help!");

          // Using ! sets value to their default values, and clears list.
          if (val == "!") {
            value = def;
            modifier = "";
            list_val.clear();
            return true;
          }

          // Ensure the value is valid.
          else if (valid.size() <= 1 || custom == custom_policy::TRUE || std::find(valid.begin(), valid.end(), val) != valid.end()) {

            // If our key already has multiple values, split and handle separately.
            if (list && val.contains(',')) {
              for (const auto& x : shared::init<shared::vector>(shared::split, val, ',', false))
                digest_keypair(key, x, pos);
            }

            // Otherwise just update.
            else if (list) list_val.emplace(parser(val));
            else value = parser(val);
            return true;
          }
        }
        return false;
      }


      /**
       * @brief Increment the argument.
       * @info Arguments are all strings, but we can treat them like numbers
       * by simply 'incrementing' the argument to successive valid values. For example,
       * --verbose is a numerical argument, and we can treat this verboseness either as a boolean:
       * valid = {"false", "true"}, or with multiple values, {"false", "log", "debug"}. Then,
       * the Argument will increment from none, to log, if we just pass --verbose without an argument.
       * Therefore, we can do -vv and get a value of 2 through level().
       */
      void increment() {
        if (long_name == "--help") throw std::runtime_error("Help!");
        auto i = level();
        if (i + 1 < valid.size())
          value = order[i + 1];
        else
          std::cerr << long_name << ": Already at highest level!" << std::endl;
      }

      /**
       * @brief Return the level for a corresponding valid value.
       * @param val: The value.
       */
      size_t level(const std::string& val) const {return std::find(order.begin(), order.end(), val) - order.begin();}

    public:

      /**
      * @brief Construct an argument.
      * @param c: THe configuration structure.
      * @param handler: The regular lambda for parsing arguments.
      * @param m_handler: The lambda for parsing modifier arguments.
      */
      Arg(
        const config& c,
        const std::function<std::string(const std::string&)>& handler = [](const std::string& value){return value;},
        const std::function<std::string(const std::string&)>& m_handler = [](const std::string& value){return value;}
      ) {

        long_name = c.l_name;
        short_name = c.s_name;
        help = c.help;
        parser = handler;
        m_parser = m_handler;

        // Parse the defaults.
        if (!c.mod.empty()) modifier = m_parser(c.mod);
        value = parser(c.def);
        def = value;

        // Construct the valid field.
        // The default value will always be valid, and can be excluded from the passed
        // valid values.
        valid = {c.def}; order = {c.def};
        for (const auto& v : c.valid) {
          if (v != c.def) {valid.emplace(v); order.emplace_back(v);}
        }
        mandatory = c.must_set;
        custom = c.custom;
        list = c.list;
        positional = c.position;
      }

      /**
       * @brief Digest arguments.
       * @param args: The list of command line arguments.
       * @param x: The current position within the list.
       * @returns: Whether the current argument was consumed.
       */
      bool digest(const std::vector<std::string>& args, size_t& x) {

        // Get the value
        const auto& key = args[x];
        auto ret = false;

        // If a .sb script ends with a list argument, new args
        // may be "consumed" by the list. However, we need to append
        // arguments to the end for proper overriding. Scripts, therefore
        // use "-- $@" to ensure that new arguments are not attached to a
        // list
        if (key == "--") return true;

        // If we have an equal, it's `key=value`
        if (key.contains("=")) {
          auto keypair = shared::init<shared::vector>(shared::split, key, '=', false);
          ret = digest_keypair(keypair[0], keypair[1], x);
        }

        // If we have a short name, digest each character.
        // There is no support for values for short names.
        else if (key[0] == '-' && key[1] != '-' && key.length() > 2) {
          for (size_t x = 1; x < key.length(); ++x) {
            if (key[x] == short_name[1]) {
              increment();
            }
          }
          // We don't return true here because with a collection of args we
          // need to parse for each match.
        }

        // / Otherwise, if the next argument isn't a switch, we have `key value`
        else if (!key.empty() && (key == long_name || key == short_name)) {
          if (x + 1 != args.size() && !args[x].empty() && args[x + 1][0] != '-') {
            if (list) {
              while (++x < args.size() && !args[x].empty() && args[x][0] != '-')
                digest_keypair(key, args[x], x);
              ret = true;
              --x;
            }
            else ret = digest_keypair(key, args[++x], x);
          }

          // If there are stages to the switch, increment it.
          else if (valid.size() > 1) {
            increment();
            ret = true;
          }
        }

        // If the position matches, and nothing else did, assume the key is the value.
        else if (positional == x) {
          if (key.starts_with("-"))
            throw std::runtime_error("Invalid argument passed to: " + long_name);
          value = parser(key);
          ret = true;
        }

        // Update the set flag.
        set |= ret;

        if (x == positional && !ret)
          throw std::runtime_error("Positional argument not set: " + long_name);
        return ret;
      }


      // Update the argument for any post-parsing.
      void update() {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        value = parser(value); modifier = m_parser(modifier);
      }

      /**
       * @brief Get the help text for the argument.
       * @returns The string.
       */
      std::string get_help() const {
        std::stringstream help_text;
        help_text << long_name;
       if (!short_name.empty()) help_text << '/' << short_name;
       help_text << ' ';

        if (list) {
          help_text << '[' << "VAL";
          if (custom == custom_policy::MODIFIABLE) help_text << ":MOD";
          help_text << ',';
        }
        if (order.size() > 1) help_text << '{';
        if (order.size() > 1) {
          for (const auto& v : order) {
            help_text << v;
            if (custom == custom_policy::MODIFIABLE) help_text << ":MOD";
            if (v != *std::prev(order.end())) help_text << ',';
          }
          help_text << '}';
        }
        if (list) help_text << "...]";

        help_text << "\n\t" << help << '\n';
        return help_text.str();
      }

      // Check if the current level is underneath the provided key.
      const bool under(const std::string& val) const {return level(value) < level(val);}

      // Check if the current level is at the val, or above it.
      const bool meets(const std::string& val) const {return level(value) >= level(val);}

      // Get the value.
      std::string& get() {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        if (list)
          throw std::runtime_error("Not a discrete value: " + long_name);
        return value;
      }

      std::string& mod() {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        return modifier;
      }

      const bool& is_list() const {return list;}

      // Get the level of the argument.
      size_t level() const {return level(value);}

      // Get the list of each value.
      std::set<std::string>& get_list() {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        if (!list) throw std::runtime_error("Argument must be a list: " + long_name);
        return list_val;
      }

      // Get the list of each value.
      std::set<std::string>& get_valid() {return valid;}

      std::vector<std::pair<std::string, std::string>> get_modlist() const {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        if (!list) throw std::runtime_error("Argument is not a list: " + long_name);
        if (custom != custom_policy::MODIFIABLE) throw std::runtime_error("List must allowed modifiers!");

        std::vector<std::pair<std::string, std::string>> ret;
        for (const auto& val : list_val) {
          if (val.contains(':')) {
            auto s = shared::init<shared::vector>(shared::split, val, ':', false);
            ret.emplace_back(std::pair{s[0], s[1]});
          }
          else ret.emplace_back(std::pair{val, ""});
        }
        return ret;
      }

      // Get the position of the argument.
      const size_t& position() const {return positional;}

      // For booleans.
      operator bool() const {
        if (mandatory && !set) throw std::runtime_error("Missing mandatory argument: " + long_name);
        return level() != 0;
      }
  };


  // Mapping of all switches.
  extern std::map<std::string, arg::Arg> switches;

  // Unknown arguments.
  extern std::vector<std::string> unknown;

  // The arguments.
  extern std::vector<std::string> args;


  // Helper functions.
  inline const Arg& at(const std::string& key) {return switches.at(key);}
  inline std::string& get(const std::string& key) {return switches.at(key).get();}
  inline std::string& mod(const std::string& key) {return switches.at(key).mod();}
  inline size_t level(const std::string& key) {return switches.at(key).level();}
  inline std::set<std::string>& list(const std::string& key) {return switches.at(key).get_list();}
  inline std::set<std::string>& valid(const std::string& key) {return switches.at(key).get_valid();}
  inline const bool& is_list(const std::string& key) {return switches.at(key).is_list();}
  inline std::vector<std::pair<std::string, std::string>> modlist(const std::string& key) {return switches.at(key).get_modlist();}

  /**
   * @brief Parse command line arguments.
   */
  void parse_args();
}
