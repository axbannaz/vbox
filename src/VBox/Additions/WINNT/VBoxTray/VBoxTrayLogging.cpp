/* $Id$ */
/** @file
 * VBoxTrayLogging.cpp - Logging.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <package-generated.h>
#include "product-generated.h"

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"

#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/handle.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/system.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PRTLOGGER      g_pLoggerRelease = NULL;           /**< This is actually the debug logger in DEBUG builds! */
/** Note: The following parameters are not yet modifiable via command line or some such, but keep them here for later. */
static uint32_t       g_cHistory = 10;                   /**< Enable log rotation, 10 files. */
static uint32_t       g_uHistoryFileTime = RT_SEC_1DAY;  /**< Max 1 day per file. */
static uint64_t       g_uHistoryFileSize = 100 * _1M;    /**< Max 100MB per file. */
/** The log window instance data. */
VBOXTRAYLOGWND        g_LogWnd;


/**
 * Header/footer callback for the release logger.
 *
 * @param   pLoggerRelease
 * @param   enmPhase
 * @param   pfnLog
 */
static DECLCALLBACK(void) vboxTrayLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "VBoxTray %s r%s %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), VBOX_BUILD_TARGET,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef VBOX_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   VBOX_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */;
    }
}

static int vboxTrayLogWndPrintV(const char *pszFormat, va_list va)
{
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, va);
    AssertPtrReturn(psz, VERR_NO_STR_MEMORY);

    int len = GetWindowTextLength(g_LogWnd.hWndLogMainEdit);
    SendMessage(g_LogWnd.hWndLogMainEdit, EM_SETSEL, len, len);
    SendMessage(g_LogWnd.hWndLogMainEdit, EM_REPLACESEL, 0, (LPARAM)psz);

    RTStrFree(psz);

    return VINF_SUCCESS;
}

static int vboxTrayLogWndPrint(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int const rc = vboxTrayLogWndPrintV(pszFormat, va);
    va_end(va);
    return rc;
}

static DECLCALLBACK(int) vboxTrayLogWndLogWriteCallback(PCRTLOGOUTPUTIF pIf, void *pvUser, const void *pvBuf,
                                                        size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF(pIf, pvUser, pcbWritten);

    vboxTrayLogWndPrint("%.*s\n", pvBuf, cbWrite);
RT_BREAKPOINT();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vboxTrayLogWndThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);

    PVBOXTRAYLOGWND pLogWnd = (PVBOXTRAYLOGWND)pvUser;
    AssertPtr(pLogWnd);

    vboxTrayLogWndPrint("--- Logging started ---\n");

    int rc = 0;

#if 0
    RTPOLLSET hPollSet;
    rc = RTPollSetCreate(&hPollSet);
    if (RT_SUCCESS(rc))
    {
        //uint32_t uFlags = RTPOLL_EVT_ERROR;

        RTHANDLE hStdOut;
    RT_BREAKPOINT();
        rc = RTHandleGetStandard(RTHANDLESTD_OUTPUT, true /* fLeaveOpen */, &hStdOut);

        /* Stdout */
        RTPIPE hPipeStdOutW;
        rc = RTPipeFromNative(&hPipeStdOutW, (RTHCINTPTR)&hStdOut.u.hPipe, RTPIPE_N_WRITE);

        RTPIPE hStdOutR;
        if (RT_SUCCESS(rc))
           rc = RTPipeCreate(&hStdOutR, &hPipeStdOutW, 0 /* Flags */);
        //if (RT_SUCCESS(rc))
        //    rc = RTPollSetAddPipe(hPollSet, hStdOutR, uFlags, 2);

        /* Stderr */
        RTPIPE hPipeStdErrW;
        rc = RTPipeFromNative(&hPipeStdErrW, (RTHCINTPTR)GetStdHandle(STD_ERROR_HANDLE), RTPIPE_N_WRITE);

        RTPIPE hStdErrR;
        if (RT_SUCCESS(rc))
            rc = RTPipeCreate(&hStdErrR, &hPipeStdErrW, 0 /* Flags */);
        //if (RT_SUCCESS(rc))
        //    rc = RTPollSetAddPipe(hPollSet, hStdErrR, uFlags, 2);

        /* Notification pipe */
        RTPIPE hNotifyPipeR;
        if (RT_SUCCESS(rc))
            rc = RTPipeCreate(&hNotifyPipeR, &pLogWnd->hNotifyPipeW, 0 /* Flags */);
        //if (RT_SUCCESS(rc))
        //    rc = RTPollSetAddPipe(hPollSet, hNotifyPipeR, RTPOLL_EVT_READ, 4);
    }

    //HANDLE hPipeWrite;
    //SetStdHandle(STD_OUTPUT_HANDLE, hPipeWrite);

    RTPIPE hPipeStdOutW;
    DWORD w = GetFileType((HANDLE)1);
    RT_NOREF(w);
    rc = RTPipeFromNative(&hPipeStdOutW, (RTHCINTPTR)STD_OUTPUT_HANDLE, RTPIPE_N_READ);

    /* Connect a read pipe specified by phPipe to stdout or stderr. */
    RTPIPE hPipeStdOut;
    rc = RTPipeCreate(&hPipeStdOut, &hPipeStdOutW, RTPIPE_C_INHERIT_WRITE);
#endif

    //RTFILE hFieStdOut;
    //int rc = RTFileFromNative(&hFieStdOut,  (RTHCINTPTR)GetStdHandle(STD_OUTPUT_HANDLE));
    //PRTSTREAM pStream;
    //rc = RTStrmOpenFileHandle(hFieStdOut, "r", 0, &pStream);
    /** @todo Closing of standard handles not support via IPRT (yet). */
    //RTStrmOpenFileHandle(hStdOut, "wt", 0, &g_pStdOut);
#if 1
#if 0
    char    abBuf[_1K];
    size_t  cbRead;
    while (   (rc = RTStrmReadEx(pStream, abBuf, sizeof(abBuf), &cbRead))
           && RT_SUCCESS(rc))
    {
        abBuf[cbRead] = '\0';
        vboxTrayLogWndPrintf("%s\n", abBuf);

    }
#endif
#else

    //RTHANDLE hStdOut;
    //RTHandleGetStandard(RTHANDLESTD_ERROR, true /*fLeaveOpen*/, &hStdOut);

    //RTPipeFromNative(&pLogWnd->hPipeStdOutR, (RTHCINTPTR)&hStdOut, RTPIPE_N_READ);
    //RTPipeFromNative(&pLogWnd->hPipeStdOutR, STD_OUTPUT_HANDLE, RTPIPE_N_READ);

    int     rc = 0;
    //rc = RTPipeCreate(&pLogWnd->hPipeStdOutR, &hStdOut.u.hPipe, RTPIPE_C_INHERIT_READ);
    AssertRC(rc);

    //rc = RTPipeReadBlocking(pLogWnd->hPipeStdOutR, abBuf, sizeof(abBuf), &cbRead);
    //AssertRC(rc);
    //RT_BREAKPOINT();
#endif

#if 0
    char    abBuf[_1K];
    size_t  cbRead;
    while (   (rc = RTPipeReadBlocking(hPipeStdOutW, abBuf, sizeof(abBuf), &cbRead))
           && RT_SUCCESS(rc))
    {
        abBuf[cbRead] = '\0';
        vboxTrayLogWndPrintf("%s\n", abBuf);

    }
#endif

    RTThreadSleep(5000);

    vboxTrayLogWndPrint("End: %d\n", rc);

    //RTLogDestroy(pLogger);
    //pLogger = NULL;

    vboxTrayLogWndPrint("--- Logging ended ---");
    return VINF_SUCCESS;
}

static LRESULT CALLBACK vboxTrayLogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            g_LogWnd.hWndLogMainEdit = CreateWindow("EDIT", "",
                                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                                    CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                                                    hwnd, (HMENU)ID_WND_LOG_MAIN_MENU, NULL, NULL);

    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    static const char         s_szGroupSettings[] = "all.e.l.l2.l3.l4.f";

    RTLOGOUTPUTIF OutputIf;
    RT_ZERO(OutputIf);

    OutputIf.pfnWrite = vboxTrayLogWndLogWriteCallback;
RT_BREAKPOINT();
    RTLOGGER       *pLogger;
    RTERRINFOSTATIC ErrInfo;
    int rc = RTLogCreateEx(&pLogger, "",
                           RTLOGFLAGS_PREFIX_TIME | RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_USECRLF,
                           s_szGroupSettings, RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_USER,
                           vboxTrayLogHeaderFooter,
                           0 /* cHistory */, 0 /* uHistoryFileTime */, 0 /* uHistoryFileSize */,
                           &OutputIf /*pOutputIf*/, g_LogWnd.hWndLogMainEdit /*pvOutputIfUser*/,
                           RTErrInfoInitStatic(&ErrInfo), NULL);

#ifdef DEBUG
        /* Register this logger as the _debug_ logger. */
        RTLogSetDefaultInstance(pLogger);
#else
        /* Register this logger as the release logger. */
        RTLogRelSetDefaultInstance(pLogger);
#endif

            rc = RTThreadCreate(&g_LogWnd.hThread, vboxTrayLogWndThread, &g_LogWnd /* pvUser */,
                                    0, RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE, "logwnd");
            if (RT_SUCCESS(rc))
            {
            }
            break;
        }

        case WM_SIZE:
            MoveWindow(g_LogWnd.hWndLogMainEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;

        case WM_CLOSE:
            ShowWindow(g_LogWnd.hWndLogMain, SW_HIDE);
            break;

        case WM_DESTROY:
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int VBoxTrayLogWindowCreate(void)
{
    WNDCLASSEX wc;
    RT_ZERO(wc);

    /* Create a custom window class. */
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = (WNDPROC)vboxTrayLogWndProc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "VBoxTrayLogWndClass";

    if (RegisterClassEx(&wc))
    {
        /* Note: Keep the window small enough so that Windows guests w/o Guest Additions installed and
         *       thus having a small video resolution don't get in trouble when showing this window. */
        int const w = 640;
        int const h = 480;

        RECT rc;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);

        int const x = rc.left + ((rc.right - rc.left) - w) / 2;
        int const y = rc.top  + ((rc.bottom - rc.top) - h) / 2;

        DWORD fWin = WS_OVERLAPPEDWINDOW;
#ifdef DEBUG_andy
        fWin |= WS_VISIBLE;
#endif
        g_LogWnd.hWndLogMain = CreateWindow("VBoxTrayLogWndClass", VBOX_VBOXTRAY_TITLE " - Log Window",
                                            fWin,
                                            x, y, w, h,
                                            NULL, NULL, g_hInstance, g_hwndToolWindow);

    }
    else
        VBoxTrayError("Registering log window failed, error = %#x\n", GetLastError());

    return VINF_SUCCESS;
}

void VBoxTrayLogWindowDestroy(void)
{
    UnregisterClassW(L"VBoxTrayLogWndClass", g_hInstance);
}

/**
 * Creates the default release logger outputting to the specified file.
 *
 * @return  VBox status code.
 * @param   pszLogFile          Path to log file to use. Can be NULL if not needed.
 */
int VBoxTrayLogCreate(const char *pszLogFile)
{
    /* Create release (or debug) logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
#ifdef DEBUG
    static const char s_szEnvVarPfx[] = "VBOXTRAY_LOG";
    static const char s_szGroupSettings[] = "all.e.l.f";
#else
    static const char s_szEnvVarPfx[] = "VBOXTRAY_RELEASE_LOG";
    static const char s_szGroupSettings[] = "all";
#endif
    RTERRINFOSTATIC ErrInfo;
    int rc = RTLogCreateEx(&g_pLoggerRelease, s_szEnvVarPfx,
                           RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG | RTLOGFLAGS_USECRLF,
                           s_szGroupSettings, RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT | RTLOGDEST_USER,
                           vboxTrayLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           RTErrInfoInitStatic(&ErrInfo), "%s", pszLogFile ? pszLogFile : "");
RT_BREAKPOINT();
    if (RT_SUCCESS(rc))
    {
        char szGroupSettings[_1K];

#ifdef DEBUG
        /* Register this logger as the _debug_ logger. */
        RTLogSetDefaultInstance(g_pLoggerRelease);
#else
        /* Register this logger as the release logger. */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);
#endif
        /* If verbosity is explicitly set, make sure to increase the logging levels for
         * the logging groups we offer functionality for in VBoxTray. */
        if (g_cVerbosity)
        {
            /* All groups we want to enable logging for VBoxTray. */
#ifdef DEBUG
            const char *apszGroups[] = { "guest_dnd", "shared_clipboard" };
#else /* For release builds we always want all groups being logged in verbose mode. Don't change this! */
            const char *apszGroups[] = { "all" };
#endif
            szGroupSettings[0] = '\0';

            for (size_t i = 0; i < RT_ELEMENTS(apszGroups); i++)
            {
                if (i > 0)
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), "+");
                if (RT_SUCCESS(rc))
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), apszGroups[i]);
                if (RT_FAILURE(rc))
                    break;

                switch (g_cVerbosity)
                {
                    case 1:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2");
                        break;

                    case 2:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3");
                        break;

                    case 3:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4");
                        break;

                    case 4:
                        RT_FALL_THROUGH();
                    default:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4.f");
                        break;
                }

                if (RT_FAILURE(rc))
                    break;
            }

            if (RT_SUCCESS(rc))
            {
                rc = RTLogGroupSettings(g_pLoggerRelease, szGroupSettings);
                if (RT_FAILURE(rc))
                    VBoxTrayShowError("Setting log group settings failed, rc=%Rrc\n", rc);
            }
        }

        /* Explicitly flush the log in case of VBOXTRAY_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);

        VBoxTrayInfo("Verbosity level: %d\n", g_cVerbosity);

        int const rc2 = RTLogQueryGroupSettings(g_pLoggerRelease, szGroupSettings, sizeof(szGroupSettings));
        if (RT_SUCCESS(rc2))
            VBoxTrayInfo("Log group settings are: %s\n", szGroupSettings);
    }
    else
        VBoxTrayShowError(ErrInfo.szMsg);

    return rc;
}

/**
 * Destroys the logging.
 */
void VBoxTrayLogDestroy(void)
{
    /* Only want to destroy the release logger before calling exit(). The debug
       logger can be useful after that point... */
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

