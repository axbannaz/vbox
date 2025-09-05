/* $Id$ */
/** @file
 * VBoxEditElf - Simple ELF binary file editor, templated code.
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


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if ELF_MODE == 32
# define ELFEDIT_NAME(name)    elfEditElf32##name
# define ELFEDIT_SUFF(name)    name##32
# define ELFEDIT_MID(pre,suff) pre##32##suff
# define FMT_ELF_ADDR           "%08RX32"
# define FMT_ELF_ADDR7          "%07RX32"
# define FMT_ELF_HALF           "%04RX16"
# define FMT_ELF_OFF            "%08RX32"
# define FMT_ELF_SIZE           "%08RX32"
# define FMT_ELF_SWORD          "%RI32"
# define FMT_ELF_WORD           "%08RX32"
# define FMT_ELF_XWORD          "%08RX32"
# define FMT_ELF_SXWORD         "%RI32"
# define Elf_Xword              Elf32_Word
# define Elf_Sxword             Elf32_Sword

#elif ELF_MODE == 64
# define ELFEDIT_NAME(name)    elfEditElf64##name
# define ELFEDIT_SUFF(name)    name##64
# define ELFEDIT_MID(pre,suff) pre##64##suff
# define FMT_ELF_ADDR           "%016RX64"
# define FMT_ELF_ADDR7          "%08RX64"
# define FMT_ELF_HALF           "%04RX16"
# define FMT_ELF_SHALF          "%RI16"
# define FMT_ELF_OFF            "%016RX64"
# define FMT_ELF_SIZE           "%016RX64"
# define FMT_ELF_SWORD          "%RI32"
# define FMT_ELF_WORD           "%08RX32"
# define FMT_ELF_XWORD          "%016RX64"
# define FMT_ELF_SXWORD         "%RI64"
# define Elf_Xword              Elf64_Xword
# define Elf_Sxword             Elf64_Sxword
#endif

#define Elf_Ehdr            ELFEDIT_MID(Elf,_Ehdr)
#define Elf_Phdr            ELFEDIT_MID(Elf,_Phdr)
#define Elf_Shdr            ELFEDIT_MID(Elf,_Shdr)
#define Elf_Sym             ELFEDIT_MID(Elf,_Sym)
#define Elf_Rel             ELFEDIT_MID(Elf,_Rel)
#define Elf_Rela            ELFEDIT_MID(Elf,_Rela)
#define Elf_Nhdr            ELFEDIT_MID(Elf,_Nhdr)
#define Elf_Dyn             ELFEDIT_MID(Elf,_Dyn)
#define Elf_Addr            ELFEDIT_MID(Elf,_Addr)
#define Elf_Half            ELFEDIT_MID(Elf,_Half)
#define Elf_Off             ELFEDIT_MID(Elf,_Off)
#define Elf_Size            ELFEDIT_MID(Elf,_Size)
#define Elf_Sword           ELFEDIT_MID(Elf,_Sword)
#define Elf_Word            ELFEDIT_MID(Elf,_Word)

#define ELF_R_SYM(info)     ELFEDIT_MID(ELF,_R_SYM)(info)
#define ELF_R_TYPE(info)    ELFEDIT_MID(ELF,_R_TYPE)(info)
#define ELF_R_INFO(sym, type) ELFEDIT_MID(ELF,_R_INFO)(sym, type)

#define ELF_ST_BIND(info)   ELFEDIT_MID(ELF,_ST_BIND)(info)
#define ELF_ST_TYPE(info)   ELFEDIT_MID(ELF,_ST_TYPE)(info)
#define ELF_ST_VISIBILITY(info)   ELFEDIT_MID(ELF,_ST_VISIBILITY)(info)


static RTEXITCODE ELFEDIT_NAME(DeleteRunpath)(RTFILE hFileElf, const char *pszInput)
{
    Elf_Ehdr Hdr;
    int rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);

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

    if (sizeof(Elf_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf_Ehdr));
    if (    sizeof(Elf_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf_Phdr));
    if (sizeof(Elf_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf_Shdr));

    /* Find dynamic section. */
    Elf_Phdr Phdr; RT_ZERO(Phdr);
    bool fFound = false;
    for (uint32_t i = 0; i < Hdr.e_phnum; i++)
    {
        rc = RTFileReadAt(hFileElf, Hdr.e_phoff + i * sizeof(Phdr), &Phdr, sizeof(Phdr), NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);

        if (Phdr.p_type == PT_DYNAMIC)
        {
            if (!Phdr.p_filesz)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Dynmic section in '%s' is empty\n", pszInput);

            fFound = true;
            break;
        }
    }

    if (!fFound)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF binary '%s' doesn't contain dynamic section\n", pszInput);

    Elf_Dyn *paDynSh = (Elf_Dyn *)RTMemAllocZ(Phdr.p_filesz);
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
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE ELFEDIT_NAME(ChangeRunpathEntry)(RTFILE hFileElf, const char *pszInput, Elf_Ehdr *pHdr, Elf_Xword offInStrTab, const char *pszRunpath)
{
    /* Read section headers to find the string table. */
    size_t const cbShdrs = pHdr->e_shnum * sizeof(Elf_Shdr);
    Elf_Shdr *paShdrs = (Elf_Shdr *)RTMemAlloc(cbShdrs);
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


static RTEXITCODE ELFEDIT_NAME(ChangeRunpath)(RTFILE hFileElf, const char *pszInput, const char *pszRunpath)
{
    /* Only support for 64-bit ELF files currently. */
    Elf_Ehdr Hdr;
    int rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header from '%s': %Rrc\n", pszInput, rc);

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

    if (sizeof(Elf_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf_Ehdr));
    if (    sizeof(Elf_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf_Phdr));
    if (sizeof(Elf_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf_Shdr));

    /* Find dynamic section. */
    Elf_Phdr Phdr; RT_ZERO(Phdr);
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

    Elf_Dyn *paDynSh = (Elf_Dyn *)RTMemAllocZ(Phdr.p_filesz);
    if (!paDynSh)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes of memory for dynamic section of '%s'\n", Phdr.p_filesz, pszInput);

    rc = RTFileReadAt(hFileElf, Phdr.p_offset, paDynSh, Phdr.p_filesz, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF program header header from '%s': %Rrc\n", pszInput, rc);

    /* Look for the first DT_RUNPATH entry and rewrite it. */
    for (uint32_t i = 0; i < Phdr.p_filesz / sizeof(Elf_Dyn); i++)
    {
        if (   paDynSh[i].d_tag == DT_RPATH
            || paDynSh[i].d_tag == DT_RUNPATH)
        {
            RTEXITCODE rcExit = ELFEDIT_NAME(ChangeRunpathEntry)(hFileElf, pszInput, &Hdr, paDynSh[i].d_un.d_val, pszRunpath);
            RTMemFree(paDynSh);
            return rcExit;
        }
    }

    RTMemFree(paDynSh);
    return RTMsgErrorExit(RTEXITCODE_FAILURE, "No DT_RPATH or DT_RUNPATH entry found in '%s'\n", pszInput);
}


static RTEXITCODE ELFEDIT_NAME(Parse)(PELFEDITSTUBIMG pStubImg, RTFILE hFileElf)
{
    Elf_Ehdr Hdr;
    int rc = RTFileReadAt(hFileElf, 0, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
    {
        RTFileClose(hFileElf);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read ELF header: %Rrc\n", rc);
    }

    if (    Hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Hdr.e_ident[EI_MAG3] != ELFMAG3)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF magic (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_CLASS] != ELFEDIT_SUFF(ELFCLASS))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid ELF class (%.*Rhxs)", sizeof(Hdr.e_ident), Hdr.e_ident);
    if (Hdr.e_ident[EI_DATA] != ELFDATA2LSB)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF endian %x is unsupported", Hdr.e_ident[EI_DATA]);
    if (Hdr.e_version != EV_CURRENT)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ELF version %x is unsupported", Hdr.e_version);

    if (sizeof(Elf_Ehdr) != Hdr.e_ehsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_ehsize is %d expected %d!", Hdr.e_ehsize, sizeof(Elf_Ehdr));
    if (    sizeof(Elf_Phdr) != Hdr.e_phentsize
        &&  (   Hdr.e_phnum != 0
             || Hdr.e_type == ET_DYN
             || Hdr.e_type == ET_EXEC))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_phentsize is %d expected %d!", Hdr.e_phentsize, sizeof(Elf_Phdr));
    if (sizeof(Elf_Shdr) != Hdr.e_shentsize)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Elf header e_shentsize is %d expected %d!", Hdr.e_shentsize, sizeof(Elf_Shdr));

    switch (Hdr.e_machine)
    {
        case EM_AARCH64: pStubImg->enmArch = RTLDRARCH_ARM64; break;
        case EM_X86_64:  pStubImg->enmArch = RTLDRARCH_AMD64; break;
        case EM_386:
        case EM_486:     pStubImg->enmArch = RTLDRARCH_X86_32; break;
        default:
            RTMsgErrorExit(RTEXITCODE_FAILURE, "Machine type %#x is not supported!", Hdr.e_machine);
    }

    /* Find all the dynamic sections we need. */
    uint32_t idStrTab = UINT32_MAX;
    uint32_t idDynamic = UINT32_MAX;
    Elf_Shdr ShdrDyn; RT_ZERO(ShdrDyn);
    char *pachStrTab = NULL; size_t cbStrTab = 0;
    Elf_Sym *paDynSyms = NULL; size_t cbDynSyms = 0; uint32_t u32DynSymInfo = 0;
    uint16_t *pu16GnuVerSym = NULL; size_t cbGnuVerSym = 0;
    uint8_t *pbGnuVerDef = NULL; size_t cbGnuVerDef = 0; uint32_t cVerdefEntries = 0;
    uint8_t *pbGnuVerNeed = NULL; size_t cbGnuVerNeed = 0; uint32_t cVerNeedEntries = 0;

    for (uint32_t i = 0; i < Hdr.e_shnum; i++)
    {
        Elf_Shdr Shdr; RT_ZERO(Shdr);
        rc = RTFileReadAt(hFileElf, Hdr.e_shoff + i * sizeof(Shdr), &Shdr, sizeof(Shdr), NULL /*pcbRead*/);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read section header at %RX64\n", Hdr.e_shoff + i * sizeof(Shdr));

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
                paDynSyms = (Elf_Sym *)RTMemAllocZ(cbDynSyms);
                if (!paDynSyms)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .dynsym section\n", cbDynSyms);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, paDynSyms, cbDynSyms, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64\n", Shdr.sh_offset);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u\n", Shdr.sh_link, idStrTab);
                break;
            }
            case SHT_GNU_versym:
            {
                cbGnuVerSym = Shdr.sh_size;
                pu16GnuVerSym = (uint16_t *)RTMemAllocZ(cbGnuVerSym);
                if (!pu16GnuVerSym)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version section\n", cbGnuVerSym);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pu16GnuVerSym, cbGnuVerSym, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64\n", Shdr.sh_offset);

                /** @todo It should link to the .dynsym table. */
                break;
            }
            case SHT_GNU_verdef:
            {
                cbGnuVerDef = Shdr.sh_size;
                pbGnuVerDef = (uint8_t *)RTMemAllocZ(cbGnuVerDef);
                cVerdefEntries = Shdr.sh_info;
                if (!pbGnuVerDef)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version_d section\n", cbGnuVerDef);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pbGnuVerDef, cbGnuVerDef, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynsym section at %RX64\n", Shdr.sh_offset);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u\n", Shdr.sh_link, idStrTab);
                break;
            }
            case SHT_GNU_verneed:
            {
                cbGnuVerNeed = Shdr.sh_size;
                pbGnuVerNeed = (uint8_t *)RTMemAllocZ(cbGnuVerNeed);
                cVerNeedEntries = Shdr.sh_info;
                if (!pbGnuVerNeed)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .gnu.version_r section\n", cbGnuVerNeed);

                rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pbGnuVerNeed, cbGnuVerNeed, NULL /*pcbRead*/);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .gnu.version_r section at %RX64\n", Shdr.sh_offset);

                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u\n", Shdr.sh_link, idStrTab);
                break;
            }
            case SHT_DYNAMIC:
            {
                if (idDynamic != UINT32_MAX)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "More than one .dynamic section\n");

                idDynamic = i;
                ShdrDyn = Shdr;
                /* It should link to the string table. */
                if (idStrTab == UINT32_MAX)
                    idStrTab = Shdr.sh_link;
                else if (idStrTab != Shdr.sh_link)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index %u doesn't match %u\n", Shdr.sh_link, idStrTab);
            }
            default:
                break; /* Ignored. */
        }
    }

    if (idStrTab == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "String table index not found\n");
    if (idDynamic == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, ".dynamic section index not found\n");

    if (pbGnuVerDef && !pu16GnuVerSym)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Versioned library misses .gnu.version\n");

    /* Read the string table section header. */
    Elf_Shdr Shdr; RT_ZERO(Shdr);
    rc = RTFileReadAt(hFileElf, Hdr.e_shoff + idStrTab * sizeof(Shdr), &Shdr, sizeof(Shdr), NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .strtab section header at %RX64: %Rrc\n",
                              Hdr.e_shoff + idStrTab * sizeof(Shdr), rc);

    cbStrTab = Shdr.sh_size;
    pachStrTab = (char *)RTMemAllocZ(cbStrTab + 1); /* Force termination */
    if (!pachStrTab)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .strtab section\n", cbStrTab);

    rc = RTFileReadAt(hFileElf, Shdr.sh_offset, pachStrTab, cbStrTab, NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .strtab section at %RX64\n", Shdr.sh_offset);

    /* Read the .dynamic section. */
    Elf_Dyn *paDynEnt = (Elf_Dyn *)RTMemAllocZ(ShdrDyn.sh_size);
    if (!paDynEnt)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %zu bytes for .dynamic section\n", ShdrDyn.sh_size);

    rc = RTFileReadAt(hFileElf, ShdrDyn.sh_offset, paDynEnt, ShdrDyn.sh_size, NULL /*pcbRead*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to read .dynamic section at %RX64 from\n", ShdrDyn.sh_offset);

    size_t cVerDefNum = 0;

    /* Parse the dynamic section first to get some basics sorted. */
    for (uint32_t i = 0; i < ShdrDyn.sh_size / sizeof(*paDynEnt); i++)
    {
        switch (paDynEnt[i].d_tag)
        {
            case DT_SONAME:
            {
                pStubImg->pszName = elfEditImgAddString(pStubImg, &pachStrTab[paDynEnt[i].d_un.d_val]);
                if (!pStubImg->pszName)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                          "Failed to add '%s' to string table\n",
                                          &pachStrTab[paDynEnt[i].d_un.d_val]);

                break;
            }
            case DT_NEEDED:
            {
                const char *pszNeeded = elfEditImgAddString(pStubImg, &pachStrTab[paDynEnt[i].d_un.d_val]);
                if (!pszNeeded)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                          "Failed to add '%s' to string table\n",
                                          &pachStrTab[paDynEnt[i].d_un.d_val]);

                if (pStubImg->cNeeded == pStubImg->cNeededMax)
                {
                    const char **papszNeededNew = (const char **)RTMemRealloc(pStubImg->papszNeeded,
                                                                              (pStubImg->cNeededMax + 10) * sizeof(const char *));
                    if (!papszNeededNew)
                        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                              "Failed to allocate more entries for the needed array\n",
                                              &pachStrTab[paDynEnt[i].d_un.d_val]);
                    pStubImg->papszNeeded = papszNeededNew;
                    pStubImg->cNeededMax += 10;
                }

                pStubImg->papszNeeded[pStubImg->cNeeded] = pszNeeded;
                pStubImg->cNeeded++;
                break;
            }
            case DT_VERDEFNUM:
                cVerDefNum = paDynEnt[i].d_un.d_val;
                break;
            default:
                break;
        }
    }

    if (cVerDefNum)
    {
        pStubImg->paVersions = (PELFEDITSTUBVERSION)RTMemAllocZ((cVerDefNum + 1) * sizeof(*pStubImg->paVersions));
        if (!pStubImg->paVersions)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %u version entries\n", cVerDefNum);

        pStubImg->cVersions = cVerDefNum + 1;

        /* Process the verdef entries. */
        Elf_Verdef *pCur = (Elf_Verdef *)pbGnuVerDef;
        for (uint32_t i = 0; i < cVerDefNum; i++)
        {
            verbose("Verdef entry %u:\n"
                    "    vd_version:   %RU16\n"
                    "    vd_flags:     %RX16\n"
                    "    vd_ndx:       %RX16\n"
                    "    vd_cnt:       %RX16\n"
                    "    vd_hash:      %RX32\n"
                    "    vd_aux:       %RU32\n"
                    "    vd_next:      %RU32\n", i,
                    pCur->vd_version, pCur->vd_flags, pCur->vd_ndx,
                    pCur->vd_cnt, pCur->vd_hash, pCur->vd_aux, pCur->vd_next);

            if (pCur->vd_cnt > 2)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Image has more than 2 auxiliary verdef entries (%u)\n", pCur->vd_cnt);

            pStubImg->paVersions[pCur->vd_ndx].idxVersion = pCur->vd_ndx;
            if (pCur->vd_aux != 0)
            {
                uint8_t idxVerdaux = 0;
                Elf_Verdaux *pCurVerdaux = (Elf_Verdaux *)((uint8_t *)pCur + pCur->vd_aux);
                for (;;)
                {
                    const char *pszVersion = elfEditImgAddString(pStubImg, &pachStrTab[pCurVerdaux->vda_name]);
                    if (!pszVersion)
                        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                              "Failed to add '%s' to string table\n",
                                              &pachStrTab[pCurVerdaux->vda_name]);

                    verbose("Verdaux entry %u:\n"
                            "    vda_name:     %RU32\n"
                            "    vda_next:     %RU32\n"
                            "        String:   %s (%RX32)\n", idxVerdaux,
                            pCurVerdaux->vda_name, pCurVerdaux->vda_next,
                            pszVersion, elfHash(pszVersion));

                    if (idxVerdaux == 0)
                        pStubImg->paVersions[pCur->vd_ndx].pszVersion = pszVersion;
                    else if (idxVerdaux == 1)
                        pStubImg->paVersions[pCur->vd_ndx].pszParent = pszVersion;

                    if (pCurVerdaux->vda_next == 0)
                        break;

                    idxVerdaux++;
                    pCurVerdaux = (Elf_Verdaux *)((uint8_t *)pCurVerdaux + pCurVerdaux->vda_next);
                }
            }

            if (pCur->vd_next == 0)
                break;

            pCur = (Elf_Verdef *)((uint8_t *)pCur + pCur->vd_next);
        }
    }

    /* Process exported symbols. */
    size_t const cSyms = cbDynSyms / sizeof(*paDynSyms);

    pStubImg->paSyms = (PELFEDITSTUBSYM)RTMemAllocZ(cSyms * sizeof(*pStubImg->paSyms));
    if (!pStubImg->paSyms)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate %u entries for the symbol table\n", cSyms);

    uint32_t idxSym = 0;
    for (size_t i = 0; i < cSyms; i++)
    {
        PELFEDITSTUBSYM pSym = &pStubImg->paSyms[idxSym];

        uint8_t iStBind = ELF_ST_BIND(paDynSyms[i].st_info);
        uint8_t iStType = ELF_ST_TYPE(paDynSyms[i].st_info);
        uint8_t iVisibility = ELF_ST_VISIBILITY(paDynSyms[i].st_other);

        /* Ignore imports and local stuff. */
        if (iStBind == STB_LOCAL || paDynSyms[i].st_shndx == STN_UNDEF)
            continue;

        switch (iStType)
        {
            case STT_NOTYPE:    pSym->enmType = kElfEditSymType_NoType;     break;
            case STT_OBJECT:    pSym->enmType = kElfEditSymType_Object;     break;
            case STT_FUNC:      pSym->enmType = kElfEditSymType_Function;   break;
            case STT_TLS:       pSym->enmType = kElfEditSymType_Tls;        break;
            case STT_GNU_IFUNC: pSym->enmType = kElfEditSymType_GnuIndFunc; break;
            default:            RTMsgErrorExit(RTEXITCODE_FAILURE, "Unsupported symbol type %u\n", iStType);
        }

        switch (iVisibility)
        {
            case STV_DEFAULT:   pSym->enmVisibility = kElfEditSymVisbility_Default;   break;
            case STV_INTERNAL:  pSym->enmVisibility = kElfEditSymVisbility_Internal;  break;
            case STV_HIDDEN:    pSym->enmVisibility = kElfEditSymVisbility_Hidden;    break;
            case STV_PROTECTED: pSym->enmVisibility = kElfEditSymVisbility_Protected; break;
            default:            RTMsgErrorExit(RTEXITCODE_FAILURE, "Unsupported symbol visbility %u\n", iVisibility);
        }

        pSym->fWeak   = iStBind == STB_WEAK ? true : false;
        pSym->cbSym   = paDynSyms[i].st_size;
        pSym->pszName = elfEditImgAddString(pStubImg, &pachStrTab[paDynSyms[i].st_name]);
        if (!pSym->pszName)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add '%s' to string table\n",
                                  &pachStrTab[paDynSyms[i].st_name]);

        if (pu16GnuVerSym && pu16GnuVerSym[i] > 1)
        {
            uint16_t const idxVersion = pu16GnuVerSym[i] & ~GNU_VERSYM_HIDDEN;
            pSym->fDefaultVersion = !RT_BOOL(pu16GnuVerSym[i] & GNU_VERSYM_HIDDEN);
            pSym->pVersion        = &pStubImg->paVersions[idxVersion];
        }

        idxSym++;
    }

    pStubImg->cSyms = idxSym;
    return RTEXITCODE_SUCCESS;
}

#undef ELFEDIT_NAME
#undef ELFEDIT_SUFF
#undef ELFEDIT_MID

#undef FMT_ELF_ADDR
#undef FMT_ELF_ADDR7
#undef FMT_ELF_HALF
#undef FMT_ELF_SHALF
#undef FMT_ELF_OFF
#undef FMT_ELF_SIZE
#undef FMT_ELF_SWORD
#undef FMT_ELF_WORD
#undef FMT_ELF_XWORD
#undef FMT_ELF_SXWORD

#undef Elf_Ehdr
#undef Elf_Phdr
#undef Elf_Shdr
#undef Elf_Sym
#undef Elf_Rel
#undef Elf_Rela
#undef Elf_Reloc
#undef Elf_Nhdr
#undef Elf_Dyn

#undef Elf_Addr
#undef Elf_Half
#undef Elf_Off
#undef Elf_Size
#undef Elf_Sword
#undef Elf_Word
#undef Elf_Xword
#undef Elf_Sxword

#undef ELF_R_SYM
#undef ELF_R_TYPE
#undef ELF_R_INFO

#undef ELF_ST_BIND
#undef ELF_ST_TYPE
#undef ELF_ST_VISIBILITY

