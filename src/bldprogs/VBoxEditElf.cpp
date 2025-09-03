/* $Id$ */
/** @file
 * VBoxEditElf - Simple ELF binary file editor.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/json.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/types.h>
#include <iprt/formats/elf32.h>
#include <iprt/formats/elf64.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Symbol type.
 */
typedef enum ELDEDITSYMTYPE
{
    kElfEditSymType_Invalid = 0,
    kElfEditSymType_Function,
    kElfEditSymType_Object,
    kElfEditSymType_Tls,
    kElfEditSymType_GnuIndFunc,
    kElfEditSymType_NoType,
    kElfEditSymType_32Bit_Hack = 0x7fffffff
} ELDEDITSYMTYPE;


/**
 * Symbol visbility.
 */
typedef enum ELDEDITSYMVISIBILITY
{
    kElfEditSymVisbility_Invalid = 0,
    kElfEditSymVisbility_Default,
    kElfEditSymVisbility_Hidden,
    kElfEditSymVisbility_Internal,
    kElfEditSymVisbility_Protected,
    kElfEditSymVisbility_32Bit_Hack = 0x7fffffff
} ELDEDITSYMVISIBILITY;


/**
 * A symbol version.
 */
typedef struct ELFEDITSTUBVERSION
{
    /** The version name. */
    const char              *pszVersion;
    /** The parent version name, optional. */
    const char              *pszParent;
    /** The version index. */
    uint32_t                idxVersion;
} ELFEDITSTUBVERSION;
typedef ELFEDITSTUBVERSION *PELFEDITSTUBVERSION;
typedef const ELFEDITSTUBVERSION *PCELFEDITSTUBVERSION;


/**
 * A symbol.
 */
typedef struct ELFEDITSTUBSYM
{
    /** The name of the symbol. */
    const char              *pszName;
    /** The symbol type */
    ELDEDITSYMTYPE          enmType;
    /** The symbol visbility. */
    ELDEDITSYMVISIBILITY    enmVisibility;
    /** Size of the symbol in bytes. */
    size_t                  cbSym;
    /** Flag whether this is a weak symbol. */
    bool                    fWeak;
    /** The version assigned for this symbol, NULL if no versioning. */
    PCELFEDITSTUBVERSION    pVersion;
    /** Flag whether this is the default version of the symbol, only valid if versioning is set. */
    bool                    fDefaultVersion;
} ELFEDITSTUBSYM;
/** Pointer to a symbol. */
typedef ELFEDITSTUBSYM *PELFEDITSTUBSYM;
/** Pointer to a constant symbol. */
typedef const ELFEDITSTUBSYM *PCELFEDITSTUBSYM;


/**
 * Entry in the string table.
 */
typedef struct ELFEDITSTR
{
    RTSTRSPACECORE          StrCore;
    /** The offset in the final string table. */
    size_t                  offStrTab;
} ELFEDITSTR;
typedef ELFEDITSTR *PELFEDITSTR;
typedef const ELFEDITSTR *PCELFEDITSTR;


/**
 * Stub image state
 */
typedef struct ELFEDITSTUBIMG
{
    /** The architecture of the image. */
    RTLDRARCH           enmArch;
    /** The string space. */
    RTSTRSPACE          StrSpace;
    /** Current length of the string table. */
    size_t              cbStrTab;
    /** The image name. */
    const char          *pszName;
    /** Array of neded libraries this image depends on. */
    const char          **papszNeeded;
    /** Number of entries in the needed libraries array. */
    size_t              cNeeded;
    /** Maximum number of entries the needed libraries array can hold. */
    size_t              cNeededMax;
    /** The symbol table. */
    PELFEDITSTUBSYM     paSyms;
    /** Number of entries in the symbol table. */
    size_t              cSyms;
    /** The version table. */
    PELFEDITSTUBVERSION paVersions;
    /** Number of entries in the version table. */
    size_t              cVersions;
} ELFEDITSTUBIMG;
/** Pointer to a stub image state. */
typedef ELFEDITSTUBIMG *PELFEDITSTUBIMG;
/** Pointer to a const stub image state. */
typedef const ELFEDITSTUBIMG *PCELFEDITSTUBIMG;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Options
 * @{ */
static enum
{
    kVBoxEditElfAction_Nothing,
    kVBoxEditElfAction_DeleteRunpath,
    kVBoxEditElfAction_ChangeRunpath,
    kVBoxEditElfAction_CreateLinkerStub,
    kVBoxEditElfAction_CreateJsonStub
}                 g_enmAction = kVBoxEditElfAction_Nothing;
static const char *g_pszInput = NULL;
/** Verbosity level. */
static int        g_cVerbosity = 0;
/** New runpath. */
static const char *g_pszRunpath = NULL;
/** The output path for the stub library. */
static const char *g_pszLinkerStub = NULL;
/** @} */

static const char s_achShStrTab[] = "\0.shstrtab\0.dynsym\0.dynstr\0.gnu.version\0.gnu.version_d\0.gnu.version_r\0.dynamic";


static void verbose(const char *pszFmt, ...)
{
    if (g_cVerbosity == 0)
        return;

    va_list Args;
    va_start(Args, pszFmt);
    RTMsgInfoV(pszFmt, Args);
    va_end(Args);
}


static const char *elfEditImgAddString(PELFEDITSTUBIMG pStubImg, const char *pszString)
{
    PELFEDITSTR pStr = (PELFEDITSTR)RTStrSpaceGet(&pStubImg->StrSpace, pszString);
    if (pStr)
        return pStr->StrCore.pszString;

    /* Allocate and insert. */
    size_t const cchStr = strlen(pszString);
    pStr = (PELFEDITSTR)RTMemAlloc(sizeof(*pStr) + cchStr + 1);
    if (!pStr)
        return NULL;

    pStr->StrCore.cchString = cchStr;
    pStr->StrCore.pszString = (char *)memcpy(pStr + 1, pszString, cchStr + 1);
    pStr->offStrTab         = 0;
    if (!RTStrSpaceInsert(&pStubImg->StrSpace, &pStr->StrCore))
        AssertFailed();

    return pStr->StrCore.pszString;
}


/* Select ELF mode and include the template. */
#define ELF_MODE            32
#include "VBoxEditElf-template.cpp.h"
#undef ELF_MODE


#define ELF_MODE            64
#include "VBoxEditElf-template.cpp.h"
#undef ELF_MODE

static RTEXITCODE deleteRunpath(const char *pszInput)
{
    RTFILE hFileElf = NIL_RTFILE;
    int rc = RTFileOpen(&hFileElf, pszInput, RTFILE_O_OPEN | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Filed to open file '%s': %Rrc\n", pszInput, rc);

    /* Only support for 64-bit ELF files currently. */
    Elf64_Ehdr Hdr;
    rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
    {
        RTFileClose(hFileElf);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);
    }

    if (    Hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Hdr.e_ident[EI_MAG3] != ELFMAG3)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF magic (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_CLASS] != ELFCLASS64)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF class (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_DATA] != ELFDATA2LSB)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF endian %x is unsupported", Hdr.e_ident[EI_DATA]);
    if (Hdr.e_version != EV_CURRENT)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF version %x is unsupported", Hdr.e_version);

    if (sizeof(Elf64_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf64_Ehdr));
    if (    sizeof(Elf64_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf64_Phdr));
    if (sizeof(Elf64_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf64_Shdr));

    /* Find dynamic section. */
    Elf64_Phdr Phdr; RT_ZERO(Phdr);
    bool fFound = false;
    for (uint32_t i = 0; i < Hdr.e_phnum; i++)
    {
        rc = RTFileReadAt(hFileElf, Hdr.e_phoff + i * sizeof(Phdr), &Phdr, sizeof(Phdr), NULL);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFileElf);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);
        }
        if (Phdr.p_type == PT_DYNAMIC)
        {
            if (!Phdr.p_filesz)
            {
                RTFileClose(hFileElf);
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Dynmic section in '%s' is empty\n", pszInput);
            }
            fFound = true;
            break;
        }
    }

    if (!fFound)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF binary '%s' doesn't contain dynamic section\n", pszInput);

    Elf64_Dyn *paDynSh = (Elf64_Dyn *)RTMemAllocZ(Phdr.p_filesz);
    if (!paDynSh)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes of memory for dynamic section of '%s'\n", Phdr.p_filesz, pszInput);

    rc = RTFileReadAt(hFileElf, Phdr.p_offset, paDynSh, Phdr.p_filesz, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);

    /* Remove all DT_RUNPATH entries and padd the remainder with DT_NULL. */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < Phdr.p_filesz / sizeof(Elf64_Dyn); i++)
    {
        paDynSh[idx] = paDynSh[i];
        if (paDynSh[i].d_tag != DT_RPATH && paDynSh[i].d_tag != DT_RUNPATH)
            idx++;
    }

    while (idx < Phdr.p_filesz / sizeof(Elf64_Dyn))
    {
        paDynSh[idx].d_tag = DT_NULL;
        paDynSh[idx].d_un.d_val = 0;
        idx++;
    }

    /* Write the result. */
    rc = RTFileWriteAt(hFileElf, Phdr.p_offset, paDynSh, Phdr.p_filesz, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write updated ELF dynamic section for '%s': %Rrc\n", pszInput, rc);

    RTMemFree(paDynSh);
    RTFileClose(hFileElf);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE changeRunpathEntry(RTFILE hFileElf, const char *pszInput, Elf64_Ehdr *pHdr, Elf64_Xword offInStrTab, const char *pszRunpath)
{
    /* Read section headers to find the string table. */
    size_t const cbShdrs = pHdr->e_shnum * sizeof(Elf64_Shdr);
    Elf64_Shdr *paShdrs = (Elf64_Shdr *)RTMemAlloc(cbShdrs);
    if (!paShdrs)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes of memory for section headers of '%s'\n", cbShdrs, pszInput);

    int rc = RTFileReadAt(hFileElf, pHdr->e_shoff, paShdrs, cbShdrs, NULL);
    if (RT_FAILURE(rc))
    {
        RTMemFree(paShdrs);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read %zu bytes of section headers from '%s': %Rrc\n", cbShdrs, pszInput, rc);
    }

    uint32_t idx;
    for (idx = 0; idx < pHdr->e_shnum; idx++)
    {
        if (paShdrs[idx].sh_type == SHT_STRTAB)
            break;
    }

    if (idx == pHdr->e_shnum)
    {
        RTMemFree(paShdrs);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF binary '%s' does not contain a string table\n", pszInput);
    }

    size_t const cbStrTab  = paShdrs[idx].sh_size;
    RTFOFF const offStrTab = paShdrs[idx].sh_offset;
    RTMemFree(paShdrs);

    if (offInStrTab >= cbStrTab)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table offset of runpath entry is out of bounds: got %#RX64, maximum is %zu\n", offInStrTab, cbStrTab - 1);

    /* Read the string table. */
    char *pbStrTab = (char *)RTMemAllocZ(cbStrTab + 1); /* Force a zero terminator. */
    if (!pbStrTab)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes of memory for string table of '%s'\n", cbStrTab + 1, pszInput);

    rc = RTFileReadAt(hFileElf, offStrTab, pbStrTab, cbStrTab, NULL);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pbStrTab);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read %zu bytes of the string table from '%s': %Rrc\n", cbStrTab, pszInput, rc);
    }

    /* Calculate the maximum number of characters we can replace. */
    char *pbStr = &pbStrTab[offInStrTab];
    size_t cchMax = strlen(pbStr);
    while (   &pbStr[cchMax + 1] < &pbStrTab[cbStrTab]
           && pbStr[cchMax] == '\0')
        cchMax++;

    size_t const cchNewRunpath = strlen(pszRunpath);
    if (cchMax < cchNewRunpath)
    {
        RTMemFree(pbStrTab);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "New runpath '%s' is too long to overwrite current one, maximum length is: %zu\n", cchNewRunpath, cchMax);
    }

    memcpy(pbStr, pszRunpath, cchNewRunpath + 1);
    rc = RTFileWriteAt(hFileElf, offStrTab, pbStrTab, cbStrTab, NULL);
    RTMemFree(pbStrTab);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Writing altered string table failed: %Rrc\n", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE changeRunpath(const char *pszInput, const char *pszRunpath)
{
    RTFILE hFileElf = NIL_RTFILE;
    int rc = RTFileOpen(&hFileElf, pszInput, RTFILE_O_OPEN | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Filed to open file '%s': %Rrc\n", pszInput, rc);

    /* Only support for 64-bit ELF files currently. */
    Elf64_Ehdr Hdr;
    rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
    {
        RTFileClose(hFileElf);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);
    }

    if (    Hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Hdr.e_ident[EI_MAG3] != ELFMAG3)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF magic (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_CLASS] != ELFCLASS64)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF class (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_DATA] != ELFDATA2LSB)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF endian %x is unsupported", Hdr.e_ident[EI_DATA]);
    if (Hdr.e_version != EV_CURRENT)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF version %x is unsupported", Hdr.e_version);

    if (sizeof(Elf64_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf64_Ehdr));
    if (    sizeof(Elf64_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf64_Phdr));
    if (sizeof(Elf64_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf64_Shdr));

    /* Find dynamic section. */
    Elf64_Phdr Phdr; RT_ZERO(Phdr);
    bool fFound = false;
    for (uint32_t i = 0; i < Hdr.e_phnum; i++)
    {
        rc = RTFileReadAt(hFileElf, Hdr.e_phoff + i * sizeof(Phdr), &Phdr, sizeof(Phdr), NULL);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFileElf);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);
        }
        if (Phdr.p_type == PT_DYNAMIC)
        {
            if (!Phdr.p_filesz)
            {
                RTFileClose(hFileElf);
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Dynmic section in '%s' is empty\n", pszInput);
            }
            fFound = true;
            break;
        }
    }

    if (!fFound)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF binary '%s' doesn't contain dynamic section\n", pszInput);

    Elf64_Dyn *paDynSh = (Elf64_Dyn *)RTMemAllocZ(Phdr.p_filesz);
    if (!paDynSh)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes of memory for dynamic section of '%s'\n", Phdr.p_filesz, pszInput);

    rc = RTFileReadAt(hFileElf, Phdr.p_offset, paDynSh, Phdr.p_filesz, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);

    /* Look for the first DT_RUNPATH entry and rewrite it. */
    for (uint32_t i = 0; i < Phdr.p_filesz / sizeof(Elf64_Dyn); i++)
    {
        if (   paDynSh[i].d_tag == DT_RPATH
            || paDynSh[i].d_tag == DT_RUNPATH)
        {
            RTEXITCODE rcExit = changeRunpathEntry(hFileElf, pszInput, &Hdr, paDynSh[i].d_un.d_val, pszRunpath);
            RTMemFree(paDynSh);
            RTFileClose(hFileElf);
            return rcExit;
        }
    }

    RTMemFree(paDynSh);
    RTFileClose(hFileElf);
    return RTMsgErrorExit(RTEXITCODE_FAILURE, "No DT_RPATH or DT_RUNPATH entry found in '%s'\n", pszInput);
}


static RTEXITCODE createLinkerStubFrom(const char *pszInput, const char *pszStubPath)
{
    RTFILE hFileElf = NIL_RTFILE;
    int rc = RTFileOpen(&hFileElf, pszInput, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Filed to open file '%s': %Rrc\n", pszInput, rc);

    /* Only support for 64-bit ELF files currently. */
    Elf64_Ehdr Hdr;
    rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
    {
        RTFileClose(hFileElf);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);
    }

    if (    Hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Hdr.e_ident[EI_MAG3] != ELFMAG3)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Invalid ELF magic (%.*Rhxs)", pszInput, sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_CLASS] != ELFCLASS64)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF class (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_DATA] != ELFDATA2LSB)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF endian %x is unsupported", Hdr.e_ident[EI_DATA]);
    if (Hdr.e_version != EV_CURRENT)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF version %x is unsupported", Hdr.e_version);

    if (sizeof(Elf64_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf64_Ehdr));
    if (    sizeof(Elf64_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf64_Phdr));
    if (sizeof(Elf64_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf64_Shdr));

    /* Find all the dynamic sections we need. */
    uint32_t idStrTab = UINT32_MAX;
    uint32_t idDynamic = UINT32_MAX;
    Elf64_Shdr ShdrDyn; RT_ZERO(ShdrDyn);
    char *pachStrTab = NULL; size_t cbStrTab = 0;
    Elf64_Sym *paDynSyms = NULL; size_t cbDynSyms = 0; uint32_t u32DynSymInfo = 0;
    uint16_t *pu16GnuVerSym = NULL; size_t cbGnuVerSym = 0;
    uint8_t *pbGnuVerDef = NULL; size_t cbGnuVerDef = 0; uint32_t cVerdefEntries = 0;
    uint8_t *pbGnuVerNeed = NULL; size_t cbGnuVerNeed = 0; uint32_t cVerNeedEntries = 0;

    for (uint32_t i = 0; i < Hdr.e_shnum; i++)
    {
        Elf64_Shdr Shdr; RT_ZERO(Shdr);
        rc = RTFileReadAt(hFileElf, Hdr.e_shoff + i * sizeof(Shdr), &Shdr, sizeof(Shdr), NULL /*pcbRead*/);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read section header at %RX64 from '%s'\n", Hdr.e_shoff + i * sizeof(Shdr), pszInput);

        verbose("Section header %u:\n"
                "    sh_name:      %RU32\n"
                "    sh_type:      %RX32\n"
                "    sh_flags:     %#RX64\n"
                "    sh_addr:      %#RX64\n"
                "    sh_offset:    %RU64\n"
                "    sh_size:      %RU64\n"
                "    sh_link:      %RU16\n"
                "    sh_info:      %RU16\n"
                "    sh_addralign: %#RX64\n"
                "    sh_entsize:   %#RX64\n", i,
                Shdr.sh_name, Shdr.sh_type, Shdr.sh_flags,
                Shdr.sh_addr, Shdr.sh_offset, Shdr.sh_size,
                Shdr.sh_link, Shdr.sh_info, Shdr.sh_addralign,
                Shdr.sh_entsize);

        switch (Shdr.sh_type)
        {
            case SHT_DYNSYM:
            {
                cbDynSyms = Shdr.sh_size;
                u32DynSymInfo = Shdr.sh_info;
                paDynSyms = (Elf64_Sym *)RTMemAllocZ(cbDynSyms);
                if (!paDynSyms)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .dynsym section in '%s'\n", cbDynSyms, pszInput);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, paDynSyms, cbDynSyms, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64 from '%s'\n", Shdr.sh_offset, pszInput);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u in '%s'\n", Shdr.sh_link, idStrTab, pszInput);
                break;
            }
            case SHT_GNU_versym:
            {
                cbGnuVerSym = Shdr.sh_size;
                pu16GnuVerSym = (uint16_t *)RTMemAllocZ(cbGnuVerSym);
                if (!pu16GnuVerSym)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version section in '%s'\n", cbGnuVerSym, pszInput);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pu16GnuVerSym, cbGnuVerSym, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64 from '%s'\n", Shdr.sh_offset, pszInput);

                /** @todo It should link to the .dynsym table. */
                break;
            }
            case SHT_GNU_verdef:
            {
                cbGnuVerDef = Shdr.sh_size;
                pbGnuVerDef = (uint8_t *)RTMemAllocZ(cbGnuVerDef);
                cVerdefEntries = Shdr.sh_info;
                if (!pbGnuVerDef)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version_d section in '%s'\n", cbGnuVerDef, pszInput);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pbGnuVerDef, cbGnuVerDef, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64 from '%s'\n", Shdr.sh_offset, pszInput);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u in '%s'\n", Shdr.sh_link, idStrTab, pszInput);
                break;
            }
            case SHT_GNU_verneed:
            {
                cbGnuVerNeed = Shdr.sh_size;
                pbGnuVerNeed = (uint8_t *)RTMemAllocZ(cbGnuVerNeed);
                cVerNeedEntries = Shdr.sh_info;
                if (!pbGnuVerNeed)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version_r section in '%s'\n", cbGnuVerNeed, pszInput);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pbGnuVerNeed, cbGnuVerNeed, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .gnu.version_r section at %RX64 from '%s'\n", Shdr.sh_offset, pszInput);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u in '%s'\n", Shdr.sh_link, idStrTab, pszInput);
                break;
            }
            case SHT_DYNAMIC:
            {
                if (idDynamic != UINT32_MAX)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "More than one .dynamic section in '%s'\n", pszInput);

                idDynamic = i;
                ShdrDyn = Shdr;
                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u in '%s'\n", Shdr.sh_link, idStrTab, pszInput);
            }
            default:
                break; /* Ignored. */
        }
    }

    if (idStrTab == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index not found in '%s'\n", pszInput);
    if (idDynamic == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, ".dynamic section index not found in '%s'\n", pszInput);

    if (pbGnuVerDef && !pu16GnuVerSym)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Versioned library '%s' misses .gnu.version\n", pszInput);

    /* Read the string table section header. */
    Elf64_Shdr Shdr; RT_ZERO(Shdr);
    rc = RTFileReadAt(hFileElf, Hdr.e_shoff + idStrTab * sizeof(Shdr), &Shdr, sizeof(Shdr), NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .strtab section header at %RX64 from '%s': %Rrc\n",
                              Hdr.e_shoff + idStrTab * sizeof(Shdr), pszInput, rc);

    cbStrTab = Shdr.sh_size;
    pachStrTab = (char *)RTMemAllocZ(cbStrTab + 1); /* Force termination */
    if (!pachStrTab)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .strtab section in '%s'\n", cbStrTab, pszInput);

    rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pachStrTab, cbStrTab, NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .strtab section at %RX64 from '%s'\n", Shdr.sh_offset, pszInput);

    /* Read the .dynamic section. */
    Elf64_Dyn *paDynEnt = (Elf64_Dyn *)RTMemAllocZ(ShdrDyn.sh_size);
    if (!paDynEnt)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .dynamic section in '%s'\n", ShdrDyn.sh_size, pszInput);

    rc = RTFileReadAt(hFileElf, ShdrDyn.sh_offset, paDynEnt, ShdrDyn.sh_size, NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynamic section at %RX64 from '%s'\n", ShdrDyn.sh_offset, pszInput);

    RTFileClose(hFileElf);

    verbose("Processing symbol table\n");

    /* Remove all undefined entries from .dynsym and rewrite all exposed symbols to point to the first section header in the stub. */
    uint32_t cDynSymsExport = 0;
    uint32_t idxFirstNonLocalSym = 0;
    for (uint32_t i = 0; i < cbDynSyms / sizeof(*paDynSyms); i++)
    {
        if (1 /*paDynSyms[i].st_shndx*/)
        {
            paDynSyms[cDynSymsExport] = paDynSyms[i];
            paDynSyms[cDynSymsExport].st_shndx = paDynSyms[cDynSymsExport].st_shndx ? 1 : 0;
            paDynSyms[cDynSymsExport].st_value = 0;
            if (pu16GnuVerSym)
                pu16GnuVerSym[cDynSymsExport] = pu16GnuVerSym[i];

            if (ELF64_ST_BIND(paDynSyms[cDynSymsExport].st_info) != STB_LOCAL)
                idxFirstNonLocalSym = cDynSymsExport;

            cDynSymsExport++;
        }
        else
            verbose("Nuked symbol entry %u\n", i);
    }

#if 0
    /* Figure out the number of .gnu.version entries. */
    if (pbGnuVerDef)
    {
        cVerdefEntries = 1;
        Elf64_Verdef *pVerDef = (Elf64_Verdef *)pbGnuVerDef;
        for (;;)
        {
            if (!pVerDef->vd_next)
                break;

            cVerdefEntries++;
            pVerDef = (Elf64_Verdef *)((uint8_t *)pVerDef + pVerDef->vd_next);
            if ((uintptr_t)pVerDef >= (uintptr_t)pbGnuVerDef + cbGnuVerDef)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Version definition next entry points outside .gnu.version section '%s': %Rrc\n", pszInput);
        }
    }
#endif

    /* Start writing the output ELF file. */

    verbose("Writing stub binary\n");

    RTFILE hFileOut = NIL_RTFILE;
    rc = RTFileOpen(&hFileOut, pszStubPath, RTFILE_O_CREATE_REPLACE | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create file '%s': %Rrc\n", pszStubPath, rc);

    /* Program headers. */
    Elf64_Phdr aPhdrs[4];

    /* Rewrite the header. */
    Hdr.e_phoff = sizeof(Hdr);
    Hdr.e_shoff = Hdr.e_phoff + sizeof(aPhdrs);
    Hdr.e_phnum = RT_ELEMENTS(aPhdrs);
    Hdr.e_shnum = pbGnuVerDef ? 8 : 6; /* NULL + .dynsym + .dynstr + (.gnu.version + gnu.version_d + gnu.version_r) + .dynamic + .shstrtab */
    Hdr.e_shstrndx = pbGnuVerDef ? 7 : 5;

    Elf64_Shdr aShdrs[8]; RT_ZERO(aShdrs);
    uint32_t idx = 1;
    /* NULL header */
    /* .dynsym */
    aShdrs[idx].sh_name      = 11;
    aShdrs[idx].sh_type      = SHT_DYNSYM;
    aShdrs[idx].sh_flags     = SHF_ALLOC;
    aShdrs[idx].sh_addr      = 0;
    aShdrs[idx].sh_offset    = Hdr.e_shoff + Hdr.e_shnum * sizeof(aShdrs[0]);
    aShdrs[idx].sh_size      = cDynSymsExport * sizeof(*paDynSyms);
    aShdrs[idx].sh_link      = 2;
    aShdrs[idx].sh_info      = u32DynSymInfo;
    aShdrs[idx].sh_addralign = sizeof(uint64_t);
    aShdrs[idx].sh_entsize   = sizeof(*paDynSyms);
    uint32_t const idxDynSym = idx++;

    /* .dynstr */
    aShdrs[idx].sh_name      = 19;
    aShdrs[idx].sh_type      = SHT_STRTAB;
    aShdrs[idx].sh_flags     = SHF_ALLOC;
    aShdrs[idx].sh_addr      = 0;
    aShdrs[idx].sh_offset    = aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size;
    aShdrs[idx].sh_size      = cbStrTab;
    aShdrs[idx].sh_link      = 0;
    aShdrs[idx].sh_info      = 0;
    aShdrs[idx].sh_addralign = sizeof(uint8_t);
    aShdrs[idx].sh_entsize   = 0;
    uint32_t const idxDynStr = idx++;

    uint32_t idxGnuVerSym = 0;
    uint32_t idxGnuVerDef = 0;
    uint32_t idxGnuVerNeed = 0;
    if (pbGnuVerDef || pbGnuVerNeed)
    {
        /* .gnu.version */
        aShdrs[idx].sh_name      = 27;
        aShdrs[idx].sh_type      = SHT_GNU_versym;
        aShdrs[idx].sh_flags     = SHF_ALLOC;
        aShdrs[idx].sh_addr      = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint16_t));
        aShdrs[idx].sh_offset    = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint16_t));
        aShdrs[idx].sh_size      = cDynSymsExport * sizeof(uint16_t);
        aShdrs[idx].sh_link      = 1; /* .dynsym */
        aShdrs[idx].sh_info      = 0;
        aShdrs[idx].sh_addralign = sizeof(uint16_t);
        aShdrs[idx].sh_entsize   = 2;
        idxGnuVerSym = idx++;

        if (pbGnuVerDef)
        {
            /* .gnu.version_d */
            aShdrs[idx].sh_name      = 40;
            aShdrs[idx].sh_type      = SHT_GNU_verdef;
            aShdrs[idx].sh_flags     = SHF_ALLOC;
            aShdrs[idx].sh_addr      = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
            aShdrs[idx].sh_offset    = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
            aShdrs[idx].sh_size      = cbGnuVerDef;
            aShdrs[idx].sh_link      = 2; /* .dynstr */
            aShdrs[idx].sh_info      = cVerdefEntries;
            aShdrs[idx].sh_addralign = sizeof(uint64_t);
            aShdrs[idx].sh_entsize   = 0;
            idxGnuVerDef = idx++;
        }

        if (pbGnuVerNeed)
        {
            /* .gnu.version_r */
            aShdrs[idx].sh_name      = 55;
            aShdrs[idx].sh_type      = SHT_GNU_verneed;
            aShdrs[idx].sh_flags     = SHF_ALLOC;
            aShdrs[idx].sh_addr      = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
            aShdrs[idx].sh_offset    = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
            aShdrs[idx].sh_size      = cbGnuVerNeed;
            aShdrs[idx].sh_link      = 2; /* .dynstr */
            aShdrs[idx].sh_info      = cVerNeedEntries;
            aShdrs[idx].sh_addralign = sizeof(uint64_t);
            aShdrs[idx].sh_entsize   = 0;
            idxGnuVerNeed = idx++;
        }
    }

    /* Process the .dynamic section. */
    uint32_t cDynEntries = 0;
    for (uint32_t i = 0; i < ShdrDyn.sh_size / sizeof(*paDynEnt); i++)
    {
        switch (paDynEnt[i].d_tag)
        {
            case DT_RUNPATH:
            case DT_RPATH:
            case DT_SONAME:
            case DT_NEEDED:
            case DT_FLAGS:
            case DT_FLAGS_1:
            case DT_STRSZ:
            case DT_SYMENT:
            case DT_VERDEFNUM:
            case DT_VERNEEDNUM:
                paDynEnt[cDynEntries++] = paDynEnt[i];
                break;
            case DT_VERDEF:
                paDynEnt[cDynEntries] = paDynEnt[i];
                paDynEnt[cDynEntries++].d_un.d_val = aShdrs[idxGnuVerDef].sh_offset;
                break;
            case DT_VERNEED:
                paDynEnt[cDynEntries] = paDynEnt[i];
                paDynEnt[cDynEntries++].d_un.d_val = aShdrs[idxGnuVerNeed].sh_offset;
                break;
            case DT_VERSYM:
                paDynEnt[cDynEntries] = paDynEnt[i];
                paDynEnt[cDynEntries++].d_un.d_val = aShdrs[idxGnuVerSym].sh_offset;
                break;
            case DT_STRTAB:
                paDynEnt[cDynEntries] = paDynEnt[i];
                paDynEnt[cDynEntries++].d_un.d_val = aShdrs[idxDynStr].sh_offset;
                break;
            case DT_SYMTAB:
                paDynEnt[cDynEntries] = paDynEnt[i];
                paDynEnt[cDynEntries++].d_un.d_val = aShdrs[idxDynSym].sh_offset;
                break;
        }
    }
    /* Close with 0. */
    paDynEnt[cDynEntries].d_tag = DT_NULL;
    paDynEnt[cDynEntries++].d_un.d_val = 0;

    /* .dynamic */
    aShdrs[idx].sh_name      = 70;
    aShdrs[idx].sh_type      = SHT_DYNAMIC;
    aShdrs[idx].sh_flags     = SHF_ALLOC | SHF_WRITE;
    aShdrs[idx].sh_addr      = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
    aShdrs[idx].sh_offset    = RT_ALIGN_64(aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size, sizeof(uint64_t));
    aShdrs[idx].sh_size      = cDynEntries * sizeof(Elf64_Dyn);
    aShdrs[idx].sh_link      = 2; /* .dynstr */
    aShdrs[idx].sh_info      = 0;
    aShdrs[idx].sh_addralign = sizeof(uint64_t);
    aShdrs[idx].sh_entsize   = sizeof(Elf64_Dyn);

    /* Build the program headers. */
    aPhdrs[0].p_type    = PT_PHDR;
    aPhdrs[0].p_flags   = PF_R;
    aPhdrs[0].p_offset  = Hdr.e_phoff;
    aPhdrs[0].p_vaddr   = Hdr.e_phoff;
    aPhdrs[0].p_paddr   = Hdr.e_phoff;
    aPhdrs[0].p_filesz  = sizeof(aPhdrs);
    aPhdrs[0].p_memsz   = sizeof(aPhdrs);
    aPhdrs[0].p_align   = sizeof(uint64_t);

    aPhdrs[1].p_type    = PT_LOAD;
    aPhdrs[1].p_flags   = PF_R | PF_X;
    aPhdrs[1].p_offset  = 0;
    aPhdrs[1].p_vaddr   = 0;
    aPhdrs[1].p_paddr   = 0;
    aPhdrs[1].p_filesz  = aShdrs[idx].sh_offset;
    aPhdrs[1].p_memsz   = aShdrs[idx].sh_offset;
    aPhdrs[1].p_align   = 0x10000;

    aPhdrs[2].p_type    = PT_LOAD;
    aPhdrs[2].p_flags   = PF_R | PF_W;
    aPhdrs[2].p_offset  = aShdrs[idx].sh_offset;
    aPhdrs[2].p_vaddr   = aShdrs[idx].sh_offset;
    aPhdrs[2].p_paddr   = aShdrs[idx].sh_offset;
    aPhdrs[2].p_filesz  = aShdrs[idx].sh_size;
    aPhdrs[2].p_memsz   = aShdrs[idx].sh_size;
    aPhdrs[2].p_align   = 0x10000;

    aPhdrs[3].p_type    = PT_DYNAMIC;
    aPhdrs[3].p_flags   = PF_R | PF_W;
    aPhdrs[3].p_offset  = aShdrs[idx].sh_offset;
    aPhdrs[3].p_vaddr   = aShdrs[idx].sh_offset;
    aPhdrs[3].p_paddr   = aShdrs[idx].sh_offset;
    aPhdrs[3].p_filesz  = aShdrs[idx].sh_size;
    aPhdrs[3].p_memsz   = aShdrs[idx].sh_size;
    aPhdrs[3].p_align   = sizeof(uint64_t);

    idx++;

    /* .shstrtab */
    aShdrs[idx].sh_name      = 1;
    aShdrs[idx].sh_type      = SHT_STRTAB;
    aShdrs[idx].sh_flags     = SHF_ALLOC;
    aShdrs[idx].sh_addr      = 0;
    aShdrs[idx].sh_offset    = aShdrs[idx - 1].sh_offset + aShdrs[idx - 1].sh_size;
    aShdrs[idx].sh_size      = sizeof(s_achShStrTab);
    aShdrs[idx].sh_link      = 0;
    aShdrs[idx].sh_info      = 0;
    aShdrs[idx].sh_addralign = sizeof(uint8_t);
    aShdrs[idx].sh_entsize   = 0;
    idx++;

    rc = RTFileWriteAt(hFileOut, 0, &Hdr, sizeof(Hdr), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write ELF header to '%s': %Rrc\n", pszStubPath, rc);

    rc = RTFileWriteAt(hFileOut, Hdr.e_phoff, &aPhdrs[0], sizeof(aPhdrs), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write section headers to '%s': %Rrc\n", pszStubPath, rc);

    rc = RTFileWriteAt(hFileOut, Hdr.e_shoff, &aShdrs[0], idx * sizeof(aShdrs[0]), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write section headers to '%s': %Rrc\n", pszStubPath, rc);

    idx = 1;
    rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, paDynSyms, cDynSymsExport * sizeof(*paDynSyms), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .dynsym section to '%s': %Rrc\n", pszStubPath, rc);

    rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, pachStrTab, cbStrTab, NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .dynstr section to '%s': %Rrc\n", pszStubPath, rc);

    if (pbGnuVerDef || pbGnuVerNeed)
    {
        rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, pu16GnuVerSym, cDynSymsExport * sizeof(uint16_t), NULL /*pcbWritten*/);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .gnu.version section to '%s': %Rrc\n", pszStubPath, rc);

        if (pbGnuVerDef)
        {
            rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, pbGnuVerDef, cbGnuVerDef, NULL /*pcbWritten*/);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .gnu.version section to '%s': %Rrc\n", pszStubPath, rc);
        }

        if (pbGnuVerNeed)
        {
            rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, pbGnuVerNeed, cbGnuVerNeed, NULL /*pcbWritten*/);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .gnu.version section to '%s': %Rrc\n", pszStubPath, rc);
        }
    }

    rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, paDynEnt, cDynEntries * sizeof(Elf64_Dyn), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .dynamic section to '%s': %Rrc\n", pszStubPath, rc);

    rc = RTFileWriteAt(hFileOut, aShdrs[idx++].sh_offset, s_achShStrTab, sizeof(s_achShStrTab), NULL /*pcbWritten*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write .gnu.version section to '%s': %Rrc\n", pszStubPath, rc);

    RTFileClose(hFileOut);
    return RTEXITCODE_SUCCESS;
}

#if 0
static RTEXITCODE elfEditStubLoadFromJson(PELFEDITSTUBIMG pStubImg, const char *pszInput)
{
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);

    RTJSONVAL hJsonRoot = NIL_RTJSONVAL;
    int rc = RTJsonParseFromFile(&hJsonRoot, RTJSON_PARSE_F_JSON5, pszInput, &ErrInfo.Core);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to load JSON stub '%s': %Rrc (%s)\n", pszInput, rc, ErrInfo.Core.pszMsg);

    char *pszTmp;
    rc = RTJsonValueQueryStringByName(hJsonRoot, "Target", &pszTmp);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'Target' from JSON stub '%s': %Rrc\n", pszInput, rc);

    if (!RTStrICmp(pszTmp, "arm64"))
        pStubImg->enmArch = RTLDRARCH_ARM64;
    else if (!RTStrICmp(pszTmp, "amd64"))
        pStubImg->enmArch = RTLDRARCH_AMD64;
    else if (!RTStrICmp(pszTmp, "x86"))
        pStubImg->enmArch = RTLDRARCH_X86_32;
    else
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "'%s' is not a valid target in JSON stub '%s'\n", pszTmp, pszInput);

    RTStrFree(pszTmp);

    rc = RTJsonValueQueryStringByName(hJsonRoot, "SoName", &pszTmp);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'SoName' from JSON stub '%s': %Rrc\n", pszInput, rc);

    pStubImg->pszName = elfEditImgAddString(pStubImg, pszTmp);
    if (!pStubImg->pszName)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add '%s' to string table\n", pszTmp);
    RTStrFree(pszTmp);

    RTJSONVAL hJsonNeeded = NIL_RTJSONVAL;
    rc = RTJsonValueQueryByName(hJsonRoot, "Needed", &hJsonNeeded);
    if (RT_FAILURE(rc) && rc != VERR_NOT_FOUND)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query 'Needed' from JSON stub '%s': %Rrc\n", pszInput, rc);

    if (RTJsonValueGetType(hJsonNeeded) != RTJSONVALTYPE_ARRAY)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "'Needed' in JSON stub '%s' is not an array\n", pszInput);

    RTJSONIT hJsonIt = NIL_RTJSONIT;
    rc = RTJsonIteratorBeginArray(hJsonNeeded, &hJsonIt);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to process 'Needed' array in JSON stub '%s': %Rrc\n", pszInput, rc);

    for (;;)
    {
        
        rc = RTJsonIteratorNext(hJsonIt);
        if (rc == VERR_JSON_ITERATOR_END)
            break;
    }

    RTJsonIteratorFree(hJsonIt);

    RTJsonValueRelease(hJsonRoot);
    return RTEXITCODE_SUCCESS;
}
#endif

static RTEXITCODE elfEditStubDumpToJson(PELFEDITSTUBIMG pStubImg, const char *pszOutput)
{
    PRTSTREAM pStrm = NULL;
    int rc = RTStrmOpen(pszOutput, "wt", &pStrm);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create file '%s': %Rrc\n", pszOutput, rc);

    rc = RTStrmPutStr(pStrm, "{\n");

    const char *pszTarget = NULL;
    switch (pStubImg->enmArch)
    {
        case RTLDRARCH_ARM64:  pszTarget = "arm64"; break;
        case RTLDRARCH_AMD64:  pszTarget = "amd64"; break;
        case RTLDRARCH_X86_32: pszTarget = "x86";   break;
        default: RTMsgErrorExit(RTEXITCODE_FAILURE,
                                "Unsupported architecture %u\n", pStubImg->enmArch);
    }

    rc |= RTStrmPrintf(pStrm, "    Target: '%s',\n", pszTarget);
    rc |= RTStrmPrintf(pStrm, "    SoName: '%s',\n", pStubImg->pszName);

    if (pStubImg->papszNeeded)
    {
        /* Find needed libraries. */
        rc |= RTStrmPutStr(pStrm, "    Needed:\n    [\n");
        for (uint32_t i = 0; i < pStubImg->cNeeded; i++)
            rc |= RTStrmPrintf(pStrm, "        '%s',\n", pStubImg->papszNeeded[i]);
        rc |= RTStrmPutStr(pStrm, "    ],\n");
    }

    /* Dump versions */
    if (pStubImg->paVersions)
    {
        rc |= RTStrmPutStr(pStrm, "    Versions:\n    [\n");
        for (uint32_t i = 0; i < pStubImg->cVersions; i++)
        {
            PCELFEDITSTUBVERSION pVersion = &pStubImg->paVersions[i];
            Assert(i == pVersion->idxVersion);

            if (!pVersion->pszVersion)
                rc |= RTStrmPutStr(pStrm, "        null,\n");
            else
            {
                rc |= RTStrmPrintf(pStrm, "        { Version: '%s'", pVersion->pszVersion);
                if (pVersion->pszParent)
                    rc |= RTStrmPrintf(pStrm, ", Parent: '%s'", pVersion->pszParent);
                rc |= RTStrmPutStr(pStrm, " },\n");
            }
        }
        rc |= RTStrmPutStr(pStrm, "    ],\n");
    }

    /* Dump symbols. */
    rc |= RTStrmPutStr(pStrm, "    Symbols:\n    [\n");

    for (uint32_t i = 0; i < pStubImg->cSyms; i++)
    {
        PELFEDITSTUBSYM pSym = &pStubImg->paSyms[i];
        const char *pszType = NULL;
        switch (pSym->enmType)
        {
            case kElfEditSymType_NoType:     pszType = "NoType";      break;
            case kElfEditSymType_Object:     pszType = "Object";      break;
            case kElfEditSymType_Function:   pszType = "Function";    break;
            case kElfEditSymType_Tls:        pszType = "Tls";         break;
            case kElfEditSymType_GnuIndFunc: pszType = "GnuIFunc";    break;
            default:                         pszType = "UNSUPPORTED"; break;
        }

        const char *pszVisibilty = NULL;
        switch (pSym->enmVisibility)
        {
            case kElfEditSymVisbility_Default:   pszVisibilty = "Default";     break;
            case kElfEditSymVisbility_Internal:  pszVisibilty = "Internal";    break;
            case kElfEditSymVisbility_Hidden:    pszVisibilty = "Hidden";      break;
            case kElfEditSymVisbility_Protected: pszVisibilty = "Protected";   break;
            default:                             pszVisibilty = "UNSUPPORTED"; break;
        }

        RTStrmPrintf(pStrm,
                     "        { Name: '%s', Type: '%s', Visibilty: '%s', Weak: %RTbool, Size: %RU64",
                     pSym->pszName, pszType, pszVisibilty, pSym->fWeak, pSym->cbSym);

        if (pSym->pVersion)
        {
            RTStrmPrintf(pStrm, ", Version: %RU16, Default: %RTbool",
                         pSym->pVersion->idxVersion, pSym->fDefaultVersion);
        }

        rc |= RTStrmPutStr(pStrm, " },\n");
    }
    rc |= RTStrmPutStr(pStrm, "    ],\n");

    rc |= RTStrmPutStr(pStrm, "}\n");
    rc |= RTStrmClose(pStrm);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to write file '%s': %Rrc\n", pszOutput, rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE elfEditCreateStub(const char *pszInput, bool fJsonInput, const char *pszOutput, bool fJsonOutput)
{
    ELFEDITSTUBIMG StubImg; RT_ZERO(StubImg);

    RTFILE hFileInput = NIL_RTFILE;
    int rc = RTFileOpen(&hFileInput, pszInput, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Filed to open file '%s': %Rrc\n", pszInput, rc);

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (!fJsonInput)
    {
        /*
         * Read the ident to decide if this is 32-bit or 64-bit
         * and worth dealing with.
         */
        uint8_t e_ident[EI_NIDENT];
        rc = RTFileReadAt(hFileInput, 0, &e_ident, sizeof(e_ident), NULL);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFileInput);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);
        }

        if (    e_ident[EI_MAG0] != ELFMAG0
            ||  e_ident[EI_MAG1] != ELFMAG1
            ||  e_ident[EI_MAG2] != ELFMAG2
            ||  e_ident[EI_MAG3] != ELFMAG3
            ||  (   e_ident[EI_CLASS] != ELFCLASS32
                 && e_ident[EI_CLASS] != ELFCLASS64)
           )
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  "%s: Unsupported/invalid ident %.*Rhxs", pszInput, sizeof(e_ident), e_ident);

        if (e_ident[EI_DATA] != ELFDATA2LSB)
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  "%s: ELF endian %x is unsupported", pszInput, e_ident[EI_DATA]);

        if (e_ident[EI_CLASS] == ELFCLASS32)
            rcExit = elfEditElf32Parse(&StubImg, hFileInput);
        else
            rcExit = elfEditElf64Parse(&StubImg, hFileInput);

        RTFileClose(hFileInput);
    }
    else
        AssertReleaseFailed();

    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    if (fJsonOutput)
        rcExit = elfEditStubDumpToJson(&StubImg, pszOutput);
    else
        AssertReleaseFailed();

    return rcExit;
}


/**
 * Display usage
 *
 * @returns success if stdout, syntax error if stderr.
 */
static RTEXITCODE usage(FILE *pOut, const char *argv0)
{
    fprintf(pOut,
            "usage: %s --input <input binary> [options and operations]\n"
            "\n"
            "Operations and Options (processed in place):\n"
            "  --verbose                                Noisier.\n"
            "  --quiet                                  Quiet execution.\n"
            "  --delete-runpath                         Deletes all DT_RUNPATH entries.\n"
            "  --change-runpath <new runpath>           Changes the first DT_RUNPATH entry to the new one.\n"
            "  --create-stub-library <path/to/stub>     Creates a stub library used for linking.\n"
            "  --create-stub-json <path/to/stub>.       Creates a stub JSON file which can be edited and turned into a linker stub.\n"
            , argv0);
    return pOut == stdout ? RTEXITCODE_SUCCESS : RTEXITCODE_SYNTAX;
}


/**
 * Parses the arguments.
 */
static RTEXITCODE parseArguments(int argc,  char **argv)
{
    /*
     * Option config.
     */
    static RTGETOPTDEF const s_aOpts[] =
    {
        { "--input",                            'i', RTGETOPT_REQ_STRING  },
        { "--verbose",                          'v', RTGETOPT_REQ_NOTHING },
        { "--delete-runpath",                   'd', RTGETOPT_REQ_NOTHING },
        { "--change-runpath",                   'c', RTGETOPT_REQ_STRING  },
        { "--create-stub-library",              's', RTGETOPT_REQ_STRING  },
        { "--create-stub-json",                 'j', RTGETOPT_REQ_STRING  },
    };

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetOptState;
    int rc = RTGetOptInit(&GetOptState, argc, argv, &s_aOpts[0], RT_ELEMENTS(s_aOpts), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertReleaseRCReturn(rc, RTEXITCODE_FAILURE);

    /*
     * Process the options.
     */
    while ((rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'h':
                return usage(stdout, argv[0]);

            case 'i':
                if (g_pszInput)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Input file is already set to '%s'", g_pszInput);
                g_pszInput = ValueUnion.psz;
                break;

            case 'v':
                g_cVerbosity++;
                break;

            case 'd':
                g_enmAction = kVBoxEditElfAction_DeleteRunpath;
                break;

            case 'c':
                g_enmAction = kVBoxEditElfAction_ChangeRunpath;
                g_pszRunpath = ValueUnion.psz;
                break;

            case 's':
                g_enmAction = kVBoxEditElfAction_CreateLinkerStub;
                g_pszLinkerStub = ValueUnion.psz;
                break;

            case 'j':
                g_enmAction = kVBoxEditElfAction_CreateJsonStub;
                g_pszLinkerStub = ValueUnion.psz;
                break;

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision$";
                const char *psz = RTStrStripL(strchr(s_szRev, ' '));
                RTPrintf("r%.*s\n", strchr(psz, ' ') - psz, psz);
                return RTEXITCODE_SUCCESS;
            }

            /*
             * Errors and bugs.
             */
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Check that we've got all we need.
     */
    if (g_enmAction == kVBoxEditElfAction_Nothing)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No action specified (--delete-runpath, --change-runpath or --create-stub-library)");
    if (!g_pszInput)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No input file specified (--input)");

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return 1;

    RTEXITCODE rcExit = parseArguments(argc, argv);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Take action.
         */
        if (g_enmAction == kVBoxEditElfAction_DeleteRunpath)
            rcExit = deleteRunpath(g_pszInput);
        else if (g_enmAction == kVBoxEditElfAction_ChangeRunpath)
            rcExit = changeRunpath(g_pszInput, g_pszRunpath);
        else if (g_enmAction == kVBoxEditElfAction_CreateLinkerStub)
            rcExit = createLinkerStubFrom(g_pszInput, g_pszLinkerStub);
        else if (g_enmAction == kVBoxEditElfAction_CreateJsonStub)
            rcExit = elfEditCreateStub(g_pszInput, false, g_pszLinkerStub, true);
    }

    return rcExit;
}

