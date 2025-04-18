#include "syscalls.hpp"
#include "arguments.hpp"

#include <seccomp.h>
#include <fstream>
#include <cassert>

using namespace shared;
using namespace exec;

namespace syscalls {

  // Create a filter.
  std::string filter(const std::string& application) {

    // Don't make a filter if we aren't supposed to.
    if (arg::at("seccomp") < "permissive") return "";

    // Get our files.
    auto syscall_file = app_data / "syscalls.txt",
    bpf = app_data / "filter.bpf";

    // If we are lacking a syscall file, permissive can just create one, but there's
    // nothing we can do except panic if we're expected to enforce something that doesn't
    // exist.
    if (!std::filesystem::exists(syscall_file)) {
      if (arg::get("seccomp") == "enforcing")
        throw std::runtime_error("Cannot enforce a non-existent syscall policy!");

      // Otherwise we're in permissive mode, so just make the file.
      else {
        log({"Creating syscalls.txt file, check logs to populate!"});
        std::ofstream file(syscall_file);
      }
    }

    // See if the existing file is already up to date, and just use that directly.
    auto content = file::parse<vector>(syscall_file, vectorize);
    if (content.size() > 1) {
      auto hash = std::string(arg::get("seccomp") == "enforcing" ? "E" : "P") + shared::hash(content[1]);
      if (
        hash == container::init<vector>(container::split<vector, char>, content[0], ' ', false)[1] &&
        std::filesystem::exists(bpf) && arg::at("update") < "cache" &&
        arg::get("shell") != "debug" && arg::at("verbose") < "error" &&
        !arg::at("stats")
      ) {
        log({"Using cached SECCOMP filter"});
        return bpf;
      }
    }
    else if (content.size() == 1) {
      if (content[0].starts_with("HASH:")) content = {"0", ""};
      else content = {"0", content[0]};
    }
    else content = {"0", ""};

    // Get the syscalls that should be allowed
    set syscalls = container::init<set>(container::split<set, char>, content[1], ' ', false);
    log({arg::get("seccomp") == "enforcing" ? "Enforced": "Logged", "Syscalls:", std::to_string(syscalls.size())});

    // Add necessary syscalls
    if (arg::get("shell") == "debug")
      extend(syscalls, {"getpgrp", "pselect6"});
    if (arg::at("verbose") >= "error")
      extend(syscalls, {"ptrace", "kill"});

    // Setup the filter.
    auto filter = seccomp_init(arg::get("seccomp") == "enforcing" ? SCMP_ACT_ERRNO(EPERM) : SCMP_ACT_LOG);
    if (filter == nullptr)
      throw std::runtime_error("Failed to initialize SECCOMP!");
    if (seccomp_api_get() < 3)
      throw std::runtime_error("SECCOMP API is too old! Update your kernel!");

    // Set all the settings we want.
    assert(seccomp_attr_set(filter, SCMP_FLTATR_CTL_NNP, true) == 0);
    assert( seccomp_attr_set(filter, SCMP_FLTATR_CTL_TSYNC, true) == 0);
    assert(seccomp_attr_set(filter, SCMP_FLTATR_CTL_OPTIMIZE, 2) == 0);
    if (arg::at("verbose"))
      assert(seccomp_attr_set(filter, SCMP_FLTATR_CTL_LOG, true) == 0);

    // go through each syscall and add it
    for (const auto& syscall : syscalls) {
      auto resolved = seccomp_syscall_resolve_name(syscall.c_str());
      if (resolved < 0) throw std::runtime_error("Unrecognized syscall: "  + syscall);
      seccomp_rule_add(filter, SCMP_ACT_ALLOW, resolved, 0);
    }

    // Write out.
    auto bpf_file = fopen(bpf.c_str(), "w");
    if (bpf_file == nullptr)
      throw std::runtime_error("Failed to open BPF file!");
    seccomp_export_bpf(filter, fileno(bpf_file));
    fflush(bpf_file);
    fclose(bpf_file);

    // Update the hash.
    if (arg::at("verbose") < "error" && arg::get("shell") != "debug") {
      auto hash = shared::hash(content[1] + arg::get("seccomp"));
      auto cache_file = std::ofstream(syscall_file);
      cache_file << "HASH: " << hash << "\n";
      cache_file << content[1];
      cache_file.close();
    }

    if (arg::at("stats")) {
      // x86 Syscalls
      float start = 335.0f, final = syscalls.size();
      std::cout << "Reduced Total Syscalls By: " << ((start - final) / fabs(start)) * 100.0f << "%"
                << " (" << start << " to " << final << ")" << std::endl;
    }

    // Release the filter and return the path to the filter so the main process can open it.
    seccomp_release(filter);
    return bpf;
  }


  // Update the policy based on strace output.
  void update_policy(const std::string& application, const vector &straced) {

    // Get our files.
    auto syscall_file = app_data / "syscalls.txt";
    set syscalls;
    if (std::filesystem::exists(syscall_file)) {
      auto content = file::parse<vector>(syscall_file, vectorize);
      switch (content.size()) {
        case 1:  if (!content[0].starts_with("HASH")) container::split<set, char>(syscalls, content[0], ' ', false); break;
        case 2: container::split<set, char>(syscalls, content[1], ' '); break;
        default: break;
      }
    }

    log({"Existing syscalls:", std::to_string(syscalls.size())});

    for (const auto& line : straced) {
      if (line.contains('(')) {
        auto start = line.find("] ");
        if (start == std::string::npos) continue;
        start += 2;

        auto end = line.find('(', start);
        if (end == std::string::npos) continue;

        auto syscall = line.substr(start, end - start);
        if (seccomp_syscall_resolve_name(syscall.c_str()) != __NR_SCMP_ERROR && !syscalls.contains(syscall)) {
          log({"New syscall:", syscall});
          syscalls.emplace(syscall);
        }
      }
    }

    auto joined = join(syscalls, ' ');
    auto out = std::ofstream(syscall_file);
    out << "HASH: " << 'P' << shared::hash(joined) << '\n' << joined;
    out.close();
  }
}
