/* $Id$ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GIC).
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_GICInternal_h
#define VMM_INCLUDED_SRC_include_GICInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/gic.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/stam.h>


/** @defgroup grp_gic_int       Internal
 * @ingroup grp_gic
 * @internal
 * @{
 */

#ifdef VBOX_INCLUDED_vmm_pdmgic_h
/** The VirtualBox GIC backend. */
extern const PDMGICBACKEND g_GicBackend;
# ifdef RT_OS_DARWIN
/** The Hypervisor.Framework GIC backend. */
extern const PDMGICBACKEND g_GicHvfBackend;
# elif defined(RT_OS_WINDOWS)
/** The Hyper-V GIC backend. */
extern const PDMGICBACKEND g_GicHvBackend;
# elif defined(RT_OS_LINUX)
/** The KVM GIC backend. */
extern const PDMGICBACKEND g_GicKvmBackend;
# endif
#endif

#define VMCPU_TO_GICCPU(a_pVCpu)            (&(a_pVCpu)->gic.s)
#define VM_TO_GIC(a_pVM)                    (&(a_pVM)->gic.s)
#define VM_TO_GICDEV(a_pVM)                 CTX_SUFF(VM_TO_GIC(a_pVM)->pGicDev)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->gic.s.pDevInsR3)
#elif defined(IN_RING0)
# error "Not implemented!"
#endif

#if 0
/** Maximum number of SPI interrupts. */
#define GIC_SPI_MAX                         32
#endif

#if 0
/** @def GIC_CACHE_LINE_SIZE
 * Padding (in bytes) for aligning data in different cache lines. The ARMv8 cache
 * line size is 64 bytes.
 *
 * See ARM spec "Cache Size ID Register, CCSIDR_EL1".
 */
#define GIC_CACHE_LINE_SIZE                64

/**
 * GIC Interrupt-Delivery Bitmap (IDB).
 */
typedef struct GICIDB
{
    uint64_t volatile   au64IntIdBitmap[33];
    uint32_t volatile   fOutstandingNotification;
    uint8_t             abAlignment[52];
} GICIDB;
AssertCompileMemberOffset(GICIDB, fOutstandingNotification, 264);
AssertCompileSizeAlignment(GICIDB, GIC_CACHE_LINE_SIZE);
/** Pointer to a pending-interrupt bitmap. */
typedef GICIDB *PGICIDB;
/** Pointer to a const pending-interrupt bitmap. */
typedef const GICIDB *PCGICIDB;
#endif

/**
 * GIC PDM instance data (per-VM).
 */
typedef struct GICDEV
{
    /** The distributor MMIO handle. */
    IOMMMIOHANDLE               hMmioDist;
    /** The redistributor MMIO handle. */
    IOMMMIOHANDLE               hMmioReDist;

    /** @name Distributor register state.
     * @{
     */
#if 1
    /** Interrupt group bitmap. */
    uint32_t                    bmIntrGroup[64];
    /** Interrupt config bitmap (edge-triggered vs level-sensitive). */
    uint32_t                    bmIntrConfig[128];
    /** Interrupt enabled bitmap. */
    uint32_t                    bmIntrEnabled[64];
    /** Interrupt pending bitmap. */
    uint32_t                    bmIntrPending[64];
    /** Interrupt active bitmap. */
    uint32_t                    bmIntrActive[64];
    /** Interrupt priorities. */
    uint8_t                     abIntrPriority[2048];
    /** Interrupt routing info. */
    uint32_t                    au32IntrRouting[2048];
    /** Interrupt routine mode bitmap. */
    uint32_t                    bmIntrRoutingMode[64];

    /** Flag whether group 0 interrupts are enabled. */
    bool                        fIntrGroup0Enabled;
    /** Flag whether group 1 interrupts are enabled. */
    bool                        fIntrGroup1Enabled;
    /** Flag whether affinity routing is enabled. */
    bool                        fAffRoutingEnabled;
    /** Alignment. */
    bool                        fAlignment0;
    /** @} */
#else
    /** @name SPI distributor register state.
     * @{ */
    /** Interrupt Group 0 Register. */
    volatile uint32_t           u32RegIGrp0;
    /** Interrupt Configuration Register 0. */
    volatile uint32_t           u32RegICfg0;
    /** Interrupt Configuration Register 1. */
    volatile uint32_t           u32RegICfg1;
    /** Interrupt enabled bitmap. */
    volatile uint32_t           bmIntEnabled;
    /** Current interrupt pending state. */
    volatile uint32_t           bmIntPending;
    /** The current interrupt active state. */
    volatile uint32_t           bmIntActive;
    /** The interrupt priority for each of the SGI/PPIs */
    volatile uint8_t            abIntPriority[GIC_SPI_MAX];
    /** The interrupt routing information. */
    volatile uint32_t           au32IntRouting[GIC_SPI_MAX];

    /** Flag whether group 0 interrupts are currently enabled. */
    volatile bool               fIrqGrp0Enabled;
    /** Flag whether group 1 interrupts are currently enabled. */
    volatile bool               fIrqGrp1Enabled;
    /** @} */
#endif

    /** @name Configurables.
     * @{ */
    /** The maximum SPI supported (GICD_TYPER.ItsLinesNumber). */
    uint16_t                    uMaxSpi;
    /** Maximum extended SPI supported (GICR_TYPER.ESPI_range).  */
    uint16_t                    uMaxExtSpi;
    /** The GIC architecture (GICD_PIDR2.ArchRev and GICR_PIDR2.ArchRev). */
    uint8_t                     uArchRev;
    /** Extended PPIs supported (GICR_TYPER.PpiNum). */
    uint8_t                     fPpiNum;
    /** Whether extended SPIs are supported (GICD_TYPER.ESPI). */
    bool                        fExtSpi;
    /** Whether NMIs are supported (GICD_TYPER.NMI). */
    bool                        fNmi;
    /** Whether range-selector is supported (GICD_TYPER.RSS and ICC_CTLR_EL1.RSS). */
    bool                        fRangeSelSupport;
    /** Alignment. */
    bool                        afAlignment0[3];
    /** @} */
} GICDEV;
/** Pointer to a GIC device. */
typedef GICDEV *PGICDEV;
/** Pointer to a const GIC device. */
typedef GICDEV const *PCGICDEV;


/**
 * GIC VM Instance data.
 */
typedef struct GIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;
} GIC;
/** Pointer to GIC VM instance data. */
typedef GIC *PGIC;
/** Pointer to const GIC VM instance data. */
typedef GIC const *PCGIC;
AssertCompileSizeAlignment(GIC, 8);

/**
 * GIC VMCPU Instance data.
 */
typedef struct GICCPU
{
    /** @name The per vCPU redistributor data is kept here.
     * @{ */
    /** @} */

    /** @name Physical LPI register state.
     * @{ */
    /** @} */

    /** @name SGI and PPI redistributor register state.
     * @{ */
#if 1
    /** Interrupt group bitmap. */
    uint32_t                    bmIntrGroup[3];
    /** Interrupt config bitmap (edge-triggered vs level-sensitive). */
    uint32_t                    bmIntrConfig[6];
    /** Interrupt enabled bitmap. */
    uint32_t                    bmIntrEnabled[3];
    /** Interrupt pending bitmap. */
    uint32_t                    bmIntrPending[3];
    /** Interrupt active bitmap. */
    uint32_t                    bmIntrActive[3];
    /** Interrupt priorities. */
    uint8_t                     abIntrPriority[96];
#else
    /** Interrupt Group 0 Register. */
    volatile uint32_t           u32RegIGrp0;
    /** Interrupt Configuration Register 0. */
    volatile uint32_t           u32RegICfg0;
    /** Interrupt Configuration Register 1. */
    volatile uint32_t           u32RegICfg1;
    /** Interrupt enabled bitmap. */
    volatile uint32_t           bmIntEnabled;
    /** Current interrupt pending state. */
    volatile uint32_t           bmIntPending;
    /** The current interrupt active state. */
    volatile uint32_t           bmIntActive;
    /** The interrupt priority for each of the SGI/PPIs */
    volatile uint8_t            abIntPriority[GIC_INTID_RANGE_PPI_LAST + 1];
#endif
    /** @} */

    /** @name ICC system register state.
     * @{ */
    /** The control register (ICC_CTLR_EL1). */
    uint64_t                    uIccCtlr;
    /** The running priorities caused by preemption. */
    uint8_t                     abRunningPriorities[256];
    /** The index to the current running priority. */
    uint8_t                     idxRunningPriority;
    /** The current interrupt priority, only interrupts with a higher priority get signalled. */
    uint8_t                     bInterruptPriority;
    /** The binary point register for group 0 interrupts. */
    uint8_t                     bBinaryPtGroup0;
    /** The binary point register for group 1 interrupts. */
    uint8_t                     bBinaryPtGroup1;
    /** Flag whether group 0 interrupts are enabled. */
    bool                        fIntrGroup0Enabled;
    /** Flag whether group 1 interrupts are enabled. */
    bool                        fIntrGroup1Enabled;
    /** Alignment. */
    bool                        afAlignment0[2];
    /** @} */

    /** @name Log Max counters
     * @{ */
    uint32_t                    cLogMaxAccessError;
    uint32_t                    cLogMaxSetApicBaseAddr;
    uint32_t                    cLogMaxGetApicBaseAddr;
    uint32_t                    uAlignment4;
    /** @} */

    /** @name APIC statistics.
     * @{ */
#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadR3;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteR3;
    /** Number of MSR reads in R3. */
    STAMCOUNTER                 StatSysRegReadR3;
    /** Number of MSR writes in R3. */
    STAMCOUNTER                 StatSysRegWriteR3;

# if 0 /* No R0 for now. */
    /** Number of MMIO reads in RZ. */
    STAMCOUNTER                 StatMmioReadRZ;
    /** Number of MMIO writes in RZ. */
    STAMCOUNTER                 StatMmioWriteRZ;
    /** Number of MSR reads in RZ. */
    STAMCOUNTER                 StatSysRegReadRZ;
    /** Number of MSR writes in RZ. */
    STAMCOUNTER                 StatSysRegWriteRZ;
# endif
#endif
    /** @} */
} GICCPU;
/** Pointer to GIC VMCPU instance data. */
typedef GICCPU *PGICCPU;
/** Pointer to a const GIC VMCPU instance data. */
typedef GICCPU const *PCGICCPU;

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicReDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicReDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

DECLHIDDEN(void)                        gicResetCpu(PPDMDEVINS pDevIns, PVMCPUCC pVCpu);
DECLHIDDEN(void)                        gicReset(PPDMDEVINS pDevIns);
DECLHIDDEN(uint16_t)                    gicReDistGetIntIdFromIndex(uint16_t idxIntr);
DECLHIDDEN(uint16_t)                    gicDistGetIntIdFromIndex(uint16_t idxIntr);

DECLCALLBACK(int)                       gicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
DECLCALLBACK(int)                       gicR3Destruct(PPDMDEVINS pDevIns);
DECLCALLBACK(void)                      gicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta);
DECLCALLBACK(void)                      gicR3Reset(PPDMDEVINS pDevIns);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_GICInternal_h */

