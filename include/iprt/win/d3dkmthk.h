/** @file
 * Safe way to include d3dkmthk.h.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_win_d3dkmthk_h
#define IPRT_INCLUDED_win_d3dkmthk_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef _MSC_VER
# pragma warning(push)
# if _MSC_VER >= 1900 /*RT_MSC_VER_VC140*/
#  pragma warning(disable:4255) /* d3dkmthk.h(2061): warning C4255: 'PFND3DKMT_CHECKEXCLUSIVEOWNERSHIP': no function prototype given: converting '()' to '(void)' */
# endif
# if _MSC_VER >= 1930 /*RT_MSC_VER_VC143*/
#  pragma warning(disable:5249) /* d3dkmthk.h(208): warning C5249: '_D3DKMDT_DISPLAYMODE_FLAGS::ModePruningReason' of type 'D3DKMDT_MODE_PRUNING_REASON' has named enumerators with values that cannot be represented in the given bit field width of '4'. */
# endif
#endif

/* 10.0.22000.0 SDK: */
#define DXGKDDI_INTERFACE_VERSION_WDDM_1_3 DXGKDDI_INTERFACE_VERSION_WDDM1_3
#define DXGKDDI_INTERFACE_VERSION_WDDM_2_0 DXGKDDI_INTERFACE_VERSION_WDDM2_0
#define DXGKDDI_INTERFACE_VERSION_WDDM1_3_M1 DXGKDDI_INTERFACE_VERSION_WDDM1_3

#include <d3dkmthk.h>

#ifdef _MSC_VER
# pragma warning(pop)
#endif

#endif /* !IPRT_INCLUDED_win_d3dkmthk_h */

