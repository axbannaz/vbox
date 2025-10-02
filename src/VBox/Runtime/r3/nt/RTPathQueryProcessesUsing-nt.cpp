/* $Id$ */
/** @file
 * IPRT - RTPathQueryProcessesUsing, Native NT.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/nt/nt-and-windows.h>
#include "internal-r3-nt.h"

#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include "internal/fs.h"
#include "internal/path.h"


/**
 * Resolves the given path (usually something like C:\... to the final NT path).
 *
 * @returns IPRT status code.
 * @param   pszPath     The path to resolve.
 * @param   pNtName     Where to store the NT name, free with RTNtPathFree();
 */
static int rtNtPathResolveFinal(const char *pszPath, struct _UNICODE_STRING *pNtName)
{
    /*
     * RTNtPathFromWinUtf8() will usually convert something like C:\Windows to \??\C:\Windows.
     * while we need the real NT path later on.
     * So after the first conversion we have to open the object and get the final path from the
     * handle.
     */
    HANDLE         hRootDir;
    UNICODE_STRING NtName;
    int rcRet = RTNtPathFromWinUtf8(&NtName, &hRootDir, pszPath);
    if (RT_FAILURE(rcRet))
        return rcRet;

    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    OBJECT_ATTRIBUTES   ObjAttr;

    /* Try open the path. */
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, hRootDir, NULL);
    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /*pcbFile*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_OPEN,
                                 FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*pvEaBuffer*/,
                                 0 /*cbEa*/);
    if (NT_SUCCESS(rcNt))
    {
        rcRet = RTNtPathFromHandle(pNtName, hFile, 0);
        NtClose(hFile);
    }

    RTNtPathFree(&NtName, &hRootDir);
    return rcRet;
}


RTR3DECL(int) RTPathQueryProcessesUsing(const char *pszPath, uint32_t fFlags, uint32_t *pcProcesses, PRTPROCESS paPids)
{
    /*
     * Convert the input path.
     */
    UNICODE_STRING NtName;
    int rcRet = rtNtPathResolveFinal(pszPath, &NtName);
    if (RT_FAILURE(rcRet))
        return rcRet;

    /*
     * Take a snapshot of all the handles in the system.
     */
    uint32_t cbBuf = _256K;
    uint8_t *pbBuf = (uint8_t *)RTMemAlloc(cbBuf);
    if (!pbBuf)
    {
        RTNtPathFree(&NtName, NULL);
        return VERR_NO_MEMORY;
    }

    ULONG cbNeeded = cbBuf;
    NTSTATUS rcNt = NtQuerySystemInformation(SystemExtendedHandleInformation, pbBuf, cbBuf, &cbNeeded);
    if (!NT_SUCCESS(rcNt))
    {
        while (   rcNt == STATUS_INFO_LENGTH_MISMATCH
               && cbNeeded > cbBuf
               && cbBuf <= _32M)
        {
            cbBuf = RT_ALIGN_32(cbNeeded + _4K, _64K);
            RTMemFree(pbBuf);
            pbBuf = (uint8_t *)RTMemAlloc(cbBuf);
            if (!pbBuf)
                return VERR_NO_MEMORY;
            rcNt = NtQuerySystemInformation(SystemExtendedHandleInformation, pbBuf, cbBuf, &cbNeeded);
        }
        if (!NT_SUCCESS(rcNt))
        {
            RTMemFree(pbBuf);
            return RTErrConvertFromNtStatus(rcNt);
        }
    }

    /*
     * Examine the snapshot for handles and check each handle not belonging to the calling process.
     */
    rcRet = VINF_SUCCESS;

    uint32_t                            cProcesses = 0;
    HANDLE const                        idProcess  = RTNtCurrentTeb()->ClientId.UniqueProcess;
    SYSTEM_HANDLE_INFORMATION_EX const *pInfo      = (SYSTEM_HANDLE_INFORMATION_EX const *)pbBuf;
    ULONG_PTR                           i          = pInfo->NumberOfHandles;
    AssertRelease(RT_UOFFSETOF_DYN(SYSTEM_HANDLE_INFORMATION_EX, Handles[i]) == cbNeeded);
    while (i-- > 0)
    {
        SYSTEM_HANDLE_ENTRY_INFO_EX const *pHandleInfo = &pInfo->Handles[i];
        if (pHandleInfo->UniqueProcessId != idProcess)
        {
            CLIENT_ID ClientId;
            ClientId.UniqueProcess = pHandleInfo->UniqueProcessId;
            ClientId.UniqueThread  = NULL;

            OBJECT_ATTRIBUTES ObjAttrs;
            InitializeObjectAttributes(&ObjAttrs, NULL, OBJ_CASE_INSENSITIVE, NULL, NULL);

            HANDLE hProcess;
            rcNt = NtOpenProcess(&hProcess, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE, &ObjAttrs, &ClientId);
            if (!NT_SUCCESS(rcNt))
                rcNt = NtOpenProcess(&hProcess, PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE, &ObjAttrs, &ClientId);
            if (!NT_SUCCESS(rcNt))
                continue;

            HANDLE hDup;
            HANDLE hProcSelf = NtCurrentProcess();
            rcNt = NtDuplicateObject(hProcess, pHandleInfo->HandleValue, hProcSelf, &hDup, SYNCHRONIZE, 0, 0);
            if (NT_SUCCESS(rcNt))
            {
                /* Continue if this is a disk file object as RTNtPathFromHandle (calling NtQueryObject()) might hang for anything else. */
#if 0 /** @todo This also triggers for named pipes, causing RTNtPathFromHandle() to get stuck again, investigate what GetFileType() is doing. */
                union
                {
                    OBJECT_TYPE_INFORMATION TypeInfo;
                    uint8_t                 ab[128];
                } ObjTypeInfo;
                rcNt = NtQueryObject(hDup, ObjectTypeInformation,
                                     &ObjTypeInfo, sizeof(ObjTypeInfo), NULL);
                if (   NT_SUCCESS(rcNt)
                    && ObjTypeInfo.TypeInfo.TypeName.Length == sizeof(L"File") - sizeof(wchar_t)
                    && memcmp(ObjTypeInfo.TypeInfo.TypeName.Buffer, L"File", sizeof(L"File") - sizeof(wchar_t)) == 0)
#else
                if (GetFileType(hDup) == FILE_TYPE_DISK)
#endif
                {
                    UNICODE_STRING NtNameDup; RT_ZERO(NtNameDup);
                    int rc2 = RTNtPathFromHandle(&NtNameDup, hDup, 0);
                    if (RT_SUCCESS(rc2))
                    {
                        bool fMatch;
                        if (fFlags & RTPATH_QUERY_PROC_F_DIR_INCLUDE_SUB_OBJ)
                            fMatch =    NtName.Length <= NtNameDup.Length
                                     && RTUtf16NICmp(NtName.Buffer, NtNameDup.Buffer, NtName.Length / sizeof(RTUTF16)) == 0;
                        else
                            fMatch =    NtName.Length == NtNameDup.Length
                                     && RTUtf16ICmp(NtName.Buffer, NtNameDup.Buffer) == 0;

                        if (fMatch)
                        {
                            /* Query the process ID. */
                            PROCESS_BASIC_INFORMATION ProcInf; RT_ZERO(ProcInf);
                            rcNt = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &ProcInf, sizeof(ProcInf), NULL);
                            if (NT_SUCCESS(rcNt))
                            {
                                if (cProcesses < *pcProcesses)
                                    paPids[cProcesses] = (RTPROCESS)(uintptr_t)ProcInf.UniqueProcessId;
                                else
                                    rcRet = VINF_BUFFER_OVERFLOW;
                                cProcesses++;
                            }
                        }

                        RTNtPathFree(&NtNameDup, NULL);
                    }
                }

                NtClose(hDup);
            }

            NtClose(hProcess);
        }
    }
    RTMemFree(pbBuf);

    *pcProcesses = cProcesses;

    RTNtPathFree(&NtName, NULL);
    return rcRet;
}