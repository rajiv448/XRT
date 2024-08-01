// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#endif /* #ifdef _WIN32 */

namespace xrt::tools::xbtracer {

#ifdef _MSC_VER
  // Visual Studio
  #define FUNCTION_SIGNATURE __FUNCSIG__
#else
  // GCC or other compilers
  #define FUNCTION_SIGNATURE __PRETTY_FUNCTION__ // NOLINT
#endif /* #ifdef _MSC_VER */

constexpr const char* xrt_trace_filename = "trace.txt";
constexpr const char* xrt_trace_bin_filename = "memdump.bin";
/*
 * enumration to identify the log type (Entry/Exit)
 * */
enum class trace_type {
  TRACE_TYPE_ENTRY,
  TRACE_TYPE_EXIT,
  TRACE_TYPE_INVALID
};

/*
 * membuf class to dump the the memory buffer.
 * */
class membuf
{
  private:
  unsigned char* ptr;
  unsigned int sz;

  public:
  membuf(unsigned char* uptr, unsigned int usz)
  {
    ptr = uptr;
    sz = usz;
  }

  friend std::ostream& operator<<(std::ostream& os, const membuf& mb)
  {
    for (unsigned int i = 0; i < mb.sz; i++)
      os << std::to_string(*(mb.ptr + i)) << " ";
    return os;
  }

  friend std::ofstream& operator<<(std::ofstream& ofs, const membuf& mb)
  {
    ofs.write("mem\0", 4);
    ofs.write((const char*)&mb.sz, sizeof(mb.sz));
    ofs.write((const char*)mb.ptr, mb.sz);
    return ofs;
  }
};

class logger
{
  private:
  void* handle = nullptr;
  std::ofstream fp;
  std::ofstream fp_bin;
  std::string program_name;
  bool inst_debug;
#ifdef _WIN32
  DWORD pid;
#else
  pid_t pid;
#endif /* #ifdef _WIN32 */
  std::string xrt_ver;
  struct timespec start_time{};

  /*
   * constructor
   * */
  logger();

  public:
  bool get_inst_debug()
  {
    return inst_debug;
  }

  void set_inst_debug(bool flag)
  {
    inst_debug = flag;
  }

  std::string os_name_ver();

  /*
   * Method to calculate the time-diffrence since start of the trace.
   * */
  std::string timediff(struct timespec now, struct timespec then);

  static logger& get_instance()
  {
    static logger ptr;
    return ptr;
  }

  std::ofstream& get_bin_fd()
  {
    return fp_bin;
  }
  /*
   * destructor
   * */
  ~logger();

  /*
   * API to capture Entry and Exit Trace.
   * */
  void log(trace_type type, std::string str);
};

/*
 * Wrapper api for time formating
 * */
inline std::tm localtime_xp(std::time_t timer)
{
  std::tm bt{};
#if defined(__unix__)
  localtime_r(&timer, &bt);
#elif defined(_MSC_VER)
  localtime_s(&bt, &timer);
#else
  static std::mutex mtx;
  std::lock_guard<std::mutex> lock(mtx);
  bt = *std::localtime(&timer);
#endif
  return bt;
}

template <typename... Args>
std::string stringify_args(const Args&... args)
{
  std::ostringstream oss;
  ((oss << args), ...);
  return oss.str();
}

template <typename T>
inline std::string mb_stringify(T a1)
{

  std::ofstream& fd = logger::get_instance().get_bin_fd();
  std::stringstream ss;
  std::streampos pos = fd.tellp();
  ss << "mem@0x" << std::hex << pos << "[filename:" << xrt_trace_bin_filename
     << "]";
  fd << a1;
  return ss.str();
}

template <typename... Args>
std::string concat_args(const Args&... args)
{
  std::ostringstream oss;
  bool first = true;
  ((oss << (first ? "" : ", ") << stringify_args(args), first = false), ...);
  return oss.str();
}

// Helper function to concatenate an argument and a value
template <typename Arg, typename Val>
std::string concat_arg_nv(const Arg& arg, const Val& val)
{
  if (!std::strcmp(typeid(membuf).name(), typeid(val).name()))
    return stringify_args(arg) + "=" + mb_stringify(val);
  else
    return stringify_args(arg) + "=" + stringify_args(val);
}

// Base case for recursive function to concatenate args and vals
template <typename... Args>
std::string concat_args_nv()
{
  return "";
}

// Recursive function to concatenate args and vals
template <typename Arg, typename Val, typename... Args, typename... Vals>
std::string concat_args_nv(const Arg& arg, const Val& val, const Args&... args,
                         const Vals&... vals)
{
  return concat_arg_nv(arg, val) +
         ((sizeof...(args) > 0 || sizeof...(vals) > 0) ? ", " : "") +
         concat_args_nv(args..., vals...);
}

// Function to perform find and replace operations
std::string find_and_replace_all(std::string str,
  const std::vector<std::pair<std::string, std::string>>& replacements);

inline std::string get_func_sig(const char* func_sig)
{
  std::vector<std::pair<std::string, std::string>> replacements =
  {
#ifdef _WIN32
    {"__cdecl ", ""},
    {"class ", ""},
    {" &", "&"},
    {"(void)", "()"},
    {"std::basic_string<char,struct std::char_traits<char>,"
        "std::allocator<char> >", "string"}
#else
    {") const", ")"}
#endif /* #ifdef _WIN32 */
  };

  return find_and_replace_all(std::string(func_sig), replacements);
}

#define XRT_TOOLS_XBT_LOG_ERROR(str)                                           \
  do                                                                           \
  {                                                                            \
    std::cerr << stringify_args(str, " is NULL @ ", __FILE__, ":L", __LINE__,  \
                               "\n");                                          \
  }                                                                            \
  while (0)

#define XRT_TOOLS_XBT_CALL_CTOR(fptr, ...) \
  do                                       \
  {                                        \
    if (fptr)                              \
      (fptr)(__VA_ARGS__);                 \
    else                                   \
      XRT_TOOLS_XBT_LOG_ERROR(#fptr);      \
  }                                        \
  while (0)

#define XRT_TOOLS_XBT_CALL_METD(fptr, ...) \
  do                                       \
  {                                        \
    if (fptr)                              \
      (this->*fptr)(__VA_ARGS__);          \
    else                                   \
      XRT_TOOLS_XBT_LOG_ERROR(#fptr);      \
  }                                        \
  while (0)

#define XRT_TOOLS_XBT_CALL_METD_RET(fptr, r, ...) \
  do                                              \
  {                                               \
    if (fptr)                                     \
      r = (this->*fptr)(__VA_ARGS__);             \
    else                                          \
      XRT_TOOLS_XBT_LOG_ERROR(#fptr);             \
  }                                               \
  while (0)

/******************************************************************************
 * macros to capture entry trace, this macro cam be called with no or upto 8
 * function argument
 ******************************************************************************/
#define XRT_TOOLS_XBT_FUNC_ENTRY(...)                                          \
  do                                                                           \
  {                                                                            \
    if ((nullptr == this) || (nullptr == this->get_handle()))                  \
    {                                                                          \
      XRT_TOOLS_XBT_LOG_ERROR("Handle");                                       \
      break;                                                                   \
    }                                                                          \
    auto __handle = this->get_handle();                                        \
    logger::get_instance().log(trace_type::TRACE_TYPE_ENTRY,                   \
        stringify_args(__handle.get(), "|", get_func_sig(FUNCTION_SIGNATURE)) +\
        "(" + concat_args(__VA_ARGS__) + ")|\n");                              \
  }                                                                            \
  while (0)                                                                    \

/******************************************************************************
 *  macros to capture exit trace of a function which has no return. Additional
 *  variables can be traced.
 ******************************************************************************/
#define XRT_TOOLS_XBT_FUNC_EXIT(...)                                           \
  do                                                                           \
  {                                                                            \
    if ((nullptr == this) || (nullptr == this->get_handle().get()))            \
    {                                                                          \
      XRT_TOOLS_XBT_LOG_ERROR("Handle");                                       \
      break;                                                                   \
    }                                                                          \
    auto __handle = this->get_handle();                                        \
    logger::get_instance().log(trace_type::TRACE_TYPE_EXIT,                    \
        stringify_args(__handle.get(), "|", get_func_sig(FUNCTION_SIGNATURE)) +\
        "|" + concat_args_nv(__VA_ARGS__) + "|\n");                            \
  }                                                                            \
  while (0)

/******************************************************************************
 *  macros to capture exit trace of a function which has return. Additional
 *  variables can be traced.
 ******************************************************************************/
#define XRT_TOOLS_XBT_FUNC_EXIT_RET(r, ...)                                    \
  do                                                                           \
  {                                                                            \
    if ((nullptr == this) || (nullptr == this->get_handle()))                  \
    {                                                                          \
      XRT_TOOLS_XBT_LOG_ERROR("Handle");                                       \
      break;                                                                   \
    }                                                                          \
    auto __handle = this->get_handle();                                        \
    logger::get_instance().log(trace_type::TRACE_TYPE_EXIT,                    \
        stringify_args(__handle.get(), "|", get_func_sig(FUNCTION_SIGNATURE)) +\
        "=" + stringify_args(r) + "|" + concat_args_nv(__VA_ARGS__) + "|\n");  \
  }                                                                            \
  while (0)

}  // namespace xrt::tools::xbtracer
