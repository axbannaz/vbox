/* $Id$ */
/** @file
 * IPRT - ISO 9660 and UDF Virtual Filesystem (read only).
 */

/*
 * Copyright (C) 2017-2025 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_FS
#include "internal/iprt.h"
#include <iprt/fsvfs.h>

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/crc.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/uni.h>
#include <iprt/utf16.h>
#include <iprt/formats/iso9660.h>
#include <iprt/formats/udf.h>

#include "udfhlp.h"



/**
 * UDF virtual partition read function.
 *
 * This deals with all the fun related to block mapping and such.
 *
 * @returns VBox status code.
 * @param   pVolInfo        The volume information (partition map ++).
 * @param   hVfsBacking     The backing file/device/whatever.
 * @param   idxPart         The virtual partition number.
 * @param   idxBlock        The block number.
 * @param   pvBuf           The output buffer.
 * @param   cbToRead        The number of bytes to read.
 * @param   offByteAddend   The byte offset relative to the block.
 */
DECLHIDDEN(int) RTFsUdfHlpVpRead(PCRTFSUDFVOLINFO pVolInfo, RTVFSFILE hVfsBacking,
                                 uint32_t idxPart, uint32_t idxBlock, void *pvBuf, size_t cbToRead, uint64_t offByteAddend /*=0*/)
{
    uint64_t const offByte = ((uint64_t)idxBlock << pVolInfo->cShiftBlock) + offByteAddend;

    int rc;
    if (idxPart < pVolInfo->cPartitions)
    {
        PRTFSISOVOLUDFPMAP  pPart = &pVolInfo->paPartitions[idxPart];
        switch (pPart->bType)
        {
            case RTFSUDF_PMAP_T_PLAIN:
                rc = RTVfsFileReadAt(hVfsBacking, offByte + pPart->offByteLocation, pvBuf, cbToRead, NULL);
                if (RT_SUCCESS(rc))
                {
                    Log3(("ISO/UDF: Read %#x bytes at %#RX64 (p%u/%#RX64(/%#RX64))\n",
                          cbToRead, offByte + pPart->offByteLocation, idxPart, offByte, offByte >> pVolInfo->cShiftBlock));
                    return VINF_SUCCESS;
                }
                Log(("ISO/UDF: Error reading %#x bytes at %#RX64 (p%u/%#RX64(/%#RX64)): %Rrc\n",
                     cbToRead, offByte + pPart->offByteLocation, idxPart, offByte, offByte >> pVolInfo->cShiftBlock, rc));
                break;

            default:
                AssertFailed();
                rc = VERR_ISOFS_IPE_1;
                break;
        }
    }
    else
    {
        Log(("ISO/UDF: Invalid partition index %#x (offset %#RX64), max partitions %#x\n",
             idxPart, offByte, pVolInfo->cPartitions));
        rc = VERR_ISOFS_INVALID_PARTITION_INDEX;
    }
    return rc;
}


#ifdef LOG_ENABLED
/**
 * Logs the content of a directory.
 */
DECLHIDDEN(void) RTFsUdfHlpLogDirContent(uint8_t const *pbDir, size_t cbDir)
{
    if (LogIs2Enabled())
    {
        uint32_t offDesc = 0;
        while (offDesc + RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) < cbDir)
        {
            PCUDFFILEIDDESC pFid  = (PCUDFFILEIDDESC)&pbDir[offDesc];
            uint32_t const  cbFid = UDFFILEIDDESC_GET_SIZE(pFid);
            if (offDesc + cbFid > cbDir)
                break;

            uint32_t    cwcName = 0;
            RTUTF16     wszName[260];
            if (pFid->cbName > 0)
            {
                uint8_t const *pbName = UDFFILEIDDESC_2_NAME(pFid);
                uint32_t       offSrc = 1;
                if (*pbName == 8)
                    while (offSrc < pFid->cbName)
                    {
                        wszName[cwcName] = pbName[offSrc];
                        cwcName++;
                        offSrc++;
                    }
                else if (*pbName == 16)
                    while (offSrc + 1 <= pFid->cbName)
                    {
                        wszName[cwcName] = RT_MAKE_U16(pbName[offSrc + 1], pbName[offSrc]);
                        cwcName++;
                        offSrc += 2;
                    }
                else
                {
                    RTUtf16CopyAscii(wszName, RT_ELEMENTS(wszName), "<bad type>");
                    cwcName = 10;
                }
            }
            else if (pFid->fFlags & UDF_FILE_FLAGS_PARENT)
            {
                wszName[0] = '.';
                wszName[1] = '.';
                cwcName    = 2;
            }
            else
            {
                RTUtf16CopyAscii(wszName, RT_ELEMENTS(wszName), "<empty>");
                cwcName = 7;
            }
            wszName[cwcName] = '\0';

            Log2(("ISO/UDF: %04x: fFlags=%#x uVer=%u Icb={%#04x:%#010RX32 LB %#06x t=%u} cbName=%#04x cbIU=%#x '%ls'\n",
                  offDesc,
                  pFid->fFlags,
                  pFid->uVersion,
                  pFid->Icb.Location.uPartitionNo,
                  pFid->Icb.Location.off,
                  pFid->Icb.cb,
                  pFid->Icb.uType,
                  pFid->cbName,
                  pFid->cbImplementationUse,
                  wszName));
            int rc = RTFsUdfHlpValidateDescTagAndCrc(&pFid->Tag, cbDir - offDesc,
                                                        UDF_TAG_ID_FILE_ID_DESC, pFid->Tag.offTag, NULL);
            if (RT_FAILURE(rc))
                Log2(("ISO/UDF:      Bad Tag: %Rrc - idTag=%#x\n", rc, pFid->Tag.idTag));
            if (pFid->cbImplementationUse > 32)
                Log2(("ISO/UDF:      impl use (%#x bytes):\n%.*RhxD\n",
                      pFid->cbImplementationUse, pFid->cbImplementationUse, pFid->abImplementationUse));
            else if (pFid->cbImplementationUse > 0)
                Log2(("ISO/UDF:      impl use (%#x bytes): %.*Rhxs\n",
                      pFid->cbImplementationUse, pFid->cbImplementationUse, pFid->abImplementationUse));

            /* advance */
            offDesc += cbFid;
        }

        if (offDesc < cbDir)
            Log2(("ISO/UDF:  warning! %#x trailing bytes in directory:\n%.*RhxD\n",
                  cbDir - offDesc, cbDir - offDesc, &pbDir[offDesc]));
    }
}
#endif /* LOG_ENABLED */


/**
 * Checks the descriptor tag and CRC.
 *
 * @retval  IPRT status code.
 * @retval  VERR_ISOFS_TAG_IS_ALL_ZEROS
 * @retval  VERR_MISMATCH
 * @retval  VERR_ISOFS_UNSUPPORTED_TAG_VERSION
 * @retval  VERR_ISOFS_TAG_SECTOR_MISMATCH
 * @retval  VERR_ISOFS_BAD_TAG_CHECKSUM
 *
 * @param   pTag        The tag to check.
 * @param   idTag       The expected descriptor tag ID, UINT16_MAX matches any
 *                      tag ID.
 * @param   offTag      The sector offset of the tag.
 * @param   pErrInfo    Where to return extended error info.
 */
DECLHIDDEN(int) RTFsUdfHlpValidateDescTag(PCUDFTAG pTag, uint16_t idTag, uint32_t offTag, PRTERRINFO pErrInfo)
{
    /*
     * Checksum the tag first.
     */
    const uint8_t *pbTag     = (const uint8_t *)pTag;
    uint8_t const  bChecksum = pbTag[0]
                             + pbTag[1]
                             + pbTag[2]
                             + pbTag[3]
                             + pbTag[5] /* skipping byte 4 as that's the checksum. */
                             + pbTag[6]
                             + pbTag[7]
                             + pbTag[8]
                             + pbTag[9]
                             + pbTag[10]
                             + pbTag[11]
                             + pbTag[12]
                             + pbTag[13]
                             + pbTag[14]
                             + pbTag[15];
    if (pTag->uChecksum == bChecksum)
    {
        /*
         * Do the matching.
         */
        if (   pTag->uVersion == 3
            || pTag->uVersion == 2)
        {
            if (   pTag->idTag == idTag
                || idTag == UINT16_MAX)
            {
                if (pTag->offTag == offTag)
                {
                    //Log3(("ISO/UDF: Valid descriptor %#06x at %#010RX32; cbDescriptorCrc=%#06RX32 uTagSerialNo=%#x\n",
                    //      pTag->idTag, offTag, pTag->cbDescriptorCrc, pTag->uTagSerialNo));
                    return VINF_SUCCESS;
                }

                Log(("RTFsUdfHlpValidateDescTag(,%#x,%#010RX32,): Sector mismatch: %#RX32 (%.*Rhxs)\n",
                     idTag, offTag, pTag->offTag, sizeof(*pTag), pTag));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_ISOFS_TAG_SECTOR_MISMATCH,
                                           "Descriptor tag sector number mismatch: %#x, expected %#x (%.*Rhxs)",
                                           pTag->offTag, offTag, sizeof(*pTag), pTag);
            }
            Log(("RTFsUdfHlpValidateDescTag(,%#x,%#010RX32,): Tag ID mismatch: %#x (%.*Rhxs)\n",
                 idTag, offTag, pTag->idTag, sizeof(*pTag), pTag));
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_MISMATCH, "Descriptor tag ID mismatch: %#x, expected %#x (%.*Rhxs)",
                                       pTag->idTag, idTag, sizeof(*pTag), pTag);
        }
        if (ASMMemIsZero(pTag, sizeof(*pTag)))
        {
            Log(("RTFsUdfHlpValidateDescTag(,%#x,%#010RX32,): All zeros\n", idTag, offTag));
            return RTERRINFO_LOG_SET(pErrInfo, VERR_ISOFS_TAG_IS_ALL_ZEROS, "Descriptor is all zeros");
        }

        Log(("RTFsUdfHlpValidateDescTag(,%#x,%#010RX32,): Unsupported version: %#x (%.*Rhxs)\n",
             idTag, offTag, pTag->uVersion, sizeof(*pTag), pTag));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_ISOFS_UNSUPPORTED_TAG_VERSION, "Unsupported descriptor tag version: %#x, expected 2 or 3 (%.*Rhxs)",
                                   pTag->uVersion, sizeof(*pTag), pTag);
    }
    Log(("RTFsUdfHlpValidateDescTag(,%#x,%#010RX32,): checksum error: %#x, calc %#x (%.*Rhxs)\n",
         idTag, offTag, pTag->uChecksum, bChecksum, sizeof(*pTag), pTag));
    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_ISOFS_BAD_TAG_CHECKSUM,
                               "Descriptor tag checksum error: %#x, calculated %#x (%.*Rhxs)",
                               pTag->uChecksum, bChecksum, sizeof(*pTag), pTag);
}


/**
 * Checks the descriptor CRC.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC
 * @retval  VERR_ISOFS_DESC_CRC_MISMATCH
 *
 * @param   pTag        The descriptor buffer to checksum.
 * @param   cbDesc      The size of the descriptor buffer.
 * @param   pErrInfo    Where to return extended error info.
 */
DECLHIDDEN(int) RTFsUdfHlpValidateDescCrc(PCUDFTAG pTag, size_t cbDesc, PRTERRINFO pErrInfo)
{
    if (pTag->cbDescriptorCrc + sizeof(*pTag) <= cbDesc)
    {
        uint16_t uCrc = RTCrc16Ccitt(pTag + 1, pTag->cbDescriptorCrc);
        if (pTag->uDescriptorCrc == uCrc)
            return VINF_SUCCESS;

        Log(("RTFsUdfHlpValidateDescCrc(,%#x,%#010RX32,): Descriptor CRC mismatch: expected %#x, calculated %#x (cbDescriptorCrc=%#x)\n",
             pTag->idTag, pTag->offTag, pTag->uDescriptorCrc, uCrc, pTag->cbDescriptorCrc));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_ISOFS_DESC_CRC_MISMATCH,
                                   "Descriptor CRC mismatch: exepcted %#x, calculated %#x (cbDescriptor=%#x, idTag=%#x, offTag=%#010RX32)",
                                   pTag->uDescriptorCrc, uCrc, pTag->cbDescriptorCrc, pTag->idTag, pTag->offTag);
    }

    Log(("RTFsUdfHlpValidateDescCrc(,%#x,%#010RX32,): Insufficient data to CRC: cbDescriptorCrc=%#x cbDesc=%#zx\n",
         pTag->idTag, pTag->offTag, pTag->cbDescriptorCrc, cbDesc));
    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC,
                               "Insufficient data to CRC: cbDescriptorCrc=%#x cbDesc=%#zx (idTag=%#x, offTag=%#010RX32)",
                               pTag->cbDescriptorCrc, cbDesc, pTag->idTag, pTag->offTag);
}


/**
 * Checks the descriptor tag and CRC.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC
 * @retval  VERR_ISOFS_TAG_IS_ALL_ZEROS
 * @retval  VERR_MISMATCH
 * @retval  VERR_ISOFS_UNSUPPORTED_TAG_VERSION
 * @retval  VERR_ISOFS_TAG_SECTOR_MISMATCH
 * @retval  VERR_ISOFS_BAD_TAG_CHECKSUM
 * @retval  VERR_ISOFS_DESC_CRC_MISMATCH
 *
 * @param   pTag        The descriptor buffer to check the tag of and to
 *                      checksum.
 * @param   cbDesc      The size of the descriptor buffer.
 * @param   idTag       The expected descriptor tag ID, UINT16_MAX
 *                      matches any tag ID.
 * @param   offTag      The sector offset of the tag.
 * @param   pErrInfo    Where to return extended error info.
 */
DECLHIDDEN(int) RTFsUdfHlpValidateDescTagAndCrc(PCUDFTAG pTag, size_t cbDesc, uint16_t idTag, uint32_t offTag, PRTERRINFO pErrInfo)
{
    int rc = RTFsUdfHlpValidateDescTag(pTag, idTag, offTag, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = RTFsUdfHlpValidateDescCrc(pTag, cbDesc, pErrInfo);
    return rc;
}


/**
 * Reads and processes the file set descriptor.
 *
 * @returns IPRT status code.
 * @param   pCtx            The UDF helper context.
 * @param   pVolInfo        Information from the VDS and where to add info from
 *                          the file set descriptor sequence.
 */
static int rtFsUdfProcessFileSetDescSeq(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVOLINFO pVolInfo)
{
    /*
     * We assume there is a single file descriptor and don't bother checking what comes next.
     */
    PUDFFILESETDESC pFsd     = (PUDFFILESETDESC)pCtx->pbBuf;
    Assert(pCtx->cbBuf > sizeof(*pFsd));
    RT_ZERO(*pFsd);
    size_t const    cbToRead = RT_MIN(RT_MAX(pVolInfo->FileSetDescriptor.cb, sizeof(*pFsd)), pCtx->cbBuf);
    int rc = RTFsUdfHlpVpRead(pVolInfo, pCtx->hVfsBacking, pVolInfo->FileSetDescriptor.Location.uPartitionNo,
                              pVolInfo->FileSetDescriptor.Location.off, pFsd, cbToRead);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsUdfHlpValidateDescTagAndCrc(&pFsd->Tag, cbToRead, UDF_TAG_ID_FILE_SET_DESC,
                                             pVolInfo->FileSetDescriptor.Location.off, pCtx->pErrInfo);
        if (RT_SUCCESS(rc))
        {
#ifdef LOG_ENABLED
            Log(("ISO/UDF: File set descriptor at %#RX32 (p%u/%#RX32)\n", pFsd->Tag.offTag,
                 pVolInfo->FileSetDescriptor.Location.uPartitionNo,
                 pVolInfo->FileSetDescriptor.Location.off));
            if (LogIs2Enabled())
            {
                UDF_LOG2_MEMBER_TIMESTAMP(pFsd, RecordingTimestamp);
                UDF_LOG2_MEMBER(pFsd, "#06RX16", uInterchangeLevel);
                UDF_LOG2_MEMBER(pFsd, "#06RX16", uMaxInterchangeLevel);
                UDF_LOG2_MEMBER(pFsd, "#010RX32", fCharacterSets);
                UDF_LOG2_MEMBER(pFsd, "#010RX32", fMaxCharacterSets);
                UDF_LOG2_MEMBER(pFsd, "#010RX32", uFileSetNo);
                UDF_LOG2_MEMBER(pFsd, "#010RX32", uFileSetDescNo);
                UDF_LOG2_MEMBER_CHARSPEC(pFsd, LogicalVolumeIDCharSet);
                UDF_LOG2_MEMBER_DSTRING(pFsd, achLogicalVolumeID);
                UDF_LOG2_MEMBER_CHARSPEC(pFsd, FileSetCharSet);
                UDF_LOG2_MEMBER_DSTRING(pFsd, achFileSetID);
                UDF_LOG2_MEMBER_DSTRING(pFsd, achCopyrightFile);
                UDF_LOG2_MEMBER_DSTRING(pFsd, achAbstractFile);
                UDF_LOG2_MEMBER_LONGAD(pFsd, RootDirIcb);
                UDF_LOG2_MEMBER_ENTITY_ID(pFsd, idDomain);
                UDF_LOG2_MEMBER_LONGAD(pFsd, NextExtent);
                UDF_LOG2_MEMBER_LONGAD(pFsd, SystemStreamDirIcb);
                if (!ASMMemIsZero(&pFsd->abReserved[0], sizeof(pFsd->abReserved)))
                    UDF_LOG2_MEMBER(pFsd, ".32Rhxs", abReserved);
            }
#endif

            /*
             * Do some basic sanity checking.
             */
            if (!UDF_IS_CHAR_SET_OSTA(&pFsd->FileSetCharSet))
                return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_FSD_UNSUPPORTED_CHAR_SET,
                                           "Invalid file set charset %.64Rhxs", &pFsd->FileSetCharSet);
            if (   pFsd->RootDirIcb.cb == 0
                || pFsd->RootDirIcb.uType != UDF_AD_TYPE_RECORDED_AND_ALLOCATED)
                return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_FSD_ZERO_ROOT_DIR,
                                           "Root Dir ICB location is zero or malformed: uType=%#x cb=%#x loc=%#x:%#RX32",
                                           pFsd->RootDirIcb.uType, pFsd->RootDirIcb.cb,
                                           pFsd->RootDirIcb.Location.uPartitionNo, pFsd->RootDirIcb.Location.off);
            if (   pFsd->NextExtent.cb != 0
                && pFsd->NextExtent.uType == UDF_AD_TYPE_RECORDED_AND_ALLOCATED)
                return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_FSD_NEXT_EXTENT,
                                           "NextExtent isn't zero: uType=%#x cb=%#x loc=%#x:%#RX32",
                                           pFsd->NextExtent.uType, pFsd->NextExtent.cb,
                                           pFsd->NextExtent.Location.uPartitionNo, pFsd->NextExtent.Location.off);

            /*
             * Copy the information we need.
             */
            pVolInfo->RootDirIcb         = pFsd->RootDirIcb;
            if (   pFsd->SystemStreamDirIcb.cb > 0
                && pFsd->SystemStreamDirIcb.uType == UDF_AD_TYPE_RECORDED_AND_ALLOCATED)
                pVolInfo->SystemStreamDirIcb = pFsd->SystemStreamDirIcb;
            else
                RT_ZERO(pVolInfo->SystemStreamDirIcb);
            return VINF_SUCCESS;
        }
        return rc;
    }
    return RTERRINFO_LOG_SET(pCtx->pErrInfo, rc, "Error reading file set descriptor");
}


/**
 * Check validatity and extract information from the descriptors in the VDS seq.
 *
 * @returns IPRT status code
 * @param   pCtx            The UDF helper context.
 * @param   pVdsInfo        The gathered temp info from the VDS sequence.
 * @param   pVolInfo        Where to return volume information found in the VDS.
 */
static int rtFsUdfProcessVdsSeqInfo(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVDSINFO pVdsInfo, PRTFSUDFVOLINFO pVolInfo)
{
    /*
     * Check the basic descriptor counts.
     */
    PUDFPRIMARYVOLUMEDESC pPvd;
    if (pVdsInfo->cPrimaryVols == 1)
        pPvd = pVdsInfo->apPrimaryVols[0];
    else
    {
        if (pVdsInfo->cPrimaryVols == 0)
            return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_NO_PVD, "No primary volume descriptor was found");
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_MULTIPLE_PVDS,
                                   "More than one primary volume descriptor was found: %u", pVdsInfo->cPrimaryVols);
    }

    PUDFLOGICALVOLUMEDESC pLvd;
    if (pVdsInfo->cLogicalVols == 1)
        pLvd = pVdsInfo->apLogicalVols[0];
    else
    {
        if (pVdsInfo->cLogicalVols == 0)
            return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_NO_LVD, "No logical volume descriptor was found");
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_MULTIPLE_LVDS,
                                   "More than one logical volume descriptor was found: %u", pVdsInfo->cLogicalVols);
    }

#if 0
    if (pVdsInfo->cPartitions == 0)
        return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_NO_PD, "No partition descriptors was found");
#endif

    /*
     * Check out the partition map in the logical volume descriptor.
     * Produce the mapping table while going about that.
     */
    if (pLvd->cPartitionMaps > 64)
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_TOO_MANY_PART_MAPS,
                                   "Too many partition maps: %u (max 64)", pLvd->cPartitionMaps);

    PRTFSISOVOLUDFPMAP paPartMaps = NULL;
    if (pLvd->cPartitionMaps > 0)
    {
        pVdsInfo->paPartMaps = paPartMaps = (PRTFSISOVOLUDFPMAP)RTMemAllocZ(sizeof(paPartMaps[0]) * pLvd->cPartitionMaps);
        if (!paPartMaps)
            return VERR_NO_MEMORY;
    }
    uint32_t cPartMaps = 0;

    if (pLvd->cbMapTable)
    {
        uint32_t off  = 0;
        while (off + sizeof(UDFPARTMAPHDR) <= pLvd->cbMapTable)
        {
            PCUDFPARTMAPHDR pHdr = (PCUDFPARTMAPHDR)&pLvd->abPartitionMaps[off];

            /*
             * Bounds checking.
             */
            if (off + pHdr->cb > pLvd->cbMapTable)
            {
                if (cPartMaps < pLvd->cbMapTable)
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_MALFORMED_PART_MAP_TABLE,
                                               "Incomplete partition map entry at offset %#x: cb=%#x -> offEnd=%#x cbMapTable=%#x (type=%#x)",
                                               off, pHdr->cb, off + pHdr->cb, pLvd->cbMapTable, pHdr->bType);
                LogRel(("ISO/UDF: Warning: Incomplete partition map entry at offset %#x: cb=%#x -> offEnd=%#x cbMapTable=%#x (type=%#x)\n",
                        off, pHdr->cb, off + pHdr->cb, pLvd->cbMapTable, pHdr->bType));
                break;
            }
            if (cPartMaps >= pLvd->cPartitionMaps)
            {
                LogRel(("ISO/UDF: Warning: LVD::cPartitionMaps is %u but there are more bytes in the table. (off=%#x cb=%#x cbMapTable=%#x bType=%#x)\n",
                        cPartMaps - pLvd->cPartitionMaps, off, pHdr->cb, pLvd->cbMapTable, pHdr->bType));
                break;
            }

            /*
             * Extract relevant info out of the entry.
             */
            paPartMaps[cPartMaps].offMapTable = (uint16_t)off;
            uint16_t uPartitionNo;
            if (pHdr->bType == 1)
            {
                PCUDFPARTMAPTYPE1 pType1 = (PCUDFPARTMAPTYPE1)pHdr;
                paPartMaps[cPartMaps].uVolumeSeqNo = pType1->uVolumeSeqNo;
                paPartMaps[cPartMaps].bType        = RTFSUDF_PMAP_T_PLAIN;
                uPartitionNo = pType1->uPartitionNo;
            }
            else if (pHdr->bType == 2)
            {
                PCUDFPARTMAPTYPE2 pType2 = (PCUDFPARTMAPTYPE2)pHdr;
                if (UDF_ENTITY_ID_EQUALS(&pType2->idPartitionType, UDF_ENTITY_ID_VPM_PARTITION_TYPE))
                {
                    paPartMaps[cPartMaps].bType = pType2->idPartitionType.Suffix.Udf.uUdfRevision >= 0x200
                                                ? RTFSUDF_PMAP_T_VPM_20 : RTFSUDF_PMAP_T_VPM_15;
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_VPM_NOT_SUPPORTED, "Partition type '%.23s' (%#x) not supported",
                                               pType2->idPartitionType.achIdentifier, pType2->idPartitionType.Suffix.Udf.uUdfRevision);
                }
                else if (UDF_ENTITY_ID_EQUALS(&pType2->idPartitionType, UDF_ENTITY_ID_SPM_PARTITION_TYPE))
                {
                    paPartMaps[cPartMaps].bType = RTFSUDF_PMAP_T_SPM;
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_SPM_NOT_SUPPORTED, "Partition type '%.23s' (%#x) not supported",
                                               pType2->idPartitionType.achIdentifier, pType2->idPartitionType.Suffix.Udf.uUdfRevision);
                }
                else if (UDF_ENTITY_ID_EQUALS(&pType2->idPartitionType, UDF_ENTITY_ID_MPM_PARTITION_TYPE))
                {
                    paPartMaps[cPartMaps].bType = RTFSUDF_PMAP_T_MPM;
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_MPM_NOT_SUPPORTED, "Partition type '%.23s' (%#x) not supported",
                                               pType2->idPartitionType.achIdentifier, pType2->idPartitionType.Suffix.Udf.uUdfRevision);
                }
                else
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_UNKNOWN_PART_MAP_TYPE_ID,
                                               "Unknown partition map ID for #%u @ %#x: %.23s",
                                               cPartMaps, off, pType2->idPartitionType.achIdentifier);
#if 0 /* unreachable code */
                paPartMaps[cPartMaps].uVolumeSeqNo = pType2->uVolumeSeqNo;
                uPartitionNo = pType2->uPartitionNo;
#endif
            }
            else
                return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_UNKNOWN_PART_MAP_ENTRY_TYPE,
                                           "Unknown partition map entry type #%u @ %#x: %u", cPartMaps, off, pHdr->bType);
            paPartMaps[cPartMaps].uPartitionNo = uPartitionNo;

            /*
             * Lookup the partition number and retrieve the relevant info from the partition descriptor.
             */
            uint32_t i = pVdsInfo->cPartitions;
            while (i-- > 0)
            {
                PUDFPARTITIONDESC pPd = pVdsInfo->apPartitions[i];
                if (paPartMaps[cPartMaps].uPartitionNo == pPd->uPartitionNo)
                {
                    paPartMaps[cPartMaps].idxPartDesc     = (uint16_t)i;
                    paPartMaps[cPartMaps].cSectors        = pPd->cSectors;
                    paPartMaps[cPartMaps].offLocation     = pPd->offLocation;
                    paPartMaps[cPartMaps].offByteLocation = (uint64_t)pPd->offLocation * pCtx->cbSector;
                    paPartMaps[cPartMaps].fFlags          = pPd->fFlags;
                    paPartMaps[cPartMaps].uAccessType     = pPd->uAccessType;
                    if (!UDF_ENTITY_ID_EQUALS(&pPd->idPartitionContents, UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF_3))
                        paPartMaps[cPartMaps].fHaveHdr    = false;
                    else
                    {
                        paPartMaps[cPartMaps].fHaveHdr    = true;
                        paPartMaps[cPartMaps].Hdr         = pPd->ContentsUse.Hdr;
                    }
                    break;
                }
            }
            if (i > pVdsInfo->cPartitions)
                return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_PARTITION_NOT_FOUND,
                                           "Partition #%u (%#x) specified by mapping entry #%u (@ %#x) was not found! (int-type %u)",
                                           uPartitionNo, uPartitionNo, cPartMaps, off, paPartMaps[cPartMaps].bType);

            /*
             * Advance.
             */
            cPartMaps++;
            off += pHdr->cb;
        }

        if (cPartMaps < pLvd->cPartitionMaps)
            return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_INCOMPLETE_PART_MAP_TABLE,
                                       "Only found %u of the %u announced partition mapping table entries",
                                       cPartMaps, pLvd->cPartitionMaps);
    }

    /* It might be theoretically possible to not use virtual partitions for
       accessing data, so just warn if there aren't any. */
    if (cPartMaps == 0)
        LogRel(("ISO/UDF: Warning: No partition maps!\n"));

    /*
     * Check out the logical volume descriptor.
     */
    if (   pLvd->cbLogicalBlock < pCtx->cbSector
        || pLvd->cbLogicalBlock > pCtx->cbMaxLogicalBlock
        || (pLvd->cbLogicalBlock % pCtx->cbSector) != 0)
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_UNSUPPORTED_LOGICAL_BLOCK_SIZE,
                                   "Logical block size of %#x is not supported with a sector size of %#x",
                                   pLvd->cbLogicalBlock, pCtx->cbSector);

    if (!UDF_ENTITY_ID_EQUALS(&pLvd->idDomain, UDF_ENTITY_ID_LVD_DOMAIN))
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_BAD_LVD_DOMAIN_ID,
                                   "Unsupported domain ID in logical volume descriptor: '%.23s'", pLvd->idDomain.achIdentifier);

    if (   pLvd->ContentsUse.FileSetDescriptor.uType != UDF_AD_TYPE_RECORDED_AND_ALLOCATED
        || pLvd->ContentsUse.FileSetDescriptor.cb    == 0
        || pLvd->ContentsUse.FileSetDescriptor.Location.uPartitionNo >= cPartMaps)
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_BAD_LVD_FILE_SET_DESC_LOCATION,
                                   "Malformed file set descriptor location (type=%u cb=%#x part=%#x)",
                                   pLvd->ContentsUse.FileSetDescriptor.uType,
                                   pLvd->ContentsUse.FileSetDescriptor.cb,
                                   pLvd->ContentsUse.FileSetDescriptor.Location.uPartitionNo);

    bool fLvdHaveVolId = !ASMMemIsZero(pLvd->achLogicalVolumeID, sizeof(pLvd->achLogicalVolumeID));
    if (   fLvdHaveVolId
        && !UDF_IS_CHAR_SET_OSTA(&pLvd->DescCharSet))
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_BAD_LVD_DESC_CHAR_SET,
                                   "Logical volume ID is not using OSTA compressed unicode");

    /*
     * We can ignore much, if not all of the primary volume descriptor.
     */

    /*
     * We're good. So copy over the data.
     */
    pVolInfo->FileSetDescriptor = pLvd->ContentsUse.FileSetDescriptor;
    pVolInfo->cbBlock           = pLvd->cbLogicalBlock;
    pVolInfo->cShiftBlock       = 9;
    while (pVolInfo->cbBlock != RT_BIT_32(pVolInfo->cShiftBlock))
        pVolInfo->cShiftBlock++;
    pVolInfo->fFlags            = pPvd->fFlags;
    pVolInfo->cPartitions       = cPartMaps;
    pVolInfo->paPartitions      = paPartMaps;
    pVdsInfo->paPartMaps = NULL;
    if (fLvdHaveVolId)
        memcpy(pVolInfo->achLogicalVolumeID, pLvd->achLogicalVolumeID, sizeof(pVolInfo->achLogicalVolumeID));
    else
        RT_ZERO(pVolInfo->achLogicalVolumeID);

    return VINF_SUCCESS;
}


/**
 * Processes a primary volume descriptor in the VDS (UDF).
 *
 * @returns IPRT status code.
 * @param   pCtx            The UDF helper context.
 * @param   pVdsInfo        Where to gather temp info from the VDS sequence.
 * @param   pDesc           The descriptor.
 */
//cmd: kmk VBoxRT && kmk_redirect -E VBOX_LOG_DEST="nofile stderr" -E VBOX_LOG="rt_fs=~0" -E VBOX_LOG_FLAGS="unbuffered enabled" -- e:\vbox\svn\trunk\out\win.amd64\debug\bin\tools\RTLs.exe :iprtvfs:file(open,d:\Downloads\en_windows_10_enterprise_version_1703_updated_march_2017_x64_dvd_10189290.iso,r):vfs(isofs):/ -la
static int rtFsUdfProcessPrimaryVolDesc(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVDSINFO pVdsInfo, PCUDFPRIMARYVOLUMEDESC pDesc)
{
#ifdef LOG_ENABLED
    Log(("ISO/UDF: Primary volume descriptor at sector %#RX32\n", pDesc->Tag.offTag));
    if (LogIs2Enabled())
    {
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uVolumeDescSeqNo);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uPrimaryVolumeDescNo);
        UDF_LOG2_MEMBER_DSTRING(pDesc, achVolumeID);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uVolumeSeqNo);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uVolumeSeqNo);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uMaxVolumeSeqNo);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uInterchangeLevel);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uMaxInterchangeLevel);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", fCharacterSets);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", fMaxCharacterSets);
        UDF_LOG2_MEMBER_DSTRING(pDesc, achVolumeSetID);
        UDF_LOG2_MEMBER_CHARSPEC(pDesc, DescCharSet);
        UDF_LOG2_MEMBER_CHARSPEC(pDesc, ExplanatoryCharSet);
        UDF_LOG2_MEMBER_EXTENTAD(pDesc, VolumeAbstract);
        UDF_LOG2_MEMBER_EXTENTAD(pDesc, VolumeCopyrightNotice);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idApplication);
        UDF_LOG2_MEMBER_TIMESTAMP(pDesc, RecordingTimestamp);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idImplementation);
        if (!ASMMemIsZero(&pDesc->abImplementationUse, sizeof(pDesc->abImplementationUse)))
            Log2(("ISO/UDF:   %-32s %.64Rhxs\n", "abReserved[64]:", &pDesc->abImplementationUse[0]));
        UDF_LOG2_MEMBER(pDesc, "#010RX32", offPredecessorVolDescSeq);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", fFlags);
        if (!ASMMemIsZero(&pDesc->abReserved, sizeof(pDesc->abReserved)))
            Log2(("ISO/UDF:   %-32s %.22Rhxs\n", "abReserved[22]:", &pDesc->abReserved[0]));
    }
#endif

    /*
     * Check if this is a new revision of an existing primary volume descriptor.
     */
    PUDFPRIMARYVOLUMEDESC pEndianConvert = NULL;
    uint32_t i = pVdsInfo->cPrimaryVols;
    while (i--> 0)
    {
        if (   memcmp(pDesc->achVolumeID, pVdsInfo->apPrimaryVols[i]->achVolumeID, sizeof(pDesc->achVolumeID)) == 0
            && memcmp(&pDesc->DescCharSet, &pVdsInfo->apPrimaryVols[i]->DescCharSet, sizeof(pDesc->DescCharSet)) == 0)
        {
            if (RT_LE2H_U32(pDesc->uVolumeDescSeqNo) >= pVdsInfo->apPrimaryVols[i]->uVolumeDescSeqNo)
            {
                Log(("ISO/UDF: Primary descriptor prevails over previous! (%u >= %u)\n",
                     RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apPartitions[i]->uVolumeDescSeqNo));
                pEndianConvert = pVdsInfo->apPrimaryVols[i];
                memcpy(pEndianConvert, pDesc, sizeof(*pDesc));
            }
            else
                Log(("ISO/UDF: Primary descriptor has lower sequence number than the previous! (%u < %u)\n",
                     RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apPartitions[i]->uVolumeDescSeqNo));
            break;
        }
    }
    if (i >= pVdsInfo->cPrimaryVols)
    {
        /*
         * It wasn't. Append it.
         */
        i = pVdsInfo->cPrimaryVols;
        if (i < RT_ELEMENTS(pVdsInfo->apPrimaryVols))
        {
            pVdsInfo->apPrimaryVols[i] = pEndianConvert = (PUDFPRIMARYVOLUMEDESC)RTMemDup(pDesc, sizeof(*pDesc));
            if (pEndianConvert)
                pVdsInfo->cPrimaryVols = i + 1;
            else
                return VERR_NO_MEMORY;
            Log2(("ISO/UDF: ++New primary descriptor.\n"));
        }
        else
            return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_TOO_MANY_PVDS, "Encountered too many primary volume descriptors");
    }

#ifdef RT_BIG_ENDIAN
    /*
     * Do endian conversion of the descriptor.
     */
    if (pEndianConvert)
    {
        AssertFailed();
    }
#else
    RT_NOREF(pEndianConvert);
#endif
    return VINF_SUCCESS;
}


/**
 * Processes an logical volume descriptor in the VDS (UDF).
 *
 * @returns IPRT status code.
 * @param   pVdsInfo    Where to gather temp info from the VDS sequence.
 * @param   pDesc       The descriptor.
 * @param   cbSector    The sector size (UDF defines the logical and physical
 *                      sector size to be the same).
 */
static int rtFsUdfProcessLogicalVolumeDesc(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVDSINFO pVdsInfo, PCUDFLOGICALVOLUMEDESC pDesc)
{
#ifdef LOG_ENABLED
    Log(("ISO/UDF: Logical volume descriptor at sector %#RX32\n", pDesc->Tag.offTag));
    if (LogIs2Enabled())
    {
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uVolumeDescSeqNo);
        UDF_LOG2_MEMBER_CHARSPEC(pDesc, DescCharSet);
        UDF_LOG2_MEMBER_DSTRING(pDesc, achLogicalVolumeID);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", cbLogicalBlock);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idDomain);
        if (UDF_ENTITY_ID_EQUALS(&pDesc->idDomain, UDF_ENTITY_ID_LVD_DOMAIN))
            UDF_LOG2_MEMBER_LONGAD(pDesc, ContentsUse.FileSetDescriptor);
        else if (!ASMMemIsZero(&pDesc->ContentsUse.ab[0], sizeof(pDesc->ContentsUse.ab)))
            Log2(("ISO/UDF:   %-32s %.16Rhxs\n", "ContentsUse.ab[16]:", &pDesc->ContentsUse.ab[0]));
        UDF_LOG2_MEMBER(pDesc, "#010RX32", cbMapTable);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", cPartitionMaps);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idImplementation);
        if (!ASMMemIsZero(&pDesc->ImplementationUse.ab[0], sizeof(pDesc->ImplementationUse.ab)))
            Log2(("ISO/UDF:   %-32s\n%.128RhxD\n", "ImplementationUse.ab[128]:", &pDesc->ImplementationUse.ab[0]));
        UDF_LOG2_MEMBER_EXTENTAD(pDesc, IntegritySeqExtent);
        if (pDesc->cbMapTable)
        {
            Log2(("ISO/UDF:   %-32s\n", "abPartitionMaps"));
            uint32_t iMap = 0;
            uint32_t off  = 0;
            while (off + sizeof(UDFPARTMAPHDR) <= pDesc->cbMapTable)
            {
                PCUDFPARTMAPHDR pHdr = (PCUDFPARTMAPHDR)&pDesc->abPartitionMaps[off];
                Log2(("ISO/UDF:     %02u @ %#05x: type %u, length %u\n", iMap, off, pHdr->bType, pHdr->cb));
                if (off + pHdr->cb > pDesc->cbMapTable)
                {
                    Log2(("ISO/UDF:                 BAD! Entry is %d bytes too long!\n", off + pHdr->cb - pDesc->cbMapTable));
                    break;
                }
                if (pHdr->bType == 1)
                {
                    PCUDFPARTMAPTYPE1 pType1 = (PCUDFPARTMAPTYPE1)pHdr;
                    UDF_LOG2_MEMBER_EX(pType1, "#06RX16", uVolumeSeqNo, 5);
                    UDF_LOG2_MEMBER_EX(pType1, "#06RX16", uPartitionNo, 5);
                }
                else if (pHdr->bType == 2)
                {
                    PCUDFPARTMAPTYPE2 pType2 = (PCUDFPARTMAPTYPE2)pHdr;
                    UDF_LOG2_MEMBER_ENTITY_ID_EX(pType2, idPartitionType, 5);
                    UDF_LOG2_MEMBER_EX(pType2, "#06RX16", uVolumeSeqNo, 5);
                    UDF_LOG2_MEMBER_EX(pType2, "#06RX16", uPartitionNo, 5);
                    if (UDF_ENTITY_ID_EQUALS(&pType2->idPartitionType, UDF_ENTITY_ID_SPM_PARTITION_TYPE))
                    {
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#06RX16", Spm.cBlocksPerPacket, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#04RX8", Spm.cSparingTables, 5);
                        if (pType2->u.Spm.bReserved2)
                            UDF_LOG2_MEMBER_EX(&pType2->u, "#04RX8", Spm.bReserved2, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Spm.cbSparingTable, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Spm.aoffSparingTables[0], 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Spm.aoffSparingTables[1], 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Spm.aoffSparingTables[2], 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Spm.aoffSparingTables[3], 5);
                    }
                    else if (UDF_ENTITY_ID_EQUALS(&pType2->idPartitionType, UDF_ENTITY_ID_MPM_PARTITION_TYPE))
                    {
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Mpm.offMetadataFile, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Mpm.offMetadataMirrorFile, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Mpm.offMetadataBitmapFile, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#010RX32", Mpm.cBlocksAllocationUnit, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#06RX16", Mpm.cBlocksAlignmentUnit, 5);
                        UDF_LOG2_MEMBER_EX(&pType2->u, "#04RX8", Mpm.fFlags, 5);
                        if (!ASMMemIsZero(pType2->u.Mpm.abReserved2, sizeof(pType2->u.Mpm.abReserved2)))
                            UDF_LOG2_MEMBER_EX(&pType2->u, ".5Rhxs", Mpm.abReserved2, 5);
                    }
                }
                else
                    Log2(("ISO/UDF:                 BAD! Unknown type!\n"));

                /* advance */
                off += pHdr->cb;
                iMap++;
            }
        }
    }
#endif

    /*
     * Check if this is a newer revision of an existing primary volume descriptor.
     */
    size_t cbDesc = (size_t)pDesc->cbMapTable + RT_UOFFSETOF(UDFLOGICALVOLUMEDESC, abPartitionMaps);
    if (   pDesc->cbMapTable >= (UINT32_MAX >> 1)
        || cbDesc > pCtx->cbSector)
    {
        Log(("ISO/UDF: Logical volume descriptor is too big: %#zx (cbSector=%#x)\n", cbDesc, pCtx->cbSector));
        return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_TOO_BIG_PARTMAP_IN_LVD,
                                   "Logical volume descriptor is too big: %#zx (cbSector=%#x)\n", cbDesc, pCtx->cbSector);
    }

    PUDFLOGICALVOLUMEDESC pEndianConvert = NULL;
    uint32_t i = pVdsInfo->cLogicalVols;
    while (i--> 0)
        if (   memcmp(pDesc->achLogicalVolumeID, pVdsInfo->apLogicalVols[i]->achLogicalVolumeID,
                      sizeof(pDesc->achLogicalVolumeID)) == 0
            && memcmp(&pDesc->DescCharSet, &pVdsInfo->apLogicalVols[i]->DescCharSet,
                      sizeof(pDesc->DescCharSet)) == 0)
        {
            if (RT_LE2H_U32(pDesc->uVolumeDescSeqNo) >= pVdsInfo->apLogicalVols[i]->uVolumeDescSeqNo)
            {
                Log(("ISO/UDF: Logical descriptor prevails over previous! (%u >= %u)\n",
                     RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apLogicalVols[i]->uVolumeDescSeqNo));
                pEndianConvert = (PUDFLOGICALVOLUMEDESC)RTMemDup(pDesc, cbDesc);
                if (!pEndianConvert)
                    return VERR_NO_MEMORY;
                RTMemFree(pVdsInfo->apLogicalVols[i]);
                pVdsInfo->apLogicalVols[i] = pEndianConvert;
            }
            else
                Log(("ISO/UDF: Logical descriptor has lower sequence number than the previous! (%u >= %u)\n",
                     RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apLogicalVols[i]->uVolumeDescSeqNo));
            break;
        }
    if (i >= pVdsInfo->cLogicalVols)
    {
        /*
         * It wasn't. Append it.
         */
        i = pVdsInfo->cLogicalVols;
        if (i < RT_ELEMENTS(pVdsInfo->apLogicalVols))
        {
            pVdsInfo->apLogicalVols[i] = pEndianConvert = (PUDFLOGICALVOLUMEDESC)RTMemDup(pDesc, cbDesc);
            if (pEndianConvert)
                pVdsInfo->cLogicalVols = i + 1;
            else
                return VERR_NO_MEMORY;
            Log2(("ISO/UDF: ++New logical volume descriptor.\n"));
        }
        else
            return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_TOO_MANY_LVDS, "Too many logical volume descriptors");
    }

#ifdef RT_BIG_ENDIAN
    /*
     * Do endian conversion of the descriptor.
     */
    if (pEndianConvert)
    {
        AssertFailed();
    }
#else
    RT_NOREF(pEndianConvert);
#endif
    return VINF_SUCCESS;
}


/**
 * Processes an partition descriptor in the VDS (UDF).
 *
 * @returns IPRT status code.
 * @param   pCtx            The UDF helper context.
 * @param   pVdsInfo        Where to gather temp info from the VDS sequence.
 * @param   pDesc           The descriptor.
 */
static int rtFsUdfProcessPartitionDesc(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVDSINFO pVdsInfo, PCUDFPARTITIONDESC pDesc)
{
#ifdef LOG_ENABLED
    Log(("ISO/UDF: Partition descriptor at sector %#RX32\n", pDesc->Tag.offTag));
    if (LogIs2Enabled())
    {
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uVolumeDescSeqNo);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", fFlags);
        UDF_LOG2_MEMBER(pDesc, "#06RX16", uPartitionNo);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idPartitionContents);
        if (UDF_ENTITY_ID_EQUALS(&pDesc->idPartitionContents, UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF_3))
        {
            UDF_LOG2_MEMBER_SHORTAD(&pDesc->ContentsUse, Hdr.UnallocatedSpaceTable);
            UDF_LOG2_MEMBER_SHORTAD(&pDesc->ContentsUse, Hdr.UnallocatedSpaceBitmap);
            UDF_LOG2_MEMBER_SHORTAD(&pDesc->ContentsUse, Hdr.PartitionIntegrityTable);
            UDF_LOG2_MEMBER_SHORTAD(&pDesc->ContentsUse, Hdr.FreedSpaceTable);
            UDF_LOG2_MEMBER_SHORTAD(&pDesc->ContentsUse, Hdr.FreedSpaceBitmap);
            if (!ASMMemIsZero(&pDesc->ContentsUse.Hdr.abReserved[0], sizeof(pDesc->ContentsUse.Hdr.abReserved)))
                Log2(("ISO/UDF:   %-32s\n%.88RhxD\n", "Hdr.abReserved[88]:", &pDesc->ContentsUse.Hdr.abReserved[0]));
        }
        else if (!ASMMemIsZero(&pDesc->ContentsUse.ab[0], sizeof(pDesc->ContentsUse.ab)))
            Log2(("ISO/UDF:   %-32s\n%.128RhxD\n", "ContentsUse.ab[128]:", &pDesc->ContentsUse.ab[0]));
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uAccessType);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", offLocation);
        UDF_LOG2_MEMBER(pDesc, "#010RX32", cSectors);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idImplementation);
        if (!ASMMemIsZero(&pDesc->ImplementationUse.ab[0], sizeof(pDesc->ImplementationUse.ab)))
            Log2(("ISO/UDF:   %-32s\n%.128RhxD\n", "ImplementationUse.ab[128]:", &pDesc->ImplementationUse.ab[0]));

        if (!ASMMemIsZero(&pDesc->abReserved[0], sizeof(pDesc->abReserved)))
            Log2(("ISO/UDF:   %-32s\n%.156RhxD\n", "ImplementationUse.ab[156]:", &pDesc->abReserved[0]));
    }
#endif

    /*
     * Check if this is a newer revision of an existing primary volume descriptor.
     */
    PUDFPARTITIONDESC pEndianConvert = NULL;
    uint32_t i = pVdsInfo->cPartitions;
    while (i--> 0)
        if (pDesc->uPartitionNo == pVdsInfo->apPartitions[i]->uPartitionNo)
        {
            if (RT_LE2H_U32(pDesc->uVolumeDescSeqNo) >= pVdsInfo->apPartitions[i]->uVolumeDescSeqNo)
            {
                Log(("ISO/UDF: Partition descriptor for part %#u prevails over previous! (%u >= %u)\n",
                     pDesc->uPartitionNo, RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apPartitions[i]->uVolumeDescSeqNo));
                pEndianConvert = pVdsInfo->apPartitions[i];
                memcpy(pEndianConvert, pDesc, sizeof(*pDesc));
            }
            else
                Log(("ISO/UDF: Partition descriptor for part %#u has a lower sequence number than the previous! (%u < %u)\n",
                     pDesc->uPartitionNo, RT_LE2H_U32(pDesc->uVolumeDescSeqNo), pVdsInfo->apPartitions[i]->uVolumeDescSeqNo));
            break;
        }
    if (i >= pVdsInfo->cPartitions)
    {
        /*
         * It wasn't. Append it.
         */
        i = pVdsInfo->cPartitions;
        if (i < RT_ELEMENTS(pVdsInfo->apPartitions))
        {
            pVdsInfo->apPartitions[i] = pEndianConvert = (PUDFPARTITIONDESC)RTMemDup(pDesc, sizeof(*pDesc));
            if (pEndianConvert)
                pVdsInfo->cPartitions = i + 1;
            else
                return VERR_NO_MEMORY;
            Log2(("ISO/UDF: ++New partition descriptor.\n"));
        }
        else
            return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_ISOFS_TOO_MANY_PDS, "Too many physical volume descriptors");
    }

#ifdef RT_BIG_ENDIAN
    /*
     * Do endian conversion of the descriptor.
     */
    if (pEndianConvert)
    {
        AssertFailed();
    }
#else
    RT_NOREF(pEndianConvert);
#endif
    return VINF_SUCCESS;
}


/**
 * Processes an implementation use descriptor in the VDS (UDF).
 *
 * @returns IPRT status code.
 * @param   pVdsInfo    Where to gather temp info from the VDS sequence.
 * @param   pDesc       The descriptor.
 */
static int rtFsUdfProcessImplUseVolDesc(PRTFSUDFVDSINFO pVdsInfo, PCUDFIMPLEMENTATIONUSEVOLUMEDESC pDesc)
{
#ifdef LOG_ENABLED
    Log(("ISO/UDF: Implementation use volume descriptor at sector %#RX32\n", pDesc->Tag.offTag));
    if (LogIs2Enabled())
    {
        UDF_LOG2_MEMBER(pDesc, "#010RX32", uVolumeDescSeqNo);
        UDF_LOG2_MEMBER_ENTITY_ID(pDesc, idImplementation);
        if (UDF_ENTITY_ID_EQUALS(&pDesc->idImplementation, UDF_ENTITY_ID_IUVD_IMPLEMENTATION))
        {
            UDF_LOG2_MEMBER_CHARSPEC(&pDesc->ImplementationUse, Lvi.Charset);
            UDF_LOG2_MEMBER_DSTRING(&pDesc->ImplementationUse, Lvi.achVolumeID);
            UDF_LOG2_MEMBER_DSTRING(&pDesc->ImplementationUse, Lvi.achInfo1);
            UDF_LOG2_MEMBER_DSTRING(&pDesc->ImplementationUse, Lvi.achInfo2);
            UDF_LOG2_MEMBER_DSTRING(&pDesc->ImplementationUse, Lvi.achInfo3);
            UDF_LOG2_MEMBER_ENTITY_ID(&pDesc->ImplementationUse, Lvi.idImplementation);
            if (!ASMMemIsZero(&pDesc->ImplementationUse.Lvi.abUse[0], sizeof(pDesc->ImplementationUse.Lvi.abUse)))
                Log2(("ISO/UDF:   %-32s\n%.128RhxD\n", "Lvi.abUse[128]:", &pDesc->ImplementationUse.Lvi.abUse[0]));
        }
        else if (!ASMMemIsZero(&pDesc->ImplementationUse.ab[0], sizeof(pDesc->ImplementationUse.ab)))
            Log2(("ISO/UDF:   %-32s\n%.460RhxD\n", "ImplementationUse.ab[460]:", &pDesc->ImplementationUse.ab[0]));
    }
#endif

    RT_NOREF(pVdsInfo, pDesc);
    return VINF_SUCCESS;
}


/**
 * Process a VDS sequence, recursively dealing with volume descriptor pointers.
 *
 * This function only gathers information from the sequence, handling the
 * prevailing descriptor fun.
 *
 * @returns IPRT status code.
 * @param   pCtx            The UDF helper context.
 * @param   pVdsInfo        Where to gather temp info from the VDS sequence.
 * @param   offSeq          The byte offset of the sequence.
 * @param   cbSeq           The length of the sequence.
 * @param   cNestings       The VDS nesting depth.
 * @param   pVolInfo        Where to return volume information found in the VDS.
 */
static int rtFsUdfReadAndProcessVdsSeq(PCRTFSUDFHLPCTX pCtx, PRTFSUDFVDSINFO pVdsInfo, uint64_t offSeq, uint32_t cbSeq,
                                       uint32_t cNestings, PRTFSUDFVOLINFO pVolInfo)
{
    Assert(pCtx->cbBuf >= pCtx->cbSector);

    /*
     * Check nesting depth.
     */
    if (cNestings > 5)
        return RTERRINFO_LOG_SET(pCtx->pErrInfo, VERR_TOO_MUCH_DATA, "The volume descriptor sequence (VDS) is nested too deeply.");


    /*
     * Do the processing sector by sector to keep things simple.
     */
    uint32_t offInSeq = 0;
    while (offInSeq < cbSeq)
    {
        int rc;

        /*
         * Read the next sector.  Zero pad if less that a sector.
         */
        Assert((offInSeq & (pCtx->cbSector - 1)) == 0);
        rc = RTVfsFileReadAt(pCtx->hVfsBacking, offSeq + offInSeq, pCtx->pbBuf, pCtx->cbSector, NULL);
        if (RT_FAILURE(rc))
            return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, rc, "Error reading VDS content at %RX64 (LB %#x): %Rrc",
                                       offSeq + offInSeq, pCtx->cbSector, rc);
        if (cbSeq - offInSeq < pCtx->cbSector)
            memset(&pCtx->pbBuf[cbSeq - offInSeq], 0, pCtx->cbSector - (cbSeq - offInSeq));

        /*
         * Check tag.
         */
        PCUDFTAG pTag = (PCUDFTAG)pCtx->pbBuf;
        rc = RTFsUdfHlpValidateDescTagAndCrc(pTag, pCtx->cbSector, UINT16_MAX, (offSeq + offInSeq) / pCtx->cbSector,
                                             pCtx->pErrInfo);
        if (   RT_SUCCESS(rc)
            || (   rc == VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC
                && (   pTag->idTag == UDF_TAG_ID_LOGICAL_VOLUME_INTEGRITY_DESC
                    || pTag->idTag == UDF_TAG_ID_LOGICAL_VOLUME_DESC
                    || pTag->idTag == UDF_TAG_ID_UNALLOCATED_SPACE_DESC
                   )
               )
           )
        {
            switch (pTag->idTag)
            {
                case UDF_TAG_ID_PRIMARY_VOL_DESC:
                    rc = rtFsUdfProcessPrimaryVolDesc(pCtx, pVdsInfo, (PCUDFPRIMARYVOLUMEDESC)pTag);
                    break;

                case UDF_TAG_ID_IMPLEMENTATION_USE_VOLUME_DESC:
                    rc = rtFsUdfProcessImplUseVolDesc(pVdsInfo, (PCUDFIMPLEMENTATIONUSEVOLUMEDESC)pTag);
                    break;

                case UDF_TAG_ID_PARTITION_DESC:
                    rc = rtFsUdfProcessPartitionDesc(pCtx, pVdsInfo, (PCUDFPARTITIONDESC)pTag);
                    break;

                case UDF_TAG_ID_LOGICAL_VOLUME_DESC:
                    if (rc != VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC)
                        rc = rtFsUdfProcessLogicalVolumeDesc(pCtx, pVdsInfo, (PCUDFLOGICALVOLUMEDESC)pTag);
                    else
                        rc = VERR_ISOFS_TOO_BIG_PARTMAP_IN_LVD;
                    break;

                case UDF_TAG_ID_LOGICAL_VOLUME_INTEGRITY_DESC:
                    Log(("ISO/UDF: Ignoring logical volume integrity descriptor at offset %#RX64.\n", offSeq + offInSeq));
                    rc = VINF_SUCCESS;
                    break;

                case UDF_TAG_ID_UNALLOCATED_SPACE_DESC:
                    Log(("ISO/UDF: Ignoring unallocated space descriptor at offset %#RX64.\n", offSeq + offInSeq));
                    rc = VINF_SUCCESS;
                    break;

                case UDF_TAG_ID_ANCHOR_VOLUME_DESC_PTR:
                    Log(("ISO/UDF: Ignoring AVDP in VDS (at offset %#RX64).\n", offSeq + offInSeq));
                    rc = VINF_SUCCESS;
                    break;

                case UDF_TAG_ID_VOLUME_DESC_PTR:
                {
                    PCUDFVOLUMEDESCPTR pVdp = (PCUDFVOLUMEDESCPTR)pTag;
                    Log(("ISO/UDF: Processing volume descriptor pointer at offset %#RX64: %#x LB %#x (seq %#x); cNestings=%d\n",
                         offSeq + offInSeq, pVdp->NextVolumeDescSeq.off, pVdp->NextVolumeDescSeq.cb,
                         pVdp->uVolumeDescSeqNo, cNestings));
                    rc = rtFsUdfReadAndProcessVdsSeq(pCtx, pVdsInfo, (uint64_t)pVdp->NextVolumeDescSeq.off * pCtx->cbSector,
                                                     pVdp->NextVolumeDescSeq.cb, cNestings + 1, pVolInfo);
                    break;
                }

                case UDF_TAG_ID_TERMINATING_DESC:
                    Log(("ISO/UDF: Terminating descriptor at offset %#RX64\n", offSeq + offInSeq));
                    return VINF_SUCCESS;

                default:
                    return RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_ISOFS_UNEXPECTED_VDS_DESC,
                                               "Unexpected/unknown VDS descriptor %#x at byte offset %#RX64",
                                               pCtx->cbSector, offSeq + offInSeq);
            }
            if (RT_FAILURE(rc))
                return rc;
        }
        /* The descriptor sequence is usually zero padded to 16 sectors.  Just
           ignore zero descriptors. */
        else if (rc != VERR_ISOFS_TAG_IS_ALL_ZEROS)
            return rc;

        /*
         * Advance.
         */
        offInSeq += pCtx->cbSector;
    }

    return VINF_SUCCESS;
}



/**
 * Processes a volume descriptor sequence (VDS) and file set descriptor sequence
 * (FSDS).
 *
 * @returns IPRT status code.
 * @param   pCtx            The UDF helper context.
 * @param   offSeq          The byte offset of the sequence.
 * @param   cbSeq           The length of the sequence.
 * @param   pSeenSequences  Structure where to keep track of VDSes we've already
 *                          processed, to avoid redoing one that we don't
 *                          understand.
 * @param   pVolInfo        Where to return volume information found in the VDS.
 */
static int rtFsUdfReadAndProcessVdsAndFsds(PCRTFSUDFHLPCTX pCtx, uint64_t offSeq, uint32_t cbSeq,
                                           PRTFSUDFHLPSEENSEQENCES pSeenSequences, PRTFSUDFVOLINFO pVolInfo)
{
    /*
     * Skip if already seen.
     */
    uint32_t i = pSeenSequences->cSequences;
    while (i-- > 0)
        if (   pSeenSequences->aSequences[i].off == offSeq
            && pSeenSequences->aSequences[i].cb  == cbSeq)
            return VERR_NOT_FOUND;

    /* Not seen, so add it. */
    AssertReturn(pSeenSequences->cSequences + 1 <= RT_ELEMENTS(pSeenSequences->aSequences), VERR_INTERNAL_ERROR_3);
    pSeenSequences->aSequences[pSeenSequences->cSequences].cb = cbSeq;
    pSeenSequences->aSequences[pSeenSequences->cSequences].off = offSeq;
    pSeenSequences->cSequences++;

    LogFlow(("ISO/UDF: Processing anchor volume descriptor sequence at offset %#RX64 LB %#RX32\n", offSeq, cbSeq));

    /*
     * Gather relevant descriptor info from the VDS then process it and on
     * success copy it into the instance.
     *
     * The processing has to be done in a different function because there may
     * be links to sub-sequences that needs to be processed.  We do this by
     * recursing and check that we don't go to deep.
     */
    RTFSUDFVDSINFO VdsInfo = {0};

    int rc = rtFsUdfReadAndProcessVdsSeq(pCtx, &VdsInfo, offSeq, cbSeq, 0 /*cNestings*/, pVolInfo);
    if (RT_SUCCESS(rc))
    {
        rc = rtFsUdfProcessVdsSeqInfo(pCtx, &VdsInfo, pVolInfo);
        if (RT_SUCCESS(rc))
            rc = rtFsUdfProcessFileSetDescSeq(pCtx, pVolInfo);
    }

    /*
     * Clean up info.
     */
    i = VdsInfo.cPrimaryVols;
    while (i-- > 0)
        RTMemFree(VdsInfo.apPrimaryVols[i]);

    i = VdsInfo.cLogicalVols;
    while (i-- > 0)
        RTMemFree(VdsInfo.apLogicalVols[i]);

    i = VdsInfo.cPartitions;
    while (i-- > 0)
        RTMemFree(VdsInfo.apPartitions[i]);

    RTMemFree(VdsInfo.paPartMaps);

    return rc;
}


DECLHIDDEN(int) RTFsUdfHlpReadAndHandleUdfAvdp(PCRTFSUDFHLPCTX pCtx, uint64_t offAvdp, PRTFSUDFHLPSEENSEQENCES pSeenSequences,
                                               PRTFSUDFVOLINFO pVolInfo)
{
    AssertReturn(pCtx->cbSector          >= 512, VERR_INVALID_PARAMETER);
    AssertReturn(pCtx->cbMaxLogicalBlock >= pCtx->cbSector, VERR_INVALID_PARAMETER);
    AssertReturn(pCtx->cbBuf             >= pCtx->cbMaxLogicalBlock, VERR_INVALID_PARAMETER);

    /*
     * Try read the descriptor and validate its tag.
     */
    PUDFANCHORVOLUMEDESCPTR const pAvdp = (PUDFANCHORVOLUMEDESCPTR)pCtx->pbBuf;
    int rc = RTVfsFileReadAt(pCtx->hVfsBacking, offAvdp, pAvdp, pCtx->cbSector, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsUdfHlpValidateDescTagAndCrc(&pAvdp->Tag, pCtx->cbSector, UDF_TAG_ID_ANCHOR_VOLUME_DESC_PTR,
                                             offAvdp / pCtx->cbSector, pCtx->pErrInfo);
        if (RT_SUCCESS(rc))
        {
            UDFEXTENTAD const aVds[2] = { pAvdp->MainVolumeDescSeq, pAvdp->ReserveVolumeDescSeq };
            Log2(("ISO/UDF: AVDP: MainVolumeDescSeq=%#RX32 LB %#RX32, ReserveVolumeDescSeq=%#RX32 LB %#RX32\n",
                  aVds[0].off, aVds[0].cb, aVds[1].off, aVds[1].cb));

            for (unsigned i = 0; i < RT_ELEMENTS(aVds); i++)
            {
                /* Try the sequence if it looks sane. */
                if (   aVds[i].off < pCtx->cBackingSectors
                    &&     (uint64_t)aVds[i].off
                         + ((uint64_t)aVds[i].cb + pCtx->cbSector - 1) / pCtx->cbSector
                       <= pCtx->cBackingSectors)
                {
                    rc = rtFsUdfReadAndProcessVdsAndFsds(pCtx, (uint64_t)aVds[i].off * pCtx->cbSector, aVds[i].cb,
                                                         pSeenSequences, pVolInfo);
                    if (RT_SUCCESS(rc))
                        return rc;
                }
                else if (RT_SUCCESS(rc))
                    rc = RTERRINFO_LOG_SET_F(pCtx->pErrInfo, VERR_NOT_FOUND,
                                             "%sVolumeDescSeq is out of bounds: sector %#RX32 LB %#RX32 bytes, image is %#RX64 sectors",
                                             i ? "Reserve" : "Main", aVds[i].off, aVds[i].cb, pCtx->cBackingSectors);
            }
        }
    }
    else
        rc = RTERRINFO_LOG_SET_F(pCtx->pErrInfo, rc,
                                 "Error reading sector at offset %#RX64 (anchor volume descriptor pointer): %Rrc", offAvdp, rc);

    return rc;
}


/**
 * Cleans up after RTFsUdfHlpReadAndHandleUdfAvdp.
 */
DECLHIDDEN(void) RTFsUdfHlpDestroyVolInfo(PRTFSUDFVOLINFO pVolInfo)
{
    if (pVolInfo->paPartitions)
        RTMemFree(pVolInfo->paPartitions);
    pVolInfo->paPartitions = NULL;
}

