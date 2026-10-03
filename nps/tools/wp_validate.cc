// WP Milestone 0's host-side validator. Reads every authored ProblemIR in a directory, validates it
// against its own source text and commits it, and fails on the first file that does not.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nps/wp/problem_ir.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cout << "usage: nps_wp_validate <corpus directory>\n";
        return 2;
    }
    std::vector<std::string> files;
    std::error_code ec;
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(argv[1], ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ir")
            files.push_back(entry.path().string());
    }
    if (ec || files.empty()) {
        std::cout << "wp corpus: no .ir files in " << argv[1] << "\n";
        return 1;
    }
    std::sort(files.begin(), files.end());
    size_t committed = 0;
    for (const std::string &path : files) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const nps::wp::IrReadResult read = nps::wp::read_problem_ir(text.str());
        if (read.status != nps::wp::IrReadStatus::Ok) {
            std::cout << "wp corpus: " << path << ":" << read.line << ": "
                      << nps::wp::ir_read_status_name(read.status) << ", " << read.detail << "\n";
            return 1;
        }
        nps::wp::IrValidation why;
        if (!nps::wp::commit(read.ir, read.source, &why)) {
            std::cout << "wp corpus: " << path << ": " << nps::wp::ir_fault_name(why.fault) << ", " << why.detail << "\n";
            return 1;
        }
        ++committed;
    }
    std::cout << "wp corpus: " << committed << " of " << files.size() << " authored problems validated and committed\n";
    return 0;
}
