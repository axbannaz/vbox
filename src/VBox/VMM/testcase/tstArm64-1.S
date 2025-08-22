/* $Id$ */
/** @file
 * ARM64 instruction set testcase \#1.
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

#include <iprt/asmdefs-arm.h>


/*
 * Main entry point.
 */
.global NAME(main)
NAME(main):
BEGINPROC_EXPORTED  MainEntrypoint
        stp     x19, x20, [sp, #-0x60]!
        stp     x21, x22, [sp, #0x10]
        stp     x23, x24, [sp, #0x20]
        stp     x25, x26, [sp, #0x30]
        stp     x27, x28, [sp, #0x40]
        stp     x29, x30, [sp, #0x50]       /* top */

        bl      NAME(HandcodedTests1)
        bl      NAME(GeneratedTests)

        ldp     x29, x30, [sp, #0x50]
        ldp     x27, x28, [sp, #0x40]
        ldp     x25, x26, [sp, #0x30]
        ldp     x23, x24, [sp, #0x20]
        ldp     x21, x22, [sp, #0x10]
        ldp     x19, x20, [sp], #0x60
        movz    x0, #0
        ret
ENDPROC             MainEntrypoint


BEGINPROC_EXPORTED  HandcodedTests1
        /* 1. sub w/ zero. */
        movz    x0, #0xffff
        mov     x10, x0
        sub     x1, x0, xzr
        cmp     x1, x10
        b.eq    L1
        brk     #1
L1:

        ret
ENDPROC             HandcodedTests1


#include "tstArm64-1-generated.S"

