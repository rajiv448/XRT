// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include "capture.h"
#include "logger.h"

#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace xrt::tools::xbtracer {

xrt_ftbl& xrt_ftbl::get_instance()
{
  static xrt_ftbl instance;
  return instance;
}

/* Warning: initialization of 'dtbl' with static storage duration may throw
 * an exception that cannot be caught
 * The singleton pattern using a static method-local variable ensures safe,
 * lazy, and thread-safe initialization.
 * The static variable inside get_instance avoids issues related to static
 * initialization order by deferring initialization until first use.
 */
// NOLINTNEXTLINE(cert-err58-cpp)
const static xrt_ftbl& dtbl = xrt_ftbl::get_instance();

std::unordered_map <void*, std::string> fptr2fname_map;

/* This will create association between function name
 * and function pointer of the original library file
 * which will be used to invoke API's from original library.
 */
/* Static initialization might depend on the order of initialization of other
 * static objects. However, in this case, the initialization of fname2fptr_map to
 * an empty map is straightforward and does not depend on any other static objects.
 */
// NOLINTNEXTLINE(cert-err58-cpp)
const static std::unordered_map < std::string, void **> fname2fptr_map = {
  /* Warning: do not use C-style cast to convert between unrelated types
   * Suppressing the warning is justified because it can simplify code in
   * performance-critical sections where types are well understood, and
   * maintains compatibility with legacy code and third-party libraries.
   */
  // NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast)
  /* device class maps */
  {"xrt::device::device(unsigned int)", (void **) &dtbl.device.ctor},
  {"xrt::device::load_xclbin(std::string const&)", (void **) &dtbl.device.load_xclbin_fnm},
  // NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast)
};

} //namespace xrt::tools::xbtracer

#ifdef __linux__
#include <cxxabi.h>
#include <dlfcn.h>
#include <elf.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

namespace xrt::tools::xbtracer {

constexpr const char* lib_name = "libxrt_coreutil.so";

// mutex to serialize dlerror and getenv
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex dlerror_mutex;
extern std::mutex env_mutex;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * This class will perform following operations.
 * 1. Read mangled symbols from .so file.
 * 2. perform demangling operation.
 * 3. update xrt_dtable which will be used to invoke original API's
 */
class router
{
  private:
  void* handle = nullptr;
  /* library Path */
  std::string path;
  /*
   * This will create association between function name (key)
   * and mangled function name (value).
   */
  std::unordered_map<std::string, std::string> func_mangled;

  public:
  router(const router&);
  auto operator = (const router&) -> router&;
  router(router && other) noexcept;
  auto operator = (router&& other) noexcept -> router&;
  auto load_func_addr() -> int;
  auto load_symbols() -> int;

  static std::shared_ptr<router> get_instance()
  {
    static auto ptr = std::make_shared<router>();
    return ptr;
  }

  router()
  : path("")
  {
    if (!load_symbols())
    {
      std::cout << "Failed to load symbols exiting application " << std::endl;
      throw std::runtime_error("Failed to load symbols");
    }
    else if (!load_func_addr())
    {
      std::cout << "Failed to load function address exiting application "
                << std::endl;
      throw std::runtime_error("Failed to load function address");
    }
  }

  ~router()
  {
    if (handle)
      dlclose(handle);
  }
};

/* Warning: initialization of 'dptr' with static storage duration may throw
 * an exception that cannot be caught
 * The singleton pattern implemented with a static local variable in
 * get_instance ensures safe, one-time initialization.
 */
// NOLINTNEXTLINE(cert-err58-cpp)
const auto dptr = router::get_instance();

/**
 * This function demangles the input mangled function.
 */
static std::string demangle(const char* mangled_name)
{
  int status = 0;
  char* demangled_name =
      abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status);
  if (status == 0)
  {
    // Use std::unique_ptr with a custom deleter to manage demangled_name
    std::unique_ptr<char, decltype(&std::free)> demangled_ptr(demangled_name,
                                                              std::free);

    /* Conditioning of demangled name because of some platform based deviation
      */
    std::vector<std::pair<std::string, std::string>> replacements =
      {
        {"std::__cxx11::basic_string<char, std::char_traits<char>, "
              "std::allocator<char> >", "std::string"},
        {"[abi:cxx11]", ""},
        {"std::map<std::string, unsigned int, std::less<std::string >, "
            "std::allocator<std::pair<std::string const, unsigned int> > > "
            "const&", "xrt::hw_context::cfg_param_type const&"}
      };

    std::string result =
        find_and_replace_all(std::string(demangled_name), replacements);

    return result;
  }
  else
    // Demangling failed
    return {mangled_name};  // Return the original mangled name
}

static std::string find_library_path()
{
  std::lock_guard<std::mutex> lock(env_mutex);
  // NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by env_mutex
  char* ld_preload = getenv("LD_PRELOAD");
  if (ld_preload == nullptr)
  {
    std::cout << "LD_PRELOAD is not set." << std::endl;
    return "";
  }

  std::string full_path = std::string(ld_preload);
  full_path.erase(std::remove(full_path.begin(), full_path.end(), ' '),
                  full_path.end());
  return full_path;
}

/**
 * This function will update the dispatch table
 * with address of the functions from original
 * library.
 */
int router::load_func_addr()
{
  // Load the shared object file
  handle = dlopen(lib_name, RTLD_LAZY);
  if (!handle)
  {
    std::lock_guard<std::mutex> lock(dlerror_mutex);
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - protected by dlerror_mutex
    std::cerr << "Error loading shared library: " << dlerror() << std::endl;
    return 0;
  }

  /**
   * Get Function address's which are of interest and ignore others.
   */
  for (auto& it : func_mangled)
  {
    auto ptr_itr = fname2fptr_map.find(it.first);

    if (ptr_itr != fname2fptr_map.end())
    {
      void** temp = ptr_itr->second;
      /* update the original function address in the dispatch table */
      *temp = (dlsym(handle, it.second.c_str()));
      if (nullptr == temp)
        std::cout << "Null Func address received " << std::endl;
      else
        fptr2fname_map[*temp] = it.first;
    }
    else
    {
      // std::cout << "func :: \"" << it.first << "\" not found in"
      //          << "fname2fptr_map\n";
    }
  }
  return 1;
}

/**
 * This function will read mangled API's from library and perform
 * Demangling operation.
 */
int router::load_symbols()
{
  constexpr unsigned int symbol_len = 1024;

  path = find_library_path();

  // Open the ELF file
  std::ifstream elf_file(path, std::ios::binary);
  if (!elf_file.is_open())
  {
    std::cerr << "Failed to open ELF file: " << path << std::endl;
    return 0;
  }

  // Read the ELF header
  Elf64_Ehdr elf_header;
  std::array<char, sizeof(Elf64_Ehdr)> buffer{0};
  elf_file.read(buffer.data(), buffer.size());
  memcpy(&elf_header, buffer.data(), buffer.size());

  // Check ELF magic number
  if (memcmp(static_cast<const void*>(elf_header.e_ident),
              static_cast<const void*>(ELFMAG), SELFMAG) != 0)
  {
    std::cerr << "Not an ELF file" << std::endl;
    return 0;
  }

  // Get the section header table
  elf_file.seekg((std::streamoff)elf_header.e_shoff);
  std::vector<Elf64_Shdr> section_headers(elf_header.e_shnum);
  /* The reinterpret cast is necessary for performing low-level binary file I/O
   * operations involving a specific memory layout.
   * The memory layout and size of section_headers match the binary data being
   * read, ensuring safety and correctness.
   */
  // NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
  elf_file.read(reinterpret_cast<char*>(section_headers.data()),
                (std::streamsize)(elf_header.e_shnum * sizeof(Elf64_Shdr)));
  if (!elf_file)
  {
    std::cerr << "Failed to read section header table" << std::endl;
    return 0;
  }

  // Find the symbol table section
  Elf64_Shdr* symtab_section = nullptr;
  for (int i = 0; i < elf_header.e_shnum; ++i)
  {
    if (section_headers[i].sh_type == SHT_DYNSYM)
    {
      symtab_section = &section_headers[i];
      break;
    }
  }

  if (symtab_section == nullptr)
  {
    std::cerr << "Symbol table section not found" << std::endl;
    return 0;
  }

  // Read and print the mangled function names from the symbol table section
  unsigned long num_symbols = symtab_section->sh_size / sizeof(Elf64_Sym);
  for (unsigned long i = 0; i < num_symbols; ++i)
  {
    Elf64_Sym symbol;
    elf_file.seekg(
        (std::streamoff)(symtab_section->sh_offset + i * sizeof(Elf64_Sym)));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    elf_file.read(reinterpret_cast<char*>(&symbol), sizeof(Elf64_Sym));
    if (!elf_file)
    {
      std::cerr << "Failed to read symbol table entry" << std::endl;
      return 0;
    }

    // Check if the symbol is a function
    if ((ELF64_ST_TYPE(symbol.st_info) == STT_FUNC) &&
        (ELF64_ST_BIND(symbol.st_info) == STB_GLOBAL) &&
        (ELF64_ST_VISIBILITY(symbol.st_other) == STV_DEFAULT) &&
        (symbol.st_shndx != SHN_UNDEF))
    {
      std::array<char, symbol_len> symbol_name{0};
      elf_file.seekg(
          (std::streamoff)section_headers[symtab_section->sh_link].sh_offset +
          symbol.st_name);
      elf_file.read(symbol_name.data(), symbol_len);
      if (!elf_file)
      {
        std::cerr << "Failed to read symbol name" << std::endl;
        return 0;
      }
      // std::cout <<"Mangled name: "<<symbol_name.data() <<std::endl;
      std::string demangled_name = demangle(symbol_name.data());
      // std::cout <<"De-Mangled name: " << demangled_name << "\n";
      func_mangled[demangled_name] = symbol_name.data();
    }
  }

  return 1;
}
}  // namespace xrt::tools::xbtracer

#elif _WIN32
#include <Dbghelp.h>
#include <windows.h>
#pragma comment(lib, "Dbghelp.lib")

namespace xrt::tools::xbtracer {
  std::string demangle(const char* mangled)
  {
    constexpr const DWORD length = 512;  // Adjust the buffer size as needed
    char demangled_str[length];

    DWORD result = UnDecorateSymbolName(
        mangled, demangled_str, length,
        UNDNAME_NO_FUNCTION_RETURNS | UNDNAME_NO_ACCESS_SPECIFIERS |
            UNDNAME_NO_ALLOCATION_LANGUAGE | UNDNAME_NO_ALLOCATION_MODEL |
            UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_THROW_SIGNATURES);

    if (result != 0)
    {
      std::vector<std::pair<std::string, std::string>> replacements =
      {
        { "class std::basic_string<char,struct std::char_traits<char>,"
            "class std::allocator<char> >", "std::string" },
        {"const ", "const"},
        {"class ", ""},
        {",", ", "},
        {")const", ") const"},
        {"__int64", "long"},
        {"(void)", "()"},
        {"enum ", ""},
        {"struct std::ratio<1, 1000>", "std::ratio<1l, 1000l>"}
      };

      std::string demangled_and_conditioned_str =
          find_and_replace_all(demangled_str, replacements);
      return demangled_and_conditioned_str;
    } else {
      return mangled;
    }
  }

  // Make the page writable and replace the function pointer. Once
  // replacement is completed restore the page protection.
  static void replace_func(PIMAGE_THUNK_DATA thunk, void* func_ptr)
  {
    // Make page writable temporarily:
    MEMORY_BASIC_INFORMATION mbinfo;
    VirtualQuery(thunk, &mbinfo, sizeof(mbinfo));
    if (!VirtualProtect(mbinfo.BaseAddress, mbinfo.RegionSize,
                        PAGE_EXECUTE_READWRITE, &mbinfo.Protect))
      return;

    // Replace function pointer with our implementation:
    thunk->u1.Function = (ULONG64)func_ptr;

    // Restore page protection:
    DWORD zero = 0;
    if (!VirtualProtect(mbinfo.BaseAddress, mbinfo.RegionSize,
                        mbinfo.Protect, &zero))
      return;
  }
}  // namespace xrt::tools::xbtracer

using namespace xrt::tools::xbtracer;

// Iterate through the IDT for all table entry corresponding to xrt_coreutil.dll
// and replace the function pointer in first_thunk by looking for the same name
// into the xrt_capture.dll for the same name.
int idt_fixup(void* dummy)
{
  static bool inst_debug = false;
  std::string filename("");
  TCHAR buffer[128];
  DWORD result = GetEnvironmentVariable(TEXT("INST_DEBUG"), buffer, 128);
  if (result > 0 && result < 128 && !strcmp(buffer, "TRUE"))
    inst_debug = true;

  LPVOID image_base;
  if (dummy != NULL)
  {
    std::filesystem::path path((const char*)dummy);
    filename = path.filename().string();
    image_base = GetModuleHandleA(filename.c_str());
  }
  else
    image_base = GetModuleHandleA(NULL);

  if (inst_debug)
    std::cout << "\nENTRY idt_fixup (" << filename << ")\n";

  if (inst_debug)
    std::cout << "image_base = " << image_base << "\n";

  PIMAGE_DOS_HEADER dos_headers = (PIMAGE_DOS_HEADER)image_base;
  if (dos_headers->e_magic != IMAGE_DOS_SIGNATURE)
  {
    std::cerr << "Invalid DOS signature\n";
    return 0;
  }

  PIMAGE_NT_HEADERS nt_headers =
      (PIMAGE_NT_HEADERS)((DWORD_PTR)image_base + dos_headers->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
  {
    std::cerr << "Invalid NT signature\n";
    return 0;
  }

  PIMAGE_IMPORT_DESCRIPTOR import_descriptor = NULL;
  IMAGE_DATA_DIRECTORY importsDirectory =
      nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (importsDirectory.Size == 0)
  {
    std::cerr << "No import directory found\n";
    return 0;
  }

  import_descriptor =
      (PIMAGE_IMPORT_DESCRIPTOR)(importsDirectory.VirtualAddress +
                                 (DWORD_PTR)image_base);
  LPCSTR library_name = NULL;
  HMODULE library = NULL;
  PIMAGE_IMPORT_BY_NAME function_name = NULL;

  GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCTSTR>(&idt_fixup), &library);

  while (import_descriptor->Name != NULL)
  {
    library_name =
        reinterpret_cast<LPCSTR>(reinterpret_cast<DWORD_PTR>(image_base) +
                                 import_descriptor->Name);

    #if defined(_MSC_VER)
    #define stricmp _stricmp
    #else
    #include <strings.h> // POSIX header for strcasecmp
    #define stricmp strcasecmp
    #endif
    if (!stricmp(library_name, "xrt_coreutil.dll"))
    {
      PIMAGE_THUNK_DATA original_first_thunk = NULL, first_thunk = NULL;
      original_first_thunk =
          (PIMAGE_THUNK_DATA)((DWORD_PTR)image_base +
                              import_descriptor->OriginalFirstThunk);
      first_thunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)image_base +
                                       import_descriptor->FirstThunk);
      while (original_first_thunk->u1.AddressOfData != NULL)
      {
        function_name =
            (PIMAGE_IMPORT_BY_NAME)((DWORD_PTR)image_base +
                                    original_first_thunk->u1.AddressOfData);

        auto ptr_itr = fname2fptr_map.find(demangle(function_name->Name));
        if (ptr_itr != fname2fptr_map.end())
        {
          void** temp = ptr_itr->second;
          /* update the original function address in the dispatch table */
          *temp = reinterpret_cast<void*>(first_thunk->u1.Function);
          fptr2fname_map[*temp] = it.first;

          void* func_ptr = GetProcAddress(library, function_name->Name);
          if (func_ptr)
          {
            if (inst_debug)
              std::cout << demangle(function_name->Name).c_str() << "\n\tOrg = "
                << std::uppercase << std::hex << std::setw(16)
                << std::setfill('0') << first_thunk->u1.Function << " New = "
                << std::uppercase << std::hex << std::setw(16)
                << std::setfill('0') << (ULONG64)func_ptr <<"\n";

            replace_func(first_thunk, func_ptr);
          }
        }
        else if (inst_debug)
          std::cout << "func :: \"" << demangle(function_name->Name) << "\""
                << "not found in fname2fptr_map\n";

        ++original_first_thunk;
        ++first_thunk;
      }
    }

    import_descriptor++;
  }

  if (inst_debug)
    std::cout << "EXIT idt_fixup ("<< filename << ")\n\n";

  return 0;
}
#endif /* #ifdef __linux__ */
