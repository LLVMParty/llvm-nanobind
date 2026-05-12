/*===-- object.c - tool for testing libLLVM and llvm-c API ----------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file implements the --object-list-sections and --object-list-symbols  *|
|* commands in llvm-c-test.                                                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm-c-test.h"
#include "llvm-c/Object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int llvm_object_list_sections(void) {
  LLVMMemoryBufferRef MB;
  LLVMBinaryRef O;
  LLVMSectionIteratorRef sect;

  char *outBufferErr = NULL;
  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &outBufferErr)) {
    fprintf(stderr, "Error reading file: %s\n", outBufferErr);
    free(outBufferErr);
    exit(1);
  }

  char *outBinaryErr = NULL;
  O = LLVMCreateBinary(MB, LLVMGetGlobalContext(), &outBinaryErr);
  if (!O || outBinaryErr) {
    fprintf(stderr, "Error reading object: %s\n", outBinaryErr);
    free(outBinaryErr);
    exit(1);
  }

  sect = LLVMObjectFileCopySectionIterator(O);
  while (sect && !LLVMObjectFileIsSectionIteratorAtEnd(O, sect)) {
    printf("'%s': @0x%08" PRIx64 " +%" PRIu64 "\n", LLVMGetSectionName(sect),
           LLVMGetSectionAddress(sect), LLVMGetSectionSize(sect));

    LLVMMoveToNextSection(sect);
  }

  LLVMDisposeSectionIterator(sect);

  LLVMDisposeBinary(O);

  LLVMDisposeMemoryBuffer(MB);

  return 0;
}

typedef struct SymbolInfo {
  const char *Name;
  const char *SectionName;
  uint64_t Address;
  uint64_t SectionAddress;
  uint64_t SectionSize;
  uint64_t Size;
  int HasSection;
} SymbolInfo;

static int same_section(const SymbolInfo *A, const SymbolInfo *B) {
  if (!A->HasSection || !B->HasSection)
    return 0;
  if (A->SectionAddress != B->SectionAddress ||
      A->SectionSize != B->SectionSize)
    return 0;
  if (A->SectionName == B->SectionName)
    return 1;
  if (!A->SectionName || !B->SectionName)
    return 0;
  return !strcmp(A->SectionName, B->SectionName);
}

static void compute_symbol_sizes(SymbolInfo *Symbols, size_t Count) {
  for (size_t I = 0; I < Count; ++I) {
    SymbolInfo *Sym = &Symbols[I];
    uint64_t SectionEnd;
    uint64_t NextAddress;

    Sym->Size = 0;
    if (!Sym->HasSection)
      continue;

    if (Sym->Name && Sym->SectionName && !strcmp(Sym->Name, Sym->SectionName))
      continue;

    SectionEnd = Sym->SectionAddress + Sym->SectionSize;
    NextAddress = SectionEnd;
    for (size_t J = 0; J < Count; ++J) {
      SymbolInfo *Other = &Symbols[J];
      if (I == J || !same_section(Sym, Other))
        continue;
      if (Other->Address > Sym->Address && Other->Address < NextAddress)
        NextAddress = Other->Address;
    }

    if (NextAddress >= Sym->Address)
      Sym->Size = NextAddress - Sym->Address;
  }
}

int llvm_object_list_symbols(void) {
  LLVMMemoryBufferRef MB;
  LLVMBinaryRef O;
  LLVMSectionIteratorRef sect;
  LLVMSymbolIteratorRef sym;
  SymbolInfo *Symbols = NULL;
  size_t NumSymbols = 0;
  size_t Capacity = 0;

  char *outBufferErr = NULL;
  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &outBufferErr)) {
    fprintf(stderr, "Error reading file: %s\n", outBufferErr);
    free(outBufferErr);
    exit(1);
  }

  char *outBinaryErr = NULL;
  O = LLVMCreateBinary(MB, LLVMGetGlobalContext(), &outBinaryErr);
  if (!O || outBinaryErr) {
    fprintf(stderr, "Error reading object: %s\n", outBinaryErr);
    free(outBinaryErr);
    exit(1);
  }

  sect = LLVMObjectFileCopySectionIterator(O);
  sym = LLVMObjectFileCopySymbolIterator(O);
  while (sect && sym && !LLVMObjectFileIsSymbolIteratorAtEnd(O, sym)) {
    SymbolInfo *Info;
    if (NumSymbols == Capacity) {
      size_t NewCapacity = Capacity ? Capacity * 2 : 16;
      SymbolInfo *NewSymbols =
          (SymbolInfo *)realloc(Symbols, NewCapacity * sizeof(SymbolInfo));
      if (!NewSymbols) {
        fprintf(stderr, "Out of memory while reading symbols\n");
        free(Symbols);
        LLVMDisposeSymbolIterator(sym);
        LLVMDisposeSectionIterator(sect);
        LLVMDisposeBinary(O);
        LLVMDisposeMemoryBuffer(MB);
        exit(1);
      }
      Symbols = NewSymbols;
      Capacity = NewCapacity;
    }

    Info = &Symbols[NumSymbols++];
    Info->Name = LLVMGetSymbolName(sym);
    Info->Address = LLVMGetSymbolAddress(sym);
    Info->SectionName = NULL;
    Info->SectionAddress = 0;
    Info->SectionSize = 0;
    Info->Size = 0;
    Info->HasSection = 0;

    LLVMMoveToContainingSection(sect, sym);
    if (!LLVMObjectFileIsSectionIteratorAtEnd(O, sect)) {
      Info->SectionName = LLVMGetSectionName(sect);
      Info->SectionAddress = LLVMGetSectionAddress(sect);
      Info->SectionSize = LLVMGetSectionSize(sect);
      Info->HasSection = 1;
    }

    LLVMMoveToNextSymbol(sym);
  }

  compute_symbol_sizes(Symbols, NumSymbols);
  for (size_t I = 0; I < NumSymbols; ++I) {
    SymbolInfo *Info = &Symbols[I];
    printf("%s @0x%08" PRIx64 " +%" PRIu64 " (%s)\n", Info->Name,
           Info->Address, Info->Size,
           Info->SectionName ? Info->SectionName : "(null)");
  }

  free(Symbols);

  LLVMDisposeSymbolIterator(sym);
  LLVMDisposeSectionIterator(sect);

  LLVMDisposeBinary(O);

  LLVMDisposeMemoryBuffer(MB);

  return 0;
}
