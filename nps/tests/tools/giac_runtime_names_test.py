#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, declaration):
    match = re.search(r"^  " + re.escape(declaration) + r"[^\n]*\{\n.*?^  \}", source, re.M | re.S)
    if not match:
        raise RuntimeError("missing production function: " + declaration)
    return match.group(0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--runtime-name", choices=("ndl", "ndless"), default="ndl")
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    source = args.source.read_text()
    functions = []
    if "  bool is_ndl_installer(" in source:
        functions.append(function(source, "bool is_ndl_installer("))
    functions.extend(function(source, declaration) for declaration in (
        "void nspire_copy_data(", "DIR * nspire_clear_data("))
    harness = r'''
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using std::string;
using BYTE = unsigned char;
constexpr int SHA256_BLOCK_SIZE = 32;
#define GIAC_CONTEXT void *contextptr
struct dirent { const char *d_name; };
struct DIR { std::vector<string> names; size_t position = 0; dirent entry; };
static DIR directory;
static string opened_path;
static std::vector<string> created, removed, checked;
static std::vector<std::pair<string, string>> copied;
static std::ostringstream log_stream;
static DIR *opendir(const char *path) {
    if (path != opened_path) return nullptr;
    directory.position = 0;
    return &directory;
}
static dirent *readdir(DIR *dir) {
    if (dir->position == dir->names.size()) return nullptr;
    dir->entry.d_name = dir->names[dir->position++].c_str();
    return &dir->entry;
}
static int closedir(DIR *) { return 0; }
static int mkdir(const char *path, int) { created.emplace_back(path); return 0; }
static int rmdir(const char *path) { removed.emplace_back(path); return 0; }
static int unlink(const char *path) { removed.emplace_back(path); return 0; }
static void cp(const char *from, const char *to) { copied.emplace_back(from, to); }
static std::ostream *logptr(void *) { return &log_stream; }
static bool sha_check(const char *path, int, BYTE[][SHA256_BLOCK_SIZE]) {
    checked.emplace_back(path);
    return string(path).find("tampered") == string::npos;
}
@FUNCTIONS@
int main() {
    const string runtime = "@RUNTIME@";
    const string visible = "/documents/" + runtime;
    const string exam = "/exammode/usr/" + runtime;
    const string installer = runtime + "_installer_cxii-6.4.0.74.tns";
    const string tampered = runtime + "_installer_tampered.tns";
    const string resources = runtime + "_resources.tns";
    const string config = runtime + ".cfg.tns";
    const string misleading = "prefix_" + installer;
    const string missing_separator = runtime + "_installerx.tns";
    directory.names = {installer, resources, config, misleading, missing_separator, tampered};
    BYTE hash[1][SHA256_BLOCK_SIZE] = {};
    opened_path = visible;
    nspire_copy_data(1, hash, nullptr);
    unsigned failures = 0, checks = 0;
    const auto check = [&](bool condition, const char *message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    check(created == std::vector<string>{exam}, "copy creates the current runtime directory");
    for (const auto &name : {installer, resources, config, tampered})
        check(std::find(copied.begin(), copied.end(), std::make_pair(visible + "/" + name, exam + "/" + name)) != copied.end(),
              "copy includes an approved runtime filename");
    check(copied.size() == 4, "copy rejects misleading prefixes and missing separators");
    opened_path = exam;
    nspire_clear_data(exam.c_str(), 1, hash, nullptr);
    for (const auto &name : {installer, resources, config}) {
        check(std::find(removed.begin(), removed.end(), exam + "/" + name) == removed.end(),
              "clear preserves a verified runtime file");
        check(std::find(checked.begin(), checked.end(), exam + "/" + name) != checked.end(),
              "clear verifies a runtime file before preserving it");
    }
    for (const auto &name : {misleading, missing_separator, tampered})
        check(std::find(removed.begin(), removed.end(), exam + "/" + name) != removed.end(),
              "clear rejects misleading or unverified runtime files");
    check(removed.size() == 3, "clear removes exactly the rejected files");
    std::cout << "Giac runtime files: " << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
'''.replace("@FUNCTIONS@", "\n".join(functions)).replace("@RUNTIME@", args.runtime_name)
    with tempfile.TemporaryDirectory(prefix="nps-giac-runtime-") as temporary:
        directory = Path(temporary)
        (directory / "runtime.cc").write_text(harness)
        subprocess.run([args.compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        str(directory / "runtime.cc"), "-o", str(directory / "runtime")], check=True)
        subprocess.run([str(directory / "runtime")], check=True)


if __name__ == "__main__":
    main()
