// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include "getopt.h"
#include <shlwapi.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// Global mutexes to ensure thread safety when accessing shared resources
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex env_mutex;
std::mutex getopt_mutex;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr unsigned int str_sz_s = 32;
constexpr unsigned int str_sz_m = 128;
constexpr unsigned int str_sz_l = 256;
constexpr unsigned int str_sz_xl = 512;
constexpr unsigned int w32 = 32;
constexpr unsigned int w64 = 64;
constexpr unsigned int max_cmd_args = 8;
constexpr unsigned int fw_9 = 9;

class launcher
{
 public:
  // Public accessor for the singleton instance
  static launcher& getInstance()
  {
    static launcher instance;
    return instance;
  }

  // Public members
  std::array<char, str_sz_s> m_name = {0};
  bool m_debug = false;

  // Delete copy constructor and assignment operator
  launcher(const launcher&) = delete;
  launcher& operator=(const launcher&) = delete;
  launcher(launcher&& other) noexcept = delete;
  launcher& operator=(launcher&& other) noexcept = delete;

 private:
  launcher() = default;
  ~launcher() = default;
};

/*
 * This function template appends a given value of any type to the specified
 * output string stream using the stream insertion operator.
 */
template <typename T>
void log_format(std::ostringstream& oss, const T& t)
{
  /* Warning: implicitly decay an array into a pointer
  * The function is explicitly designed to handle any type, including arrays
  * Behavior of array-to-pointer decay as safe in this context
  */
  oss << t; /* NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  hicpp-no-array-decay) */
}

/*
 * This function template appends a given value of any type to the specified
 * output string stream and recursively processes additional values if provided.
 * It terminates when there are no more values to process.
 */
template <typename T, typename... Args>
void log_format(std::ostringstream& oss, const T& t, const Args&... args)
{
  /* Warning: implicitly decay an array into a pointer
  * The function is explicitly designed to handle any type, including arrays
  * Behavior of array-to-pointer decay as safe in this context
  */
  oss << t; /* NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  hicpp-no-array-decay) */
  log_format(oss, args...);
}

/*
 * Function to trace the error log
 */
template <typename... Args>
void log_e(const Args&... args)
{
  std::ostringstream oss;
  oss << "[" << launcher::getInstance().m_name.data() << "] E: ";
  log_format(oss, args...);
  std::cout << oss.str() << std::endl;
}

/*
 * Function to trace the fatal log
 */
template <typename... Args>
void log_f(const Args&... args)
{
  std::ostringstream oss;
  oss << "[" << launcher::getInstance().m_name.data() << "] F: ";
  log_format(oss, args...);
  throw std::runtime_error(oss.str() + ". Aborted!\n");
}

/*
 * Function to trace the debug log
 */
template <typename... Args>
void log_d(const Args&... args)
{
  if (launcher::getInstance().m_debug) {
    std::ostringstream oss;
    oss << "[" << launcher::getInstance().m_name.data() << "] D: ";
    log_format(oss, args...);
    std::cout << oss.str() << std::endl;
  }
}

/*
 * thread safe wrapper of putenv()
 */
void putenv_t(std::string& new_entry)
{
  std::lock_guard<std::mutex> lock(env_mutex);
  #ifdef _WIN32
  _putenv(new_entry.data());
  #else
  // NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
  putenv(new_entry.data());
  #endif
}

#ifdef _WIN32
/*
 * Check the architectural compatability(32bit vs 64bit) between the app and
 * lib.
 */
static bool check_compatibility(HANDLE parent, HANDLE child)
{
  static launcher& app = launcher::getInstance();
  BOOL is_parent_wow64 = FALSE, is_child_wow64 = FALSE;

  IsWow64Process(parent, &is_parent_wow64);
  IsWow64Process(child, &is_child_wow64);

  if (is_parent_wow64 != is_child_wow64) {
    log_e(app.m_name.data(), "is", (is_parent_wow64 ? w64 : w32),
          "-bit but target application is ", (is_child_wow64 ? w64 : w32),
          "-bit");
    return false;
  }

  return true;
}

// Function to set an environment variable
bool SetEnvVariable(const std::string& name, const std::string& value)
{
  LPCSTR lpName = name.c_str();
  LPCSTR lpValue = value.c_str();

  BOOL result = SetEnvironmentVariableA(lpName, lpValue);

  if (result == 0) {
    DWORD errorCode = GetLastError();
    std::cerr << "Failed to set environment variable. Error code: " << errorCode
              << std::endl;
    return false;
  }

  return true;
}

/*
 * Function to get the current precise system time as pair for string and
 * formated string
 */
std::pair<std::string, std::string> GetCurrentTimeAsString()
{
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);

  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;

  // Convert the time to a string
  std::ostringstream oss;
  oss << uli.QuadPart;

  // Time formatting and trace directory printing
  FILETIME localFt;
  FileTimeToLocalFileTime(&ft, &localFt);

  SYSTEMTIME st;
  FileTimeToSystemTime(&localFt, &st);

  std::ostringstream ss;
  ss << std::setfill('0') << std::setw(4) << st.wYear << "-" << std::setw(2)
     << st.wMonth << "-" << std::setw(2) << st.wDay << "_" << std::setw(2)
     << st.wHour << "-" << std::setw(2) << st.wMinute << "-" << std::setw(2)
     << st.wSecond;

  std::string formattedTime = ss.str();

  return {oss.str(), formattedTime};
}
#else
/* Warning: variable 'environ' is non-const and globally accessible
 * It ensures compatibility with existing systems and libraries that rely on
 * environ. Its usage is typically limited and encapsulated, reducing the risk
 * of misuse.
 */
// NOLINTNEXTLINE (cppcoreguidelines-avoid-non-const-global-variables)
extern char **environ;
#endif

/*
 * Main Function
 */
int main(int argc, char* argv[])
{
  int option = 0;

  launcher& app = launcher::getInstance();
  bool inst_debug = false;
  std::vector<std::string> args(argv, argv + argc);
#ifdef _WIN32
  DWORD retval = 0;

  strncpy_s(app.m_name.data(), app.m_name.size(), PathFindFileName(argv[0]), _TRUNCATE);
#else
  int retval = 0;
  std::string name = "";
  std::filesystem::path path(args[0]);
  if (!path.filename().empty()) {
    name = path.filename().string();
  }

  if (!name.empty()) {
    /* Warning: do not implicitly decay an array into a pointer
     * The array-to-pointer decay is necessary and controlled for interfacing
     * with C-style functions.
     * Size control ensures that the strncpy operation is safe and prevents
     * buffer overflows.
     */
    // NOLINTNEXTLINE (cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    strncpy(app.m_name.data(), name.c_str(), str_sz_s);
  }
#endif
  try {
    std::lock_guard<std::mutex> lock(getopt_mutex);
    // Parse command line options
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - getopt is protected by a mutex
    while ((option = getopt(argc, argv, "vV:")) != -1) {
      switch (option) {
        case 'v': {
          app.m_debug = true;
          break;
        }
        case 'V': {
          app.m_debug = true;
          inst_debug = true;
          break;
        }
        default: {
          break;
        }
      }
    }

    if (optind == argc) {
      log_f("There should be alleast 1 argument without option switch");
    }

    std::string cmdline = "";

    if (optind <= static_cast<int>(args.size() - 1)) {
      cmdline = args[optind++];
    } else {
      log_f("Invalid optindex: ", optind, "args size: ", args.size());
    }

    for (size_t idx = optind; idx < args.size(); idx++) {
      cmdline += " ";
      cmdline += args[idx];
    }
    log_d("Application to intercept = \"", cmdline, "\"");

#ifdef _WIN32
    if (inst_debug) {
      if (!SetEnvironmentVariable(TEXT("INST_DEBUG"), "TRUE")) {
        log_e("Failed to set INST_DEBUG Env");
      }
    }

    if (!SetEnvironmentVariable(TEXT("TRACE_APP_NAME"), cmdline.c_str())) {
      log_e("Failed to set the TRACE_APP_NAME Env");
    }

    constexpr std::string envName = "START_TIME";
    auto times = GetCurrentTimeAsString();
    std::string envValue = times.first;
    std::string formattedTime = times.second;
    if (SetEnvVariable(envName, envValue)) {
      log_d("Environment variable set successfully: ", envName, " = ",
          envValue);
    } else {
      log_f("Failed to set environment variable: ", envName);
    }

    // Create child process in suspended state:
    log_d("Creating child process with command line: ", cmdline.c_str());
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    // Initialize si and pi
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(
            NULL,                    // No module name (use command line)
            (LPSTR)cmdline.c_str(),  // Command line
            NULL,                    // Process handle not inheritable
            NULL,                    // Thread handle not inheritable
            FALSE,                   // Set handle inheritance to FALSE
            CREATE_SUSPENDED,        // Process created in a suspended state
            NULL,                    // Use parent's environment block
            NULL,                    // Use parent's starting directory
            &si,                     // Pointer to STARTUPINFO structure
            &pi) == FALSE)           // Pointer to PROCESS_INFORMATION structure
    {
      log_f("Child process creation failed");
    }
    log_d("Child process created. ");

    // Check that we don't have a compatability issue between child and parrent
    if (check_compatibility(GetCurrentProcess(), pi.hProcess) == false) {
      log_f("Compatability check failed. Exiting ...");
    }

    // Resume child process:
    log_d("Resuming child process");
    if (ResumeThread(pi.hThread) < 0) {
      log_f("Failed to resume thread");
    }

    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path traceDir = currentDir / formattedTime;
    std::cout << "\nTraces can be found at: " << traceDir << std::endl;

    log_d("Child process resumed, Waiting for child process to finish");

    // Wait for child process to finish
    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
      log_f("Waiting for child process failed");
    }

    // Get return code and forward it
    if (GetExitCodeProcess(pi.hProcess, &retval) == FALSE) {
      log_f("Failed to read child process exit code");
    }

    std::ostringstream ret_fmt;

    // Format the number as a hexadecimal string with leading zeros and width 8
    ret_fmt << std::setfill('0') << std::setw(8) << std::hex << retval;

    log_d("Child process completed with exit code ", retval, " (",
          ret_fmt.str(), ")");

#else
    std::array<char, str_sz_l> cwc = {0};
    std::array<char, str_sz_l> command = {0};
    std::array<char*, max_cmd_args> command_args = {nullptr};
    unsigned long num_cmd_args = 0;
    uint32_t i = 0, j = 0;

    if (optind > 0 && optind <= static_cast<int>(args.size())) {
      int index = optind - 1;
      cmdline = args[index];
      strncpy(command.data(), cmdline.c_str(), command.size());
      command[str_sz_l - 1] = '\0';
    } else {
      log_f("Invalid option index range");
    }

    log_d("Command: ", command.data());

    unsigned long len = strlen(command.data());
    if (optind == argc) {
      /* Copy command argument pointers in command_args */
      j = 1;
      auto it = command.begin();
      auto end = command.end();
      while (it != end && j < max_cmd_args) {
        if (*it == ' ') {
          *it = '\0';
          if (std::next(it) != end) {
            /* Warning: do not use array subscript when the index is not an
             * integer constant expression
             * Indexing and iteration logic are synchronized and managed to
             * prevent boundary violations
             */
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            command_args[j++] = &(*std::next(it));
          }
        }
        ++it;
      }

      if (i < len && j >= max_cmd_args) {
        log_f("Number of app cmd argument is larger then supported(",
              max_cmd_args, ")");
      }
      command_args[0] = command.data();
    } else {
      for(i = optind, j = 0; i < args.size() && j < max_cmd_args; i++) {
        /* Warning: do not use array subscript when the index is not an
         * integer constant expression
         * The indices i and j are controlled and validated to be within their
         * respective bounds.
         * The args[i].c_str() operation is safe within the bounds of args,
         * and strdup ensures safe string duplication.
         */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        command_args[j++] = strdup(args[i].c_str());
      }
    }

    num_cmd_args = args.size();

    std::string app_name_env = "TRACE_APP_NAME=";
    for (i = 0; i < num_cmd_args; i++) {
      std::ostringstream oss;
      /* Warning: do not use array subscript when the index is not an
       * integer constant expression
       * The loop's bounds are controlled by num_cmd_args, ensuring i remains
       * within valid limits.
       * num_cmd_args is validated to be within the bounds of command_args.
       */
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
      oss << app_name_env << command_args[i];
      if (i != (num_cmd_args - 1)) {
        oss << " ";
      }
      app_name_env = oss.str();
    }

    log_d("Adding to ENV : ", app_name_env.data());

    putenv_t(app_name_env);

    // Capture the current time
    struct timespec start_time = {0, 0};  // Initialize tv_sec and tv_nsec to 0
    clock_gettime(CLOCK_REALTIME, &start_time);

    // Convert the timespec to a string representation
    std::array<char, w64> time_str = {0};
    // snprintf(time_str.data(), sizeof(time_str), "%ld.%09ld",
    // start_time.tv_sec, start_time.tv_nsec);
    std::ostringstream oss;
    oss << start_time.tv_sec << '.' << std::setw(fw_9) << std::setfill('0')
        << start_time.tv_nsec;
    std::strncpy(time_str.data(), oss.str().c_str(), time_str.size() - 1);
    time_str[time_str.size() - 1] = '\0';  // Null-terminate to be safe

    // Store the string in an environment variable using putenv
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
    if (setenv("START_TIME", time_str.data(), 1) != 0) {
      log_f("Failed to set START_TIME environment variable", "");
    }

    log_d("Time stored in environment variable: ", time_str.data());

    // Time formatting and trace directory printing
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
    std::tm tm_time = *std::localtime(
        &start_time.tv_sec);  // Initialize tm_time with current local time;
    localtime_r(&start_time.tv_sec,
                &tm_time);  // Convert time_t to tm in a threadsafe way

    std::stringstream ss;
    ss << std::put_time(&tm_time, "%Y-%m-%d_%H-%M-%S");
    std::string formattedTime = ss.str();
    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path traceDir = currentDir / formattedTime;
    std::cout << "\nTraces can be found at: " << traceDir << std::endl;

    if (inst_debug) {
      std::string inst_dbg_env = "INST_DEBUG=TRUE";
      log_d("Adding to ENV : ", inst_dbg_env);
      putenv_t(inst_dbg_env);
    }

    if (app.m_debug) {
      std::ostringstream oss;
      oss << "App command :: ";
      for (i = 0; i < num_cmd_args; i++) {
        oss << args[i] << " ";
      }
      log_d(oss.str());
    }

    std::vector<char> new_entry(str_sz_xl);

    if (!getcwd(cwc.data(), cwc.size())) {
      perror("getcwc");
    }

    execve(command.data(), command_args.data(), (char* const*)environ);

    perror("execve");
#endif
  } catch (const std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
  }
  return retval;
}
