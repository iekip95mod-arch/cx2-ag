#ifndef NPS_TOOLS_SCRATCH_DIRECTORY_H
#define NPS_TOOLS_SCRATCH_DIRECTORY_H

#include <stdlib.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nps {

// A selftest's staging directory. It is made under NPS_SCRATCH_DIR when the suite names one, which
// CTest points into the build tree, and under the system temporary directory otherwise. It is
// removed with everything in it when it goes out of scope, so a selftest leaves nothing behind.
class ScratchDirectory {
  public:
    explicit ScratchDirectory(const std::string &prefix) {
        std::error_code error;
        const char *named = std::getenv("NPS_SCRATCH_DIR");
        const std::filesystem::path base = named != nullptr && *named != '\0'
                                               ? std::filesystem::path(named)
                                               : std::filesystem::temp_directory_path(error);
        if (error)
            return;
        std::filesystem::create_directories(base, error);
        if (error)
            return;
        const std::string pattern = (base / (prefix + "XXXXXX")).string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        if (::mkdtemp(buffer.data()) != nullptr)
            path_ = buffer.data();
    }

    ~ScratchDirectory() {
        if (path_.empty())
            return;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ScratchDirectory(const ScratchDirectory &) = delete;
    ScratchDirectory &operator=(const ScratchDirectory &) = delete;

    // Empty when the directory could not be made, which every caller already treats as a failure.
    const std::string &path() const { return path_; }

  private:
    std::string path_;
};

}  // namespace nps

#endif
