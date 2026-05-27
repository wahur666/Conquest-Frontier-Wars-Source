#ifndef CFW_REBUILD_VTDUMP_H
#define CFW_REBUILD_VTDUMP_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include "fdump.h"
#include "json.hpp"

namespace vtdump {

inline const char* default_log_path() {
    return "vtdump.log";
}

inline std::mutex& log_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::string now_local_time() {
    SYSTEMTIME time;
    GetLocalTime(&time);

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << time.wYear << '-'
        << std::setw(2) << time.wMonth << '-'
        << std::setw(2) << time.wDay << ' '
        << std::setw(2) << time.wHour << ':'
        << std::setw(2) << time.wMinute << ':'
        << std::setw(2) << time.wSecond << '.'
        << std::setw(3) << time.wMilliseconds;
    return out.str();
}

inline void append_log_line(const std::string& text, const int row, const char* filename, const char* log_path = default_log_path()) {
    const char* source = filename != nullptr ? filename : "<unknown>";
    const char* path = log_path != nullptr ? log_path : default_log_path();

    std::lock_guard<std::mutex> lock(log_mutex());
    std::ofstream out(path, std::ios::out | std::ios::app);
    if (!out) {
        return;
    }

    out << '[' << now_local_time() << ']'
        << " pid=" << GetCurrentProcessId()
        << " tid=" << GetCurrentThreadId()
        << ' ' << source << ':' << row
        << " | " << text << '\n';
}

inline std::string make_text(const char* value) {
    return value != nullptr ? std::string(value) : std::string("<null>");
}

inline std::string make_text(char* value) {
    return make_text(static_cast<const char*>(value));
}

inline std::string make_text(const std::string& value) {
    return value;
}

inline std::string make_text(const nlohmann::json& value) {
    return value.dump(2);
}

template<typename T, typename = void>
struct is_streamable : std::false_type {};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};

template<typename T, typename = void>
struct is_json_serializable : std::false_type {};

template<typename T>
struct is_json_serializable<T, std::void_t<decltype(nlohmann::json(std::declval<const T&>()))>> : std::true_type {};

template<typename T>
std::string make_text(const T& value) {
    if constexpr (is_json_serializable<T>::value && !is_streamable<T>::value) {
        return nlohmann::json(value).dump(2);
    } else if constexpr (is_streamable<T>::value) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        std::ostringstream out;
        out << "<object type=\"" << typeid(T).name()
            << "\" address=0x" << std::hex << reinterpret_cast<std::uintptr_t>(&value)
            << " size=" << std::dec << sizeof(T) << ">";
        return out.str();
    }
}

template<typename T>
inline void file_log(const T& value, const int row, const char* filename, const char* log_path = default_log_path()) {
    append_log_line(make_text(value), row, filename, log_path);
}

inline void file_log_bytes(const void* data, const std::size_t size, const int row, const char* filename, const char* log_path = default_log_path()) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::ostringstream out;
    out << "bytes[" << size << "]";

    if (bytes == nullptr) {
        out << " <null>";
        append_log_line(out.str(), row, filename, log_path);
        return;
    }

    for (std::size_t offset = 0; offset < size; offset += 16) {
        out << "\n  " << std::setw(4) << std::setfill('0') << std::hex << offset << "  ";

        const std::size_t row_count = (std::min)(std::size_t{16}, size - offset);
        for (std::size_t i = 0; i < row_count; ++i) {
            out << std::setw(2) << static_cast<unsigned int>(bytes[offset + i]) << ' ';
        }

        for (std::size_t i = row_count; i < 16; ++i) {
            out << "   ";
        }

        out << " ";
        for (std::size_t i = 0; i < row_count; ++i) {
            const unsigned char ch = bytes[offset + i];
            out << (std::isprint(ch) ? static_cast<char>(ch) : '.');
        }
    }

    append_log_line(out.str(), row, filename, log_path);
}

} // namespace vtdump

#define VTDUMP_LOG(value) ::vtdump::file_log((value), __LINE__, __FILE__)
#define VTDUMP_LOG_TO(path, value) ::vtdump::file_log((value), __LINE__, __FILE__, (path))
#define VTDUMP_LOG_BYTES(data, size) ::vtdump::file_log_bytes((data), (size), __LINE__, __FILE__)
#define VTDUMP_LOG_BYTES_TO(path, data, size) ::vtdump::file_log_bytes((data), (size), __LINE__, __FILE__, (path))

/**
 * Dumps Vtable of object
 * Get the vtable with: `auto** vt = *(void***)obj;`
 * Link with "dbghelp"
 * @param vtable Vtable pointer
 * @param count Number of items to be listed
 * @param obj_name Debugger friendly name to find the output
 */
inline void dump_vtable_with_names(void** vtable, const int count, const char* obj_name) {
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    for (int i = 0; i < count; ++i) {
        DWORD64 addr = (DWORD64)vtable[i];
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        if (SymFromAddr(process, addr, nullptr, symbol)) {
            FDUMP(ErrorCode(ERR_GENERAL, SEV_NOTICE),
                  "VTable[%d]: %s::%s at %p\n", i, obj_name, symbol->Name, vtable[i]);
        } else {
            FDUMP(ErrorCode(ERR_GENERAL, SEV_NOTICE),
                  "VTable[%d]: unknown at %p\n", i, vtable[i]);
        }
    }
}

#endif //CFW_REBUILD_VTDUMP_H
