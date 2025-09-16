/* $Id$ */
/** @file
 * VBox disassembler - Tables for ARMv8 A64 - SIMD.
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

.private_extern _TestProcA64Simd
_TestProcA64Simd:

    addv b0, v0.8B
    addv b0, v0.16B
    addv b31, v31.8B
    addv b31, v31.16B
    addv h0, v0.4H
    addv h0, v0.8H
    addv h31, v31.4H
    addv h31, v31.8H
    addv s0, v0.4S
    addv s31, v31.4S

    saddlv h0, v0.8B
    saddlv h0, v0.16B
    saddlv h31, v31.8B
    saddlv h31, v31.16B
    saddlv s0, v0.4H
    saddlv s0, v0.8H
    saddlv s31, v31.4H
    saddlv s31, v31.8H
    saddlv d0, v0.4S
    saddlv d31, v31.4S

    uaddlv h0, v0.8B
    uaddlv h0, v0.16B
    uaddlv h31, v31.8B
    uaddlv h31, v31.16B
    uaddlv s0, v0.4H
    uaddlv s0, v0.8H
    uaddlv s31, v31.4H
    uaddlv s31, v31.8H
    uaddlv d0, v0.4S
    uaddlv d31, v31.4S

    smaxv b0, v0.8B
    smaxv b0, v0.16B
    smaxv b31, v31.8B
    smaxv b31, v31.16B
    smaxv h0, v0.4H
    smaxv h0, v0.8H
    smaxv h31, v31.4H
    smaxv h31, v31.8H
    smaxv s0, v0.4S
    smaxv s31, v31.4S

    umaxv b0, v0.8B
    umaxv b0, v0.16B
    umaxv b31, v31.8B
    umaxv b31, v31.16B
    umaxv h0, v0.4H
    umaxv h0, v0.8H
    umaxv h31, v31.4H
    umaxv h31, v31.8H
    umaxv s0, v0.4S
    umaxv s31, v31.4S

    sminv b0, v0.8B
    sminv b0, v0.16B
    sminv b31, v31.8B
    sminv b31, v31.16B
    sminv h0, v0.4H
    sminv h0, v0.8H
    sminv h31, v31.4H
    sminv h31, v31.8H
    sminv s0, v0.4S
    sminv s31, v31.4S

    uminv b0, v0.8B
    uminv b0, v0.16B
    uminv b31, v31.8B
    uminv b31, v31.16B
    uminv h0, v0.4H
    uminv h0, v0.8H
    uminv h31, v31.4H
    uminv h31, v31.8H
    uminv s0, v0.4S
    uminv s31, v31.4S

    ;
    ; Keep last so the testcase can catch errors in
    ; the disassembly of the last instruction.
    ;
    nop

.private_extern _TestProcA64Simd_EndProc
_TestProcA64Simd_EndProc:
