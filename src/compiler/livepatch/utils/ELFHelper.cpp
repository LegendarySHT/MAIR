#include "debug.h"
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <iostream>
#include <libelf.h>
#include <linux/limits.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "PatchHelper.h"

namespace {

// Now this parser only support the loaded ELF file, i.e., /proc/self/exe
/// TODO: support ELF parsing for shared library. Along with the base address
/// resolution for shared library, we can also support the unexported symbol
/// resolution for shared library.
class SelfELFParser {
  static constexpr const char *SelfPath = "/proc/self/exe";

public:
  SelfELFParser() : fd(open(SelfPath, O_RDONLY, 0)) {
    if (fd < 0) {
      FATAL("open(/proc/self/exe) failed: %s", std::strerror(errno));
    }

    if (elf_version(EV_CURRENT) == EV_NONE) {
      FATAL("ELF library initialization failed: %s", elf_errmsg(-1));
    }

    e = elf_begin(fd, ELF_C_READ, NULL);
    if (!e) {
      close(fd);
      FATAL("elf_begin() failed: %s", elf_errmsg(-1));
    }

    if (elf_kind(e) != ELF_K_ELF) {
      elf_end(e);
      close(fd);
      FATAL("File is not an ELF object: %s", SelfPath);
    }

    if (gelf_getehdr(e, &ehdr) == NULL) {
      elf_end(e);
      close(fd);
      FATAL("Failed to get ELF header: %s", elf_errmsg(-1));
    }

    switch (ehdr.e_type) {
    case ET_EXEC:
      is_pie = false;
      break;
    case ET_DYN:
      is_pie = true;
      break;
    default:
      is_pie = is_self_proc_pie();
      break;
    }

    symtab = parse_symtab();
  }

  Elf64_Addr search_symbol_offset_in_symtab(const std::string &symbol_name) {
    if (!symtab) {
      symtab = parse_symtab();
    }

    if (!symtab) {
      FATAL("Failed to find .symtab in current process");
    }

    uint64_t symbol_offset = 0;
    size_t symbol_count = shdr.sh_size / shdr.sh_entsize;
    for (size_t i = 0; i < symbol_count; ++i) {
      GElf_Sym sym;
      if (gelf_getsym(symtab, (int)i, &sym) != &sym) {
        continue;
      }

      // Obtain the symbol name
      const char *name = elf_strptr(e, shdr.sh_link, sym.st_name);
      if (!name || name != symbol_name) {
        continue;
      }
      // We only care about function symbols
      if (GELF_ST_TYPE(sym.st_info) == STT_FUNC) {
        // 检查是否为 PIC 符号
        if (GELF_ST_BIND(sym.st_info) == STB_GLOBAL &&
            sym.st_shndx == SHN_UNDEF) {
          // 如果是 PIC 符号，返回基址加上符号值
          return (Elf64_Addr)get_base_address() + sym.st_value;
        }
        return sym.st_value;
      }
    }

    FATAL("Failed to find symbol %s in current process", symbol_name.c_str());
  }

  bool hasSymTab() { return symtab != nullptr; }

  ~SelfELFParser() {
    close(fd);
    elf_end(e);
  }

private:
  Elf_Data *parse_symtab() {
    Elf_Scn *scn = nullptr;

    symtab = nullptr;

    while ((scn = elf_nextscn(e, scn)) != nullptr) {
      if (gelf_getshdr(scn, &shdr) != &shdr) {
        continue;
      }

      if (shdr.sh_type != SHT_SYMTAB) {
        continue;
      }

      symtab = elf_getdata(scn, nullptr);
      if (symtab) {
        break;
      }
    }

    return symtab;
  }

private:
  int fd;
  Elf *e;
  GElf_Shdr shdr;
  GElf_Ehdr ehdr;
  Elf_Data *symtab = nullptr;
  bool is_pie;
};

SelfELFParser Parser;

} // namespace

bool isSelfProcStripped() { return !Parser.hasSymTab(); }

// Return the address of the symbol in current process via ELF resolution
void *find_symtab_symbol_addr(const std::string &symbol_name) {
  Elf64_Addr symbol_offset = Parser.search_symbol_offset_in_symtab(symbol_name);
  void *base_address = get_base_address();
  void *symbol_address = (char *)base_address + symbol_offset;
  return symbol_address;
}
