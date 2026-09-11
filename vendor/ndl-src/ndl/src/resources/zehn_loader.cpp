#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <memory>

#include <zlib.h>
#include <libndls.h>
#include <syscall-decls.h>
#include <zehn.h>

#include "ndl.h"
#include "ndl_version.h"
#include "zehn_loader.h"

// Lighter alternative to std::vector
template <typename T> class Storage
{
public:
	Storage(const size_t count) : m_count(count) { m_data = reinterpret_cast<T*>(malloc(sizeof(T) * m_count)); }
	Storage(const Storage<T> &s) = delete;
	~Storage() { free(m_data); }
	Storage<T> &operator=(const Storage<T> &s) = delete;

	T* begin() { return m_data; }
	T* end() { return m_data + m_count; }

	size_t size() { return m_count; }
	T* data() { return m_data; }

	bool alloc_failed() { return m_count > 0 && m_data == nullptr; }

private:
	const size_t m_count;
	T* m_data;
};

void msgbox(const char *title, const char *fmt, ...)
{
	char content[1024];

	va_list args;
	va_start(args, fmt);
	vsnprintf(content, sizeof(content), fmt, args);

	show_msgbox(title, content);

	va_end(args);
}

// Validates the string a Zehn_flag points to
bool zehn_check_string(const uint8_t *extra_data, const Zehn_flag flag, unsigned int max_length, const char **string)
{
	*string = reinterpret_cast<const char*>(extra_data) + flag.data;
	const char *ptr = *string;
        while(*ptr)
        {
                if(max_length == 0)
                	return false;

        	++ptr;
		--max_length;
	}

	return true;
}

static uint32_t ru32(void *ptr)
{
        if((reinterpret_cast<uintptr_t>(ptr) & 0b11) == 0)
            return *reinterpret_cast<uint32_t*>(ptr);

	uint32_t ret;
	memcpy(&ret, ptr, sizeof(ret));
	return ret;
}

static void wu32(void *ptr, uint32_t val)
{
        if((reinterpret_cast<uintptr_t>(ptr) & 0b11) == 0)
            *reinterpret_cast<uint32_t*>(ptr) = val;
        else
	    memcpy(ptr, &val, sizeof(val));
}

extern "C" int zehn_load(NUC_FILE *file, void **mem_ptr, int (**entry)(int,char*[]), bool *supports_hww)
{
	return zehn_load_diagnostic(file, mem_ptr, entry, supports_hww, nullptr);
}

extern "C" int zehn_load_diagnostic(NUC_FILE *file, void **mem_ptr, int (**entry)(int,char*[]), bool *supports_hww, struct ld_diagnostic *diagnostic)
{
	ld_diagnostic_set(diagnostic, "none", 0, 0);
	Zehn_header header;

	// The Zehn file may not begin at the file start
	size_t file_start = nuc_ftell(file);

	if(nuc_fread(&header, sizeof(header), 1, file) != 1)
		return ld_diagnostic_set(diagnostic, "zehn.header.read", 1, sizeof(header));

	if(header.signature != ZEHN_SIGNATURE || header.version != ZEHN_VERSION || header.file_size > header.alloc_size)
	{
		puts("[Zehn] This Zehn file is not supported!");
		return ld_diagnostic_set(diagnostic, "zehn.header.format", 1, 0);
	}

	Storage<Zehn_reloc> relocs(header.reloc_count);
	Storage<Zehn_flag> flags(header.flag_count);
	Storage<uint8_t> extra_data(header.extra_size);

	if(relocs.alloc_failed() || flags.alloc_failed() || extra_data.alloc_failed())
	{
		puts("[Zehn] Allocation of metadata failed!");
		size_t bytes = relocs.alloc_failed() ? sizeof(Zehn_reloc) * relocs.size()
			: flags.alloc_failed() ? sizeof(Zehn_flag) * flags.size() : extra_data.size();
		return ld_diagnostic_set(diagnostic, "zehn.metadata.allocate", 1, bytes);
	}

	if(nuc_fread(reinterpret_cast<void*>(relocs.data()), sizeof(Zehn_reloc), header.reloc_count, file) != header.reloc_count
		|| nuc_fread(reinterpret_cast<void*>(flags.data()), sizeof(Zehn_flag), header.flag_count, file) != header.flag_count
		|| nuc_fread(reinterpret_cast<void*>(extra_data.data()), 1, header.extra_size, file) != header.extra_size)
	{
		puts("[Zehn] File read failed!");
		return ld_diagnostic_set(diagnostic, "zehn.metadata.read", 1, 0);
	}

	size_t remaining_mem = header.alloc_size - nuc_ftell(file) + file_start, remaining_file = header.file_size - nuc_ftell(file) + file_start;

	std::unique_ptr<uint8_t[], decltype(&execmem_free)> mem_allocation{reinterpret_cast<uint8_t*>(execmem_alloc(remaining_mem)), &execmem_free};
	uint8_t *base = mem_allocation.get();
	if(!base)
	{
		puts("[Zehn] Memory allocation failed!");
		return ld_diagnostic_set(diagnostic, "zehn.executable.allocate", 1, remaining_mem);
	}

	if(relocs.size() > 0 && relocs.data()[0].type == Zehn_reloc_type::FILE_COMPRESSED)
	{
		if(relocs.data()[0].offset != static_cast<int>(Zehn_compress_type::ZLIB))
		{
			puts("[Zehn] Compression format not supported!");
			return ld_diagnostic_set(diagnostic, "zehn.compression.format", 1, 0);
		}

		Storage<uint8_t> compressed(remaining_file);
		if(compressed.alloc_failed())
			return ld_diagnostic_set(diagnostic, "zehn.compressed.allocate", 1, remaining_file);
		if(nuc_fread(compressed.data(), remaining_file, 1, file) != 1)
		{
			puts("[Zehn] File read failed!");
			return ld_diagnostic_set(diagnostic, "zehn.compressed.read", 1, remaining_file);
		}

		uLongf dest_len = remaining_mem;
		int zlib_status = uncompress(base, &dest_len, compressed.data(), remaining_file);
		if(zlib_status != Z_OK)
		{
			puts("[Zehn] Decompression failed!");
			ld_diagnostic_set(diagnostic, "zehn.decompress", zlib_status, remaining_mem);
			return 1;
		}

		std::fill(base + dest_len, base + remaining_mem, 0);
	}
	else
	{
		if(nuc_fread(base, remaining_file, 1, file) != 1)
		{
			puts("[Zehn] File read failed!");
			return ld_diagnostic_set(diagnostic, "zehn.executable.read", 1, remaining_file);
		}
	
		// Fill rest with zeros (.bss and other NOBITS sections)
		std::fill(base + remaining_file, base + remaining_mem, 0);
	}

	const char *application_name = "(unknown)", *application_author = "(unknown)", *application_notice = "(no notice)";
	unsigned int application_version = 1, ndl_version_min = 0, ndl_version_max = UINT_MAX,
		ndl_revision_min = 0, ndl_revision_max = UINT_MAX;

	// Iterate through each flag
	for(Zehn_flag &f : flags)
	{
		const char *ptr;
		switch(f.type)
		{
		case Zehn_flag_type::EXECUTABLE_NAME:
			if(!zehn_check_string(extra_data.data(), f, 255, &application_name))
			{
				puts("[Zehn] Invalid application name!");
				return ld_diagnostic_set(diagnostic, "zehn.name", 1, 0);
			}

			break;
		case Zehn_flag_type::EXECUTABLE_NOTICE:
			if(zehn_check_string(extra_data.data(), f, 1024, &ptr))
				application_notice = ptr;

			break;
		case Zehn_flag_type::EXECUTABLE_AUTHOR:
			if(zehn_check_string(extra_data.data(), f, 128, &ptr))
				application_author = ptr;

			break;
		case Zehn_flag_type::EXECUTABLE_VERSION:
			application_version = f.data;
			break;
		case Zehn_flag_type::NDL_VERSION_MIN:
			ndl_version_min = f.data;
			break;
		case Zehn_flag_type::NDL_REVISION_MIN:
			ndl_revision_min = f.data;
			break;
		case Zehn_flag_type::NDL_VERSION_MAX:
			ndl_version_max = f.data;
			break;
		case Zehn_flag_type::NDL_REVISION_MAX:
			ndl_revision_max = f.data;
			break;
		case Zehn_flag_type::RUNS_ON_COLOR:
			if(f.data == false && has_colors)
			{
				msgbox("Error", "The application %s doesn't support CX and CM calculators!", application_name);
				return ld_diagnostic_set(diagnostic, "zehn.color", 2, 0);
			}
			break;
		case Zehn_flag_type::RUNS_ON_CLICKPAD:
			if(f.data == false && !is_touchpad)
			{
				msgbox("Error", "The application %s doesn't support clickpads!", application_name);
				return ld_diagnostic_set(diagnostic, "zehn.clickpad", 2, 0);
			}
			break;
		case Zehn_flag_type::RUNS_ON_TOUCHPAD:
			if(f.data == false && is_touchpad)
			{
				msgbox("Error", "The application %s doesn't support touchpads!", application_name);
				return ld_diagnostic_set(diagnostic, "zehn.touchpad", 2, 0);
			}
			break;
		case Zehn_flag_type::RUNS_ON_32MB:
			if(f.data == false && (!has_colors || is_cm))
			{
				msgbox("Error", "The application %s requires more than 32MB of RAM!", application_name);
				return ld_diagnostic_set(diagnostic, "zehn.ram", 2, 0);
			}
			break;
                case Zehn_flag_type::RUNS_ON_HWW:
                        *supports_hww = f.data;
			break;
		default:
			break;
		}
	}

	// Show some information about the executable
	if(isKeyPressed(KEY_NSPIRE_CAT))
	{
		msgbox("Information about the executable", "Name: %s Version: %u\nAuthor: %s\nNotice: %s", application_name, application_version, application_author, application_notice);
		return ld_diagnostic_set(diagnostic, "zehn.information", 2, 0);
	}

	if(NDL_VERSION < ndl_version_min || (NDL_VERSION == ndl_version_min && NDL_REVISION < ndl_revision_min))
	{
		msgbox("Error", "The application %s requires at least ndl %d.%d.%d!", application_name, ndl_version_min / 10, ndl_version_min % 10, ndl_revision_min);
		return ld_diagnostic_set(diagnostic, "zehn.version.minimum", 2, 0);
	}

	if(NDL_VERSION > ndl_version_max || (NDL_VERSION == ndl_version_max && NDL_REVISION > ndl_revision_max))
	{
		if(ndl_revision_max != UINT_MAX)
			msgbox("Error", "The application %s requires ndl %d.%d.%d or older!", application_name, ndl_version_max / 10, ndl_version_max % 10, ndl_revision_max);
		else
			msgbox("Error", "The application %s requires ndl %d.%d or older!", application_name, ndl_version_max / 10, ndl_version_max % 10);

		return ld_diagnostic_set(diagnostic, "zehn.version.maximum", 2, 0);
	}

	// Iterate through the reloc table
	for(Zehn_reloc &r : relocs)
	{
		if(r.offset >= remaining_mem)
		{
			puts("[Zehn] Wrong reloc in Zehn file!");
			return ld_diagnostic_set(diagnostic, "zehn.relocation.offset", 1, 0);
		}

		// No alignment guaranteed!
		uint32_t *place = reinterpret_cast<uint32_t*>(base + r.offset);
		switch(r.type)
		{
		//Handled above
		case Zehn_reloc_type::FILE_COMPRESSED:
                        break;
                case Zehn_reloc_type::UNALIGNED_RELOC:
                        if(r.offset != 0)
                        {
                            printf("[Zehn] Unexpected UNALIGNED_RELOC value %u!\n", r.offset);
                            return ld_diagnostic_set(diagnostic, "zehn.relocation.unaligned", 1, 0);
                        }

			break;
		case Zehn_reloc_type::ADD_BASE:
			wu32(place, ru32(place) + reinterpret_cast<uint32_t>(base));
			break;
		case Zehn_reloc_type::ADD_BASE_GOT:
		{
			uint32_t u32;
			while((u32 = ru32(place)) != 0xFFFFFFFF)
				wu32(place++, u32 + reinterpret_cast<uint32_t>(base));

			break;
		}
		case Zehn_reloc_type::SET_ZERO:
			wu32(place, 0);
			break;
		default:
			printf("[Zehn] Unsupported reloc %d!\n", static_cast<int>(r.type));
			return ld_diagnostic_set(diagnostic, "zehn.relocation.type", 1, 0);
		}
	}

	*mem_ptr = base;
	mem_allocation.release(); // Caller owns it now
	*entry = reinterpret_cast<int (*)(int,char*[])>(base + header.entry_offset);

	return 0;
}
