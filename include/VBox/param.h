/** @file
 * VirtualBox Parameter Definitions. (VMM,+)
 *
 * param.mac is generated from this file by running 'kmk incs' in the root.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_param_h
#define VBOX_INCLUDED_param_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/param.h>
#include <iprt/cdefs.h>
#ifdef VMM_HOST_PAGE_SIZE_DYNAMIC
# include <iprt/system.h>
#endif


/** @defgroup   grp_vbox_param  VBox Parameter Definition
 * @{
 */

/** @def GUEST_MIN_PAGE_SIZE
 * Minimum guest page size.   */
#define GUEST_MIN_PAGE_SIZE         0x1000
/** @def GUEST_MIN_PAGE_OFFSET_MASK
 * Minimum guest page size.
 * @note If one-complementing this, always put a typecast after the operator! */
#define GUEST_MIN_PAGE_OFFSET_MASK  0xfff
/** @def GUEST_MIN_PAGE_SHIFT
 * Minimum guest page size.   */
#define GUEST_MIN_PAGE_SHIFT        12

/** @def GUEST_MAX_PAGE_SIZE
 * Maximum guest page size.   */
#ifdef VBOX_VMM_TARGET_ARMV8
# define GUEST_MAX_PAGE_SIZE        0x10000
#elif defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define GUEST_MAX_PAGE_SIZE        0x1000
#endif
/** @def GUEST_MAX_PAGE_OFFSET_MASK
 * Maximum guest page size.
 * @note If one-complementing this, always put a typecast after the operator! */
#ifdef VBOX_VMM_TARGET_ARMV8
# define GUEST_MAX_PAGE_OFFSET_MASK 0xffff
#elif defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define GUEST_MAX_PAGE_OFFSET_MASK 0xfff
#endif
/** @def GUEST_MAX_PAGE_SHIFT
 * Maximum guest page size.   */
#ifdef VBOX_VMM_TARGET_ARMV8
# define GUEST_MAX_PAGE_SHIFT       16
#elif defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define GUEST_MAX_PAGE_SHIFT       12
#endif

/** The guest page size (x86). */
#define GUEST_PAGE_SIZE             0x1000
/** The guest page offset mask (x86).
 * @note If one-complementing this, always put a typecast after the operator! */
#define GUEST_PAGE_OFFSET_MASK      0xfff
/** The guest page shift (x86). */
#define GUEST_PAGE_SHIFT            12


/** Host page size. */
#define HOST_PAGE_SIZE              PAGE_SIZE
/** Host page offset mask.
 * @note If one-complementing this, always put a typecast after the operator! */
#define HOST_PAGE_OFFSET_MASK       PAGE_OFFSET_MASK
/** Host page shift. */
#define HOST_PAGE_SHIFT             PAGE_SHIFT


/** The host page size which can be dynamic on certain hosts, linux.arm64 for instance */
#ifdef VMM_HOST_PAGE_SIZE_DYNAMIC
# define HOST_PAGE_SIZE_DYNAMIC     RTSystemGetPageSize()
#else
# define HOST_PAGE_SIZE_DYNAMIC     HOST_PAGE_SIZE
#endif
/** The host page shift which can be dynamic on certain hosts, linux.arm64 for instance */
#ifdef VMM_HOST_PAGE_SIZE_DYNAMIC
# define HOST_PAGE_SHIFT_DYNAMIC     RTSystemGetPageShift()
#else
# define HOST_PAGE_SHIFT_DYNAMIC     HOST_PAGE_SHIFT
#endif


/** The maximum memory size that can be allocated and mapped
 * by various MM, PGM and SUP APIs. */
#define VBOX_MAX_ALLOC_SIZE          _512M


/** The maximum number of pages that can be allocated and mapped
 * by various MM, PGM and SUP APIs. */
#if ARCH_BITS == 64
# define VBOX_MAX_ALLOC_PAGE_COUNT   (VBOX_MAX_ALLOC_SIZE / PAGE_SIZE)
#else
# define VBOX_MAX_ALLOC_PAGE_COUNT   (_256M / PAGE_SIZE) /** @todo r=aeichner Remove? */
#endif

/** @def VBOX_WITH_PAGE_SHARING
 * Enables the page sharing code on the host side (do not use in guest code).
 * @remarks Currently we only support page fusion on mainline AMD64 hosts,
 *          except Mac OS X (no ring-0). */
#if (   HC_ARCH_BITS == 64          /* ASM-NOINC */ \
     && defined(RT_ARCH_AMD64)      /* ASM-NOINC */ \
     && (defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)) ) /* ASM-NOINC */ \
     && (defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)) /* quick hack */ \
 || defined(DOXYGEN_RUNNING)        /* ASM-NOINC */
# define VBOX_WITH_PAGE_SHARING     /* ASM-NOINC */
#endif                              /* ASM-NOINC */


/** @defgroup   grp_vbox_param_mm  Memory Monitor Parameters
 * @{
 */
/** Initial address of Hypervisor Memory Area.
 * MUST BE PAGE TABLE ALIGNED! */
#define MM_HYPER_AREA_ADDRESS       UINT32_C(0xa0000000)

/** The max size of the hypervisor memory area. */
#define MM_HYPER_AREA_MAX_SIZE      (40U * _1M) /**< @todo Readjust when floating RAMRANGEs have been implemented. Used to be 20 * _1MB */

/** Maximum number of bytes we can dynamically map into the hypervisor region.
 * This must be a power of 2 number of pages!
 */
#define MM_HYPER_DYNAMIC_SIZE       (16U * PAGE_SIZE)

/** The minimum guest RAM size in bytes. */
#define MM_RAM_MIN                  UINT32_C(0x00400000)
/** The maximum guest RAM size in bytes. */
#if HC_ARCH_BITS == 64
# define MM_RAM_MAX                 UINT64_C(0x20000000000)
#else
# define MM_RAM_MAX                 UINT64_C(0x000E0000000)
#endif
/** The minimum guest RAM size in MBs. */
#define MM_RAM_MIN_IN_MB            UINT32_C(4)
/** The maximum guest RAM size in MBs. */
#if HC_ARCH_BITS == 64
# define MM_RAM_MAX_IN_MB           UINT32_C(2097152)
#else
# define MM_RAM_MAX_IN_MB           UINT32_C(3584)
#endif
/** The default size of the below 4GB RAM hole. */
#define MM_RAM_HOLE_SIZE_DEFAULT    (512U * _1M)
/** The maximum 64-bit MMIO BAR size.
 * @remarks There isn't really any limit here other than the size of the
 *          tracking structures we need (around 1/256 of the size). */
#if HC_ARCH_BITS == 64
# define MM_MMIO_64_MAX             _1T
#else
# define MM_MMIO_64_MAX             (_1G64 * 16)
#endif
/** The maximum 32-bit MMIO BAR size. */
#define MM_MMIO_32_MAX              _2G

/** @} */

/** @defgroup   grp_vbox_param_pdm  Pluggable Device Manager Parameters
 * @{
 */
/** Max number of network shaper groups. */
#define PDM_NET_SHAPER_MAX_GROUPS   32
/** Max length of a network shaper group name (excluding terminator). */
#define PDM_NET_SHAPER_MAX_NAME_LEN 63
/** @} */


/** @defgroup   grp_vbox_param_pgm  Page Manager Parameters
 * @{
 */
/** The number of handy pages.
 * This should be a power of two. */
#define PGM_HANDY_PAGES             128
/** The threshold at which allocation of more handy pages is flagged. */
#define PGM_HANDY_PAGES_SET_FF      32
/** The threshold at which we will allocate more when in ring-3.
 * This is must be smaller than both PGM_HANDY_PAGES_SET_FF and
 * PGM_HANDY_PAGES_MIN. */
#define PGM_HANDY_PAGES_R3_ALLOC    8
/** The threshold at which we will allocate more when in ring-0 or raw mode.
 * The idea is that we should never go below this threshold while in ring-0 or
 * raw mode because of PGM_HANDY_PAGES_RZ_TO_R3. However, should this happen and
 * we are actually out of memory, we will have 8 page to get out of whatever
 * code we're executing.
 *
 * This is must be smaller than both PGM_HANDY_PAGES_SET_FF and
 * PGM_HANDY_PAGES_MIN. */
#define PGM_HANDY_PAGES_RZ_ALLOC    8
/** The threshold at which we force return to R3 ASAP.
 * The idea is that this should be large enough to get out of any code and up to
 * the main EM loop when we are out of memory.
 * This must be less or equal to PGM_HANDY_PAGES_MIN. */
#define PGM_HANDY_PAGES_RZ_TO_R3    24
/** The minimum number of handy pages (after allocation).
 * This must be greater or equal to PGM_HANDY_PAGES_SET_FF.
 * Another name would be PGM_HANDY_PAGES_EXTRA_RESERVATION or _PARANOIA. :-) */
#define PGM_HANDY_PAGES_MIN         32
/** @} */


/** @defgroup   grp_vbox_param_vmm  VMM Parameters
 * @{
 */
/** VMM stack size. */
#ifdef RT_OS_DARWIN
# define VMM_STACK_SIZE             16384U
#else
# define VMM_STACK_SIZE             8192U
#endif
/** Min number of Virtual CPUs. */
#define VMM_MIN_CPU_COUNT           1
/** Max number of Virtual CPUs. */
#define VMM_MAX_CPU_COUNT           64

/** @} */


/** @defgroup   grp_vbox_pci        PCI Identifiers
 * @{ */
/** VirtualBox PCI vendor ID. */
#define VBOX_PCI_VENDORID           (0x80ee)

/** @name VirtualBox graphics card identifiers
 * @{ */
#define VBOX_VENDORID               VBOX_PCI_VENDORID   /**< @todo wonderful choice of name! Please squeeze a _VGA_ or something in there, please. */
#define VBOX_DEVICEID               (0xbeef)            /**< @todo ditto. */
#define VBOX_VESA_VENDORID          VBOX_PCI_VENDORID
#define VBOX_VESA_DEVICEID          (0xbeef)
/** @} */

/** @name VMMDev PCI card identifiers
 * @{ */
#define VMMDEV_VENDORID             VBOX_PCI_VENDORID
#define VMMDEV_DEVICEID             (0xcafe)
/** @} */

/** @} */


/** @defgroup grp_vbox_param_vga   VGA
 * @{ */
/** The default amount of VGA VRAM (in bytes). */
#define VGA_VRAM_DEFAULT            (_4M)
/** The minimum amount of VGA VRAM (in bytes). */
#define VGA_VRAM_MIN                (_1M)
/** The maximum amount of VGA VRAM (in bytes).  */
#define VGA_VRAM_MAX                (_1G)

/** The minimum amount of SVGA VRAM (in bytes).   */
#define VBOX_SVGA_VRAM_MIN_SIZE     (4U * 640U * 480U)
/** The maximum amount of SVGA VRAM (in bytes) when 3D acceleration is enabled. */
#define VBOX_SVGA_VRAM_MIN_SIZE_3D  _16M
/** The maximum amount of SVGA VRAM (in bytes). */
#define VBOX_SVGA_VRAM_MAX_SIZE     _128M
/** @} */


/** @defgroup grp_vbox_param_misc  Misc
 * @{ */

/** The maximum size of a generic segment offload (GSO) frame.  This limit is
 *  imposed by the 16-bit frame size in internal networking header. */
#define VBOX_MAX_GSO_SIZE           0xfff0

/** @} */


/** @} */

#endif /* !VBOX_INCLUDED_param_h */

