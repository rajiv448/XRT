// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include "logger.h"
#include "version.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <array>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
  #include <unistd.h>
#endif /* #ifdef _WIN32 */

namespace xrt::tools::xbtracer {

constexpr unsigned int giga = 1e9;
constexpr unsigned int hundred = 100;
constexpr unsigned int str_sz_s = 32;
constexpr unsigned int str_sz_m = 128;
constexpr unsigned int str_sz_l = 256;
constexpr unsigned int fw_9 = 9;
#ifdef _WIN32
constexpr std::string_view path_separator = "\\";
#else
constexpr std::string_view path_separator = "/";
#endif /* #ifdef _WIN32 */

//NOLINTNEXTLINE - env_mutex cann't be const
std::mutex env_mutex;

/*
 * Method to calculate the time-diffrence since start of the trace.
 * */
std::string logger::timediff(struct timespec now, struct timespec then)
{
  uint64_t nsec = 0, sec = 0;
  std::ostringstream oss;

  if (now.tv_nsec < then.tv_nsec)
  {
    sec = now.tv_sec - then.tv_sec - 1;
    nsec = giga + now.tv_nsec - then.tv_nsec;
  }
  else
  {
    sec = now.tv_sec - then.tv_sec;
    nsec = now.tv_nsec - then.tv_nsec;
  }

  oss << sec << "." << std::setfill('0') << std::setw(fw_9) << nsec;

  return oss.str();
}

std::string logger::os_name_ver()
{
  std::string pretty_name;
#ifdef _WIN32
  DWORD edition_id = 0;
  OSVERSIONINFOEX osvi;
  ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
  osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

  if (GetVersionEx((LPOSVERSIONINFO)&osvi))
  {
    if (!GetProductInfo(GetVersion(), 0, 0, 0, &edition_id))
      std::cerr << "Failed to retrieve Windows edition" << std::endl;

    if(inst_debug)
      std::cout << "Major: " <<  osvi.dwMajorVersion << ", Minor: "
                << osvi.dwMinorVersion << ", PT: " << osvi.wProductType
                << ", CSDV: \"" << osvi.szCSDVersion << "\"" << ", Edition: 0x"
                << std::hex << edition_id << "\n";

    if (osvi.dwMajorVersion == 10 && osvi.wProductType == VER_NT_WORKSTATION
        && osvi.szCSDVersion[0] == 0)
      pretty_name = "Windows 11";
    else
    {
      if ( !osvi.szCSDVersion[0] )
        pretty_name = stringify_args("\"Windows ", osvi.dwMajorVersion, ".",
                        osvi.dwMinorVersion, "\"");
      else
        pretty_name = stringify_args("\"Windows ", osvi.dwMajorVersion, ".",
                        osvi.dwMinorVersion, " ", osvi.szCSDVersion, "\"");
    }
  }
  else
    pretty_name = "\"Windows(unknown)\"";
#else
  std::ifstream os_release("/etc/os-release");
  std::string line;

  if (os_release.is_open())
  {
    while (std::getline(os_release, line))
    {
      if (line.find("PRETTY_NAME=") != std::string::npos)
      {
        pretty_name = line.substr(line.find('=') + 1);
        break;
      }
    }
    os_release.close();
  }
  else
  {
    std::cerr << "Failed to open /etc/os-release" << std::endl;
    pretty_name = "Linux-unknown-dist";
  }
#endif /* #ifdef _WIN32 */
  return pretty_name;
}

#ifdef _WIN32
void read_time_now(struct timespec & time)
{
  FILETIME ft;
  ULARGE_INTEGER ul;

  GetSystemTimePreciseAsFileTime(&ft);
  ul.LowPart = ft.dwLowDateTime;
  ul.HighPart = ft.dwHighDateTime;

  ULONGLONG nanoseconds = ul.QuadPart * hundred;
  time.tv_nsec = nanoseconds % giga;
  time.tv_sec = nanoseconds / giga;
}

SYSTEMTIME ul_to_systemtime(ULARGE_INTEGER& ul)
{
  FILETIME ft;
  ft.dwLowDateTime = ul.LowPart;
  ft.dwHighDateTime = ul.HighPart;

  FILETIME local_ft;
  FileTimeToLocalFileTime(&ft, &local_ft);

  SYSTEMTIME st;
  FileTimeToSystemTime(&local_ft, &st);

  return st;
}

ULARGE_INTEGER timespec_to_ul(struct timespec& time)
{
  ULARGE_INTEGER ul;
  ULONGLONG nanoseconds = (giga * time.tv_sec) + time.tv_nsec;
  ul.QuadPart = nanoseconds / hundred;
  return ul;
}

struct timespec ul_to_timespec(ULARGE_INTEGER& ul)
{
  struct timespec time{};
  ULONGLONG nanoseconds = ul.QuadPart * hundred;
  time.tv_nsec = nanoseconds % giga;
  time.tv_sec = nanoseconds / giga;
  return time;
}

std::string systemtime_to_str(SYSTEMTIME &st)
{
  std::stringstream ss;
  ss << std::setfill('0')
     << std::setw(4) << st.wYear << "-"
     << std::setw(2) << st.wMonth << "-"
     << std::setw(2) << st.wDay << "_"
     << std::setw(2) << st.wHour << "-"
     << std::setw(2) << st.wMinute << "-"
     << std::setw(2) << st.wSecond;
  return ss.str();
}

ULARGE_INTEGER str_to_ul(char *str)
{
  ULARGE_INTEGER ul;
  ul.QuadPart = std::stoull(std::string(str));
  return ul;
}
#else
std::string timespec_to_str(struct timespec& time)
{
  auto bt = localtime_xp(time.tv_sec);
  std::array<char, str_sz_m> buffer = {0};
  (void)std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d_%H-%M-%S", &bt);
  return std::string(buffer.data());
}
#endif /* #ifdef _WIN32 */

/*
 * constructor
 * */
logger::logger()
{
  std::string time_fmt_str="";
  std::ostringstream oss_full_path;

#ifdef _WIN32
  pid = GetCurrentProcessId();

  TCHAR buffer[str_sz_l];
  DWORD result =
          GetEnvironmentVariable(TEXT("INST_DEBUG"), buffer, str_sz_l);
  if (result > 0 && result < str_sz_l && !strcmp(buffer, "TRUE"))
    inst_debug=true;

  result = GetEnvironmentVariable(TEXT("TRACE_APP_NAME"), buffer, str_sz_l);
  if (result > 0 && result < str_sz_l)
    program_name = std::string(buffer);

  result = GetEnvironmentVariable(TEXT("START_TIME"), buffer, str_sz_l);
  if (result > 0 && result < str_sz_l)
  {
    ULARGE_INTEGER ul = str_to_ul(buffer);
    start_time = ul_to_timespec(ul);
    time_fmt_str = systemtime_to_str(ul_to_systemtime(ul));
  }

#else
  std::lock_guard<std::mutex> lock(env_mutex);

  //NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
  char* app_dbg_env = getenv("INST_DEBUG");
  if (app_dbg_env && !strcmp(app_dbg_env, "TRUE"))
    inst_debug = true;

  //NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
  program_name=getenv("TRACE_APP_NAME");

  pid = getpid();

  // Retrieve the time from the environment variable
  //NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
  const char* time_str = getenv("START_TIME");
  if (time_str == nullptr)
    std::cerr << "Environment variable START_TIME not set!" << std::endl;
  else
    std::cout << "Time retrieved from environment variable: " << time_str
              << std::endl;

  // Convert the string back to a timespec structure
  std::istringstream iss(time_str);
  iss >> start_time.tv_sec;
  iss.ignore(1, '.');  // Ignore the dot character
  iss >> start_time.tv_nsec;
  time_fmt_str = timespec_to_str(start_time);
#endif /* #ifdef _WIN32 */

  // Create directory using the timestamp.
  namespace fs = std::filesystem;
  if (!fs::exists(time_fmt_str))
    if (!fs::create_directory(time_fmt_str))
      std::cerr << "Failed to create directory: " << time_fmt_str << std::endl;

  // Construct full path and open files for logging.
  oss_full_path << "." <<path_separator << time_fmt_str << path_separator
                << xrt_trace_filename;

  fp.open(oss_full_path.str(), std::ios::out);

  oss_full_path.str("");
  oss_full_path.clear();

  oss_full_path << "." << path_separator << time_fmt_str << path_separator
                << xrt_trace_bin_filename;

  fp_bin.open(oss_full_path.str(), std::ios::out | std::ios::binary);

  fp << "|HEADER|pname:\"" << program_name <<  "\"|pid:" << pid << "|xrt_ver:"
     << XRT_DRIVER_VERSION << "|os:" << os_name_ver() << "|time:"
     << time_fmt_str << "." << std::setfill('0') << std::setw(fw_9)
     << start_time.tv_nsec << "|\n";

  fp << "|START|"<< time_fmt_str << "." << std::setfill('0') << std::setw(fw_9)\
     << start_time.tv_nsec << "|\n";
}

/*
 * destructor
 * */
logger::~logger()
{
  struct timespec time_now{};

#ifdef _WIN32
  read_time_now(time_now);
  ULARGE_INTEGER ul = timespec_to_ul(time_now);
  std::string time_fmt_str = systemtime_to_str(ul_to_systemtime(ul));
#else
  clock_gettime(CLOCK_REALTIME, &time_now);
  std::string time_fmt_str = timespec_to_str(time_now);
#endif

  fp << "|END|" << time_fmt_str << "." << std::setfill('0') << std::setw(fw_9)
     << time_now.tv_nsec << "|\n";

  fp_bin.close();
  fp.close();
}

/*
 * API to capture Entry and Exit Trace.
 * */
void logger::log(trace_type type, std::string str)
{
  struct timespec time_now{};
  std::stringstream ss;

#ifdef _WIN32
  DWORD tid = GetCurrentThreadId();
  read_time_now(time_now);
#else
  pthread_t tid = pthread_self();
  clock_gettime(CLOCK_REALTIME, &time_now);
#endif /* #ifdef _WIN32 */

  ss << ((type == trace_type::TRACE_TYPE_ENTRY) ? "|ENTRY|" : "|EXIT|")
     << timediff(time_now, start_time) << "|" << pid << "|" << tid << "|"
     << str;

  fp << ss.str();
};

// Function to perform find and replace operations
std::string find_and_replace_all(std::string str,
    const std::vector<std::pair<std::string, std::string>>& replacements)
{
  for (const auto& pair : replacements)
  {
    std::string::size_type pos = 0;
    while ((pos = str.find(pair.first, pos)) != std::string::npos)
    {
      str.replace(pos, pair.first.length(), pair.second);
      pos += pair.second.length();
    }
  }
  return str;
}

} // namespace xrt::tools::xbtracer
