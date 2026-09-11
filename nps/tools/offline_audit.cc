#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sha256.h"

namespace {

struct SyscallRange {
    unsigned first;
    unsigned last;
    const char *prefix;
    const char *what;
};

const SyscallRange kHostReaching[] = {
    {227, 258, "usbd_", "the USB device stack"},
    {267, 296, "TI_NN_", "the NavNet link protocol"},
};

const char *const kExtraHostReachingNames[] = {"usb_register_driver"};

struct Section {
    std::string name;
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint32_t addr = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t entsize = 0;
};

struct Symbol {
    std::string name;
    std::uint32_t value = 0;
    std::uint32_t size = 0;
    unsigned char info = 0;
};

struct Instruction {
    std::uint32_t address = 0;
    unsigned number = 0;
};

struct Image {
    std::string path;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Instruction> syscalls;
    bool has_symbol_table = false;
    bool has_thumb_code = false;
};

bool read_whole_file(const std::string &path, std::vector<unsigned char> *bytes) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streampos end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    bytes->resize(static_cast<std::size_t>(end));
    if (bytes->empty())
        return true;
    in.read(reinterpret_cast<char *>(bytes->data()), end);
    return in.good() || in.eof();
}

bool in_bounds(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t length) {
    return at <= bytes.size() && length <= bytes.size() - at;
}

std::uint16_t read_u16(const std::vector<unsigned char> &bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t read_u32(const std::vector<unsigned char> &bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::string read_string(const std::vector<unsigned char> &bytes, std::size_t table,
                        std::uint32_t at) {
    std::string text;
    std::size_t index = table + at;
    while (index < bytes.size() && bytes[index] != 0) {
        text.push_back(static_cast<char>(bytes[index]));
        ++index;
    }
    return text;
}

bool starts_with(const std::string &text, const char *prefix) {
    std::size_t index = 0;
    while (prefix[index] != '\0') {
        if (index >= text.size() || text[index] != prefix[index])
            return false;
        ++index;
    }
    return true;
}

bool host_reaching_number(unsigned number, const SyscallRange **range) {
    for (const SyscallRange &candidate : kHostReaching) {
        if (number >= candidate.first && number <= candidate.last) {
            *range = &candidate;
            return true;
        }
    }
    return false;
}

bool host_reaching_name(const std::string &name) {
    for (const SyscallRange &range : kHostReaching) {
        if (starts_with(name, range.prefix))
            return true;
    }
    for (const char *extra : kExtraHostReachingNames) {
        if (name == extra)
            return true;
    }
    return false;
}

// ARM encodes SWI as condition, 1111, then a 24-bit immediate. The 1111 condition is the
// unconditional instruction space rather than a SWI, so it is excluded.
bool decode_syscall(std::uint32_t word, unsigned *number) {
    if ((word & 0x0F000000u) != 0x0F000000u)
        return false;
    if ((word & 0xF0000000u) == 0xF0000000u)
        return false;
    *number = word & 0x00FFFFFFu;
    return true;
}

bool load_image(const std::string &path, Image *image, std::string *error) {
    std::vector<unsigned char> bytes;
    if (!read_whole_file(path, &bytes)) {
        *error = "cannot be read";
        return false;
    }
    if (bytes.size() < 52 || bytes[0] != 0x7F || bytes[1] != 'E' || bytes[2] != 'L' ||
        bytes[3] != 'F') {
        *error = "is not an ELF file";
        return false;
    }
    if (bytes[4] != 1 || bytes[5] != 1) {
        *error = "is not a 32-bit little-endian ELF";
        return false;
    }
    if (read_u16(bytes, 18) != 40) {
        *error = "is not an ARM image";
        return false;
    }

    const std::uint32_t section_offset = read_u32(bytes, 32);
    const std::uint16_t section_size = read_u16(bytes, 46);
    const std::uint16_t section_count = read_u16(bytes, 48);
    const std::uint16_t name_index = read_u16(bytes, 50);
    if (section_size != 40 || section_count == 0 || name_index >= section_count) {
        *error = "has no usable section table";
        return false;
    }

    std::vector<Section> raw;
    for (std::uint16_t index = 0; index < section_count; ++index) {
        const std::size_t at = section_offset + static_cast<std::size_t>(index) * section_size;
        if (!in_bounds(bytes, at, section_size)) {
            *error = "has a section header outside the file";
            return false;
        }
        Section section;
        section.type = read_u32(bytes, at + 4);
        section.flags = read_u32(bytes, at + 8);
        section.addr = read_u32(bytes, at + 12);
        section.offset = read_u32(bytes, at + 16);
        section.size = read_u32(bytes, at + 20);
        section.link = read_u32(bytes, at + 24);
        section.entsize = read_u32(bytes, at + 36);
        raw.push_back(section);
    }

    const std::size_t names = raw[name_index].offset;
    for (std::uint16_t index = 0; index < section_count; ++index) {
        const std::size_t at = section_offset + static_cast<std::size_t>(index) * section_size;
        raw[index].name = read_string(bytes, names, read_u32(bytes, at));
    }
    image->path = path;
    image->sections = raw;

    for (const Section &section : raw) {
        if (section.type != 2 || section.entsize != 16)
            continue;
        image->has_symbol_table = true;
        if (section.link >= raw.size()) {
            *error = "has a symbol table with no string table";
            return false;
        }
        const std::size_t strings = raw[section.link].offset;
        for (std::uint32_t at = 0; at + 16 <= section.size; at += 16) {
            const std::size_t entry = section.offset + at;
            if (!in_bounds(bytes, entry, 16)) {
                *error = "has a symbol table outside the file";
                return false;
            }
            Symbol symbol;
            symbol.name = read_string(bytes, strings, read_u32(bytes, entry));
            symbol.value = read_u32(bytes, entry + 4);
            symbol.size = read_u32(bytes, entry + 8);
            symbol.info = bytes[entry + 12];
            if ((symbol.info & 0x0Fu) == 2 && (symbol.value & 1u) != 0)
                image->has_thumb_code = true;
            image->symbols.push_back(symbol);
        }
    }

    for (const Section &section : raw) {
        if ((section.flags & 0x4u) == 0 || section.type == 8)
            continue;
        if (!in_bounds(bytes, section.offset, section.size)) {
            *error = "has an executable section outside the file";
            return false;
        }
        for (std::uint32_t at = 0; at + 4 <= section.size; at += 4) {
            unsigned number = 0;
            if (!decode_syscall(read_u32(bytes, section.offset + at), &number))
                continue;
            Instruction found;
            found.address = section.addr + at;
            found.number = number;
            image->syscalls.push_back(found);
        }
    }
    return true;
}

std::string enclosing_symbol(const Image &image, std::uint32_t address) {
    for (const Symbol &symbol : image.symbols) {
        if ((symbol.info & 0x0Fu) != 2 || symbol.name.empty())
            continue;
        if (address >= symbol.value && address < symbol.value + symbol.size)
            return symbol.name;
    }
    return "an unnamed region";
}

struct Verdict {
    bool readable = false;
    bool reaches_host = false;
    std::vector<std::string> detail;
};

Verdict audit(const std::string &path) {
    Verdict verdict;
    Image image;
    std::string error;
    if (!load_image(path, &image, &error)) {
        verdict.detail.push_back(path + " " + error);
        return verdict;
    }
    if (!image.has_symbol_table) {
        verdict.detail.push_back(path + " has no symbol table, so it cannot be read by name");
        return verdict;
    }
    // A Thumb SWI is a halfword the word scan would step over, so an image carrying any is refused
    // rather than half checked.
    if (image.has_thumb_code) {
        verdict.detail.push_back(path + " contains Thumb code, which this scan does not decode");
        return verdict;
    }
    verdict.readable = true;

    for (const Instruction &found : image.syscalls) {
        const SyscallRange *range = nullptr;
        if (!host_reaching_number(found.number, &range))
            continue;
        verdict.reaches_host = true;
        verdict.detail.push_back(path + " issues syscall " + std::to_string(found.number) + " into " +
                                 range->what + ", from " + enclosing_symbol(image, found.address));
    }
    for (const Symbol &symbol : image.symbols) {
        if ((symbol.info & 0x0Fu) != 2 || !host_reaching_name(symbol.name))
            continue;
        verdict.reaches_host = true;
        verdict.detail.push_back(path + " links the stub " + symbol.name);
    }
    return verdict;
}

struct Numbering {
    bool readable = false;
    std::vector<std::string> disagreements;
};

bool define_of(const std::string &line, std::string *name, unsigned *number) {
    const std::string marker = "#define e_";
    if (line.compare(0, marker.size(), marker) != 0)
        return false;
    std::size_t at = marker.size();
    const std::size_t start = at;
    while (at < line.size() && line[at] != ' ' && line[at] != '\t')
        ++at;
    if (at == start || at >= line.size())
        return false;
    *name = line.substr(start, at - start);
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
        ++at;
    if (at >= line.size() || line[at] < '0' || line[at] > '9')
        return false;
    unsigned value = 0;
    while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
        value = value * 10 + static_cast<unsigned>(line[at] - '0');
        ++at;
    }
    *number = value;
    return true;
}

// The ranges above are only meaningful while the SDK still numbers those calls that way, so the
// header is read and both directions are checked: every named call inside its range, and no other
// name inside it.
Numbering check_numbering(const std::string &path) {
    Numbering numbering;
    std::ifstream in(path.c_str());
    if (!in) {
        numbering.disagreements.push_back(path + " cannot be read");
        return numbering;
    }
    numbering.readable = true;
    std::string line;
    while (std::getline(in, line)) {
        std::string name;
        unsigned number = 0;
        if (!define_of(line, &name, &number))
            continue;
        const bool named = host_reaching_name(name);
        const SyscallRange *range = nullptr;
        const bool numbered = host_reaching_number(number, &range);
        if (named && !numbered)
            numbering.disagreements.push_back(name + " is host reaching but sits outside the audited "
                                                     "ranges at " + std::to_string(number));
        if (numbered && !named)
            numbering.disagreements.push_back(name + " sits inside " + range->what + " at " +
                                              std::to_string(number) + " but is not host reaching");
    }
    return numbering;
}

std::string basename_of(const std::string &path) {
    std::size_t at = path.size();
    while (at > 0 && path[at - 1] != '/')
        --at;
    return path.substr(at);
}

std::string hex_digest_of(const std::string &path) {
    std::vector<unsigned char> bytes;
    if (!read_whole_file(path, &bytes))
        return std::string();
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, bytes.data(), bytes.size());
    BYTE digest[SHA256_BLOCK_SIZE];
    sha256_final(&hash, digest);
    static const char nibbles[] = "0123456789abcdef";
    std::string text;
    for (BYTE byte : digest) {
        text.push_back(nibbles[byte >> 4]);
        text.push_back(nibbles[byte & 0x0F]);
    }
    return text;
}

// The audit does not write evidence itself. It writes a record naming the packaged artifact and its
// digest, and tools/device_evidence.cc turns that into evidence only if the artifact still hashes to
// what is written here. One road for everything that claims something about a device.
bool write_record(const std::string &path, const std::string &package, bool clean,
                  std::size_t image_count, std::string *error) {
    const std::string digest = hex_digest_of(package);
    if (digest.empty()) {
        *error = "the packaged artifact could not be read";
        return false;
    }
    std::ofstream out(path.c_str());
    if (!out) {
        *error = "the record could not be written";
        return false;
    }
    out << "device\tschema\t1\n"
        << "device\tsource\toffline audit\n"
        << "device\tartifact\t" << basename_of(package) << "\n"
        << "device\tdigest\t" << digest << "\n"
        << "check\tPLAT-008\t" << (clean ? "pass\t" : "fail\t")
        << "no shipped device image links or issues a call into the USB device stack or the NavNet "
           "link protocol, proved against "
        << image_count << (image_count == 1 ? " image" : " images")
        << " and falsified against a fixture that does\n";
    return out.good();
}

void report(const std::vector<std::string> &lines) {
    for (const std::string &line : lines)
        std::cout << "offline audit: " << line << "\n";
}

}  // namespace

int main(int argc, char **argv) {
    std::string syscall_list;
    std::string dirty;
    std::string record;
    std::string package;
    std::vector<std::string> images;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--syscall-list" && index + 1 < argc) {
            syscall_list = argv[++index];
            continue;
        }
        if (argument == "--dirty" && index + 1 < argc) {
            dirty = argv[++index];
            continue;
        }
        if (argument == "--record" && index + 1 < argc) {
            record = argv[++index];
            continue;
        }
        if (argument == "--package" && index + 1 < argc) {
            package = argv[++index];
            continue;
        }
        images.push_back(argument);
    }
    if (syscall_list.empty() || dirty.empty() || images.empty() ||
        record.empty() != package.empty()) {
        std::cout << "usage: nps_offline_audit --syscall-list <syscall-list.h> --dirty <elf> "
                     "[--record <path> --package <tns>] <elf>...\n";
        return 2;
    }

    const Numbering numbering = check_numbering(syscall_list);
    if (!numbering.readable || !numbering.disagreements.empty()) {
        report(numbering.disagreements);
        std::cout << "offline audit: the audited ranges no longer match the SDK, so nothing was "
                     "checked\n";
        return 1;
    }

    // An audit that has never rejected anything is indistinguishable from one that cannot, so the
    // fixture that does call out is checked before the images that should not.
    const Verdict fixture = audit(dirty);
    if (!fixture.readable || !fixture.reaches_host) {
        report(fixture.detail);
        std::cout << "offline audit: SELFTEST FAILED, " << dirty
                  << " calls out and was not flagged\n";
        return 1;
    }

    bool clean = true;
    for (const std::string &image : images) {
        const Verdict verdict = audit(image);
        if (!verdict.readable || verdict.reaches_host) {
            report(verdict.detail);
            clean = false;
            continue;
        }
        std::cout << "offline audit: " << image << " reaches no host\n";
    }

    if (!record.empty() && !package.empty()) {
        std::string error;
        if (!write_record(record, package, clean, images.size(), &error)) {
            std::cout << "offline audit: record not written, " << error << "\n";
            return 1;
        }
    }
    return clean ? 0 : 1;
}
