#ifndef NPS_TOOLS_DEVICE_READINGS_H
#define NPS_TOOLS_DEVICE_READINGS_H

#include <limits>
#include <string>
#include <vector>

// What a handheld run comes back with. The writer takes these off the screen and the reader judges
// them, so the two need the same names, and one table here is what keeps them from drifting apart.

namespace nps_tools {

struct Readings {
    std::string manifest_line;
    std::string launch_header;
    std::string solve;
    long hardware_type = -1;
    long hardware_subtype = -1;
    long os_index = -1;
    long ndl_revision = -1;
    long outside_writes = -1;
    long offline_solves = -1;
    long offline_needed_link = -1;
    long launch_red = -1;
    long browse_presses = -1;
    long browse_total_ms = -1;
    long giac_calls_before_browse = -1;
    long giac_calls_after_browse = -1;
    long first_step_ms = -1;
    long total_ms = -1;
    long free_kb = -1;
    long contiguous_kb = -1;
};

struct TextReading {
    const char *name;
    std::string Readings::*field;
};

struct NumberReading {
    const char *name;
    long Readings::*field;
};

inline const std::vector<TextReading> &text_readings() {
    static const std::vector<TextReading> table = {
        {"manifest-line", &Readings::manifest_line},
        {"launch-header", &Readings::launch_header},
        {"solve", &Readings::solve},
    };
    return table;
}

inline const std::vector<NumberReading> &number_readings() {
    static const std::vector<NumberReading> table = {
        {"hardware-type", &Readings::hardware_type},
        {"hardware-subtype", &Readings::hardware_subtype},
        {"os-index", &Readings::os_index},
        {"ndl-revision", &Readings::ndl_revision},
        {"outside-writes", &Readings::outside_writes},
        {"offline-solves", &Readings::offline_solves},
        {"offline-needed-link", &Readings::offline_needed_link},
        {"launch-red", &Readings::launch_red},
        {"browse-presses", &Readings::browse_presses},
        {"browse-total-ms", &Readings::browse_total_ms},
        {"giac-calls-before-browse", &Readings::giac_calls_before_browse},
        {"giac-calls-after-browse", &Readings::giac_calls_after_browse},
        {"first-step-ms", &Readings::first_step_ms},
        {"total-ms", &Readings::total_ms},
        {"free-kb", &Readings::free_kb},
        {"contiguous-kb", &Readings::contiguous_kb},
    };
    return table;
}

inline bool reading_number_of(const std::string &text, long *value) {
    if (text.empty())
        return false;
    long parsed = 0;
    for (char character : text) {
        if (character < '0' || character > '9')
            return false;
        const long digit = character - '0';
        // Refused rather than wrapped, because a number that does not fit is not the one the record
        // wrote, and the wrap is silent: twenty nines came back as 7766279631452241919.
        if (parsed > (std::numeric_limits<long>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

// A tab or a newline in a reading would forge a field, so it is refused rather than escaped.
inline bool reading_is_one_line(const std::string &text) {
    return text.find('\t') == std::string::npos && text.find('\n') == std::string::npos &&
           text.find('\r') == std::string::npos;
}

enum class ReadingResult {
    Taken,
    UnknownName,
    Malformed,
    Repeated,
};

// A reading given twice is refused rather than overwritten, so neither copy is silently the one judged.
inline ReadingResult assign_reading(Readings *readings, const std::string &name,
                                    const std::string &value) {
    if (!reading_is_one_line(value))
        return ReadingResult::Malformed;
    for (const TextReading &reading : text_readings()) {
        if (name != reading.name)
            continue;
        if (value.empty())
            return ReadingResult::Malformed;
        if (!(readings->*reading.field).empty())
            return ReadingResult::Repeated;
        readings->*reading.field = value;
        return ReadingResult::Taken;
    }
    for (const NumberReading &reading : number_readings()) {
        if (name != reading.name)
            continue;
        if (readings->*reading.field >= 0)
            return ReadingResult::Repeated;
        return reading_number_of(value, &(readings->*reading.field)) ? ReadingResult::Taken
                                                                     : ReadingResult::Malformed;
    }
    return ReadingResult::UnknownName;
}

// Names the first reading still missing, so a refusal can say which one rather than that one is.
inline std::string first_missing_reading(const Readings &readings) {
    for (const TextReading &reading : text_readings()) {
        if ((readings.*reading.field).empty())
            return reading.name;
    }
    for (const NumberReading &reading : number_readings()) {
        if (readings.*reading.field < 0)
            return reading.name;
    }
    return std::string();
}

inline std::size_t reading_count() { return text_readings().size() + number_readings().size(); }

}  // namespace nps_tools

#endif
