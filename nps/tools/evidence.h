// The evidence file as its appenders see it. It was a copy in each of the three tools that append,
// and they had already drifted on what a refused append means.
#ifndef NPS_TOOLS_EVIDENCE_H
#define NPS_TOOLS_EVIDENCE_H

#include <fstream>
#include <string>
#include <vector>

namespace nps_tools {

// The id a row names: upper-case letters, a hyphen, then digits, as PLAT-001 and WP-044. Scanned
// rather than matched, so the shape it accepts is the shape written here. It lives beside the append
// because a row naming anything else reaches the traceability report as an id nobody can look up.
inline bool is_requirement_id(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && s[i] >= 'A' && s[i] <= 'Z')
        ++i;
    if (i == 0 || i >= s.size() || s[i] != '-')
        return false;
    ++i;
    const size_t digits = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        ++i;
    return i > digits && i == s.size();
}

// A caller whose append is refused has to fail, because the absence of its rows reads off the report
// as a requirement nobody answered rather than as a tool that could not write.
inline bool append_evidence(const std::string &path, const std::string &after_group,
                            const std::string &group, const std::vector<std::string> &rows,
                            std::string *error) {
    std::ifstream existing(path.c_str());
    if (!existing) {
        *error = "the evidence file does not exist yet";
        return false;
    }
    const std::string wanted = "group\t" + after_group;
    std::string line;
    bool ran = false;
    while (std::getline(existing, line)) {
        if (line == wanted)
            ran = true;
    }
    existing.close();
    if (!ran) {
        *error = "the evidence file does not name the group " + after_group + " yet";
        return false;
    }
    std::ofstream out(path.c_str(), std::ios::app);
    if (!out) {
        *error = "could not append";
        return false;
    }
    out << "group\t" << group << "\n";
    for (size_t i = 0; i < rows.size(); ++i)
        out << rows[i] << "\n";
    return out.good();
}

}  // namespace nps_tools

#endif
