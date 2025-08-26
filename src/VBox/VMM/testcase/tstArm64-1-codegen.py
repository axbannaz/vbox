#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id$
# pylint: disable=invalid-name

"""
tstArm64-1 code generator.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2025 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision$"

# pylint: enable=invalid-name

# Standard python imports.
import argparse;
import random;
import os;
import sys;


# Imports from the VMMAll directory.
sys.path.append(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'VMMAll'));
import PyCommonVmm as pycmn;    # pylint: disable=import-error


#
# Utility functions.
#

def isPowerOfTwo(uValue):
    """ Check if 'uValue' is a power of two. """
    return (uValue & (uValue - 1)) == 0 and uValue != 0;

def bitsOnes(cBits):
    """ Returns a value with 'cBits' ones in it. """
    return (1<<cBits) - 1;

def bitsReplicate(cBits, uValue, cTimes):
    """ Return the 'uValue' ('cBits' wide) replicated 'cTimes' times. """
    uRet = uValue;
    while cTimes > 1:
        cTimes -= 1;
        uRet <<= cBits;
        uRet  |= uValue;
    return uRet;

def bitsRor(cBits, uValue, cShift):
    """ Rotates 'uValue' ('cBits' wide) 'cShift' times to the right. """
    assert isPowerOfTwo(cBits);
    cShift &= cBits - 1;
    fMask   = bitsOnes(cBits);
    return ((uValue & fMask) >> cShift) | ((uValue << (cBits - cShift)) & fMask);

## The random number generator we use.
g_oRandom = random.Random();

def randBool():
    return g_oRandom.randrange(0, 2) == 1;

def randU8():
    return g_oRandom.randrange(0, 1<<8);

def randU16():
    return g_oRandom.randrange(0, 1<<16);

def randU32():
    return g_oRandom.randrange(0, 1<<32);

def randU64():
    return g_oRandom.randrange(0, 1<<64);

def randUx(iLast):
    return g_oRandom.randrange(0, iLast+1);

def randUBits(cBits):
    return g_oRandom.randrange(0, 1 << cBits);


#
# Register allocators.
#

class Arm64RegAllocator(object):
    """ This is mainly for random register allocating. """
    def __init__(self, oParent = None, cMax = 32):
        self.cMax        = cMax;
        self.bmAllocated = 0;
        self.auValues    = [None for _ in range(cMax)];
        if oParent:
            self.bmAllocated |= oParent.bmAllocated;
            # Note! Value tracking in the parent will be broken ...
            for i, uValue in enumerate(oParent.auValues):
                if uValue is not None:
                    if i < cMax:
                        self.auValues[i] = uValue;
                    oParent.auValues[i] = None;

    def alloc(self, uValue = None, fIncludingReg31 = False):
        """ Allocates a random register. """
        iReg      = randUx(self.cMax - 1);
        fRegMask  = 1 << iReg;
        iStartReg = iReg;
        while (self.bmAllocated & fRegMask) or (iReg == 31 and not fIncludingReg31):
            iReg = (iReg + 1) % self.cMax;
            assert iReg != iStartReg;
            fRegMask = 1 << iReg;

        self.bmAllocated   |= fRegMask;
        self.auValues[iReg] = uValue;
        return iReg;

    def allocFixed(self, iReg, uValue = None):
        """ Allocates a specific register. """
        assert 0 <= iReg < self.cMax;
        fRegMask = 1 << iReg;
        assert not (self.bmAllocated & fRegMask);
        self.bmAllocated   |= fRegMask;
        self.auValues[iReg] = uValue;
        return iReg;

    def updateValue(self, iReg, uValue):
        """ Updates the value of an allocated register. """
        assert self.bmAllocated & (1 << iReg);
        self.auValues[iReg] = uValue;
        return uValue;

    def free(self, iReg):
        """ Frees a previously allocated register. """
        if iReg != -1:
            assert 0 <= iReg < self.cMax;
            fRegMask = 1 << iReg;
            assert self.bmAllocated & fRegMask;
            self.bmAllocated &= ~fRegMask;
        return None;

    def freeList(self, aiRegs):
        for iReg in aiRegs:
            self.free(iReg);
        return None;


class Arm64GprAllocator(Arm64RegAllocator):
    """ GPR allocator.  This excludes the SP/XZR register. """

    def __init__(self, oParent = None):
        Arm64RegAllocator.__init__(self, oParent = oParent, cMax = 32);
        #self.bmAllocated |= 1 << 31; # SP/XZR
        self.bmAllocated |= 1 << 18; # PR - platform register.

g_dGpr64NamesZr = {
    0:  'x0',       1:  'x1',       2:  'x2',       3:  'x3',       4:  'x4',       5:  'x5',       6:  'x6',       7:  'x7',
    8:  'x8',       9:  'x9',       10: 'x10',      11: 'x11',      12: 'x12',      13: 'x13',      14: 'x14',      15: 'x15',
    16: 'x16',      17: 'x17',      18: 'x18',      19: 'x19',      20: 'x20',      21: 'x21',      22: 'x22',      23: 'x23',
    24: 'x24',      25: 'x25',      26: 'x26',      27: 'x27',      28: 'x28',      29: 'x29',      30: 'x30',      31: 'xzr',
};
g_dGpr32NamesZr = {
    0:  'w0',       1:  'w1',       2:  'w2',       3:  'w3',       4:  'w4',       5:  'w5',       6:  'w6',       7:  'w7',
    8:  'w8',       9:  'w9',       10: 'w10',      11: 'w11',      12: 'w12',      13: 'w13',      14: 'w14',      15: 'w15',
    16: 'w16',      17: 'w17',      18: 'w18',      19: 'w19',      20: 'w20',      21: 'w21',      22: 'w22',      23: 'w23',
    24: 'w24',      25: 'w25',      26: 'w26',      27: 'w27',      28: 'w28',      29: 'w29',      30: 'w30',      31: 'wzr',
};
g_ddGprNamesZrByBits = { 64: g_dGpr64NamesZr, 32: g_dGpr32NamesZr };

g_dGpr64NamesSp = {
    0:  'x0',       1:  'x1',       2:  'x2',       3:  'x3',       4:  'x4',       5:  'x5',       6:  'x6',       7:  'x7',
    8:  'x8',       9:  'x9',       10: 'x10',      11: 'x11',      12: 'x12',      13: 'x13',      14: 'x14',      15: 'x15',
    16: 'x16',      17: 'x17',      18: 'x18',      19: 'x19',      20: 'x20',      21: 'x21',      22: 'x22',      23: 'x23',
    24: 'x24',      25: 'x25',      26: 'x26',      27: 'x27',      28: 'x28',      29: 'x29',      30: 'x30',      31: 'sp',
};
g_dGpr32NamesSp = {
    0:  'w0',       1:  'w1',       2:  'w2',       3:  'w3',       4:  'w4',       5:  'w5',       6:  'w6',       7:  'w7',
    8:  'w8',       9:  'w9',       10: 'w10',      11: 'w11',      12: 'w12',      13: 'w13',      14: 'w14',      15: 'w15',
    16: 'w16',      17: 'w17',      18: 'w18',      19: 'w19',      20: 'w20',      21: 'w21',      22: 'w22',      23: 'w23',
    24: 'w24',      25: 'w25',      26: 'w26',      27: 'w27',      28: 'w28',      29: 'w29',      30: 'w30',      31: 'wsp',
};
g_ddGprNamesSpByBits = { 64: g_dGpr64NamesSp, 32: g_dGpr32NamesSp };

g_dddGprNamesBySpAndBits = {
    True:  g_ddGprNamesSpByBits,
    False: g_ddGprNamesZrByBits,
};


class Arm64FpRegAllocator(Arm64RegAllocator):
    """ FPU/SIMD allocator. """

    def __init__(self, oParent = None):
        Arm64RegAllocator.__init__(self, oParent = oParent, cMax = 32);



#
# Code generators
#

## Immediate value for BRK. Incremented for each instruction we emit.
g_iBrkNo = 1;
## Immediate value for BRK. Incremented for each instruction we emit.
g_iLabel = 100;

## Counters for each instruction test name used to generate unique label names.
g_dNameSeqNumbers = {}

class A64No1CodeGenBase(object):

    def __init__(self, sName, sInstr, oGprAllocator, fMayUseSp = False):
        assert isinstance(oGprAllocator, Arm64GprAllocator)
        self.sName         = sName;
        self.sInstr        = sInstr;
        self.oGprAllocator = oGprAllocator;
        self.iRegDataPtr   = oGprAllocator.alloc();
        self.fMayUseSp     = fMayUseSp;
        self.asCode        = [];        # Assembly code lines.
        self.asData        = [];        # Assembly data lines.
        self.cbLastData    = -1;
        self.cLastDataItems = 0;

        self.sLabel        = sName;
        if sName not in g_dNameSeqNumbers:
            g_dNameSeqNumbers[sName] = 1;
        else:
            self.sLabel    = '%s_%u' % (sName, g_dNameSeqNumbers[sName]);
            g_dNameSeqNumbers[sName] += 1;


    def generate(self, oOptions):
        self.asData += [
            'g_DataStart_%s: /* %s / %s */' % (self.sLabel, self.sName, self.sInstr),
        ];
        self.asCode += [
            '/* %s / %s */' % (self.sName, self.sInstr,),
            'BEGINPROC %s' % (self.sLabel,),
        ];
        self.emitInstr('stp',   'fp, lr, [sp, #-64]!');
        if self.fMayUseSp:
            self.asCode.append('/* Save SP as it may be used in the test: */');
            self.emitInstr('adrp',  'x%u, PAGE(g_u64SavedSp)' % (self.iRegDataPtr,));
            self.emitInstr('add',   'x%u, x%u, PAGEOFF(g_u64SavedSp)' % (self.iRegDataPtr, self.iRegDataPtr,));
            iTmp = self.oGprAllocator.alloc();
            self.emitInstr('mov',   'x%u, sp' % (iTmp,));
            self.emitInstr('str',   'x%u, [x%u]' % (iTmp, self.iRegDataPtr));
            self.oGprAllocator.free(iTmp);
            self.asCode.append('');

        self.emitInstr('adrp',  'x%u, PAGE(g_DataStart_%s)' % (self.iRegDataPtr, self.sLabel,));
        self.emitInstr('add',   'x%u, x%u, PAGEOFF(g_DataStart_%s)' % (self.iRegDataPtr, self.iRegDataPtr, self.sLabel,));
        self.asCode.append('');

        self.generateBody(oOptions, oOptions.cCheckAllRegsInterval);

        self.asCode.append('');
        if self.fMayUseSp:
            self.asCode.append('/* Restore SP: */');
            self.emitInstr('adrp',  'x%u, PAGE(g_u64SavedSp)' % (self.iRegDataPtr,));
            self.emitInstr('add',   'x%u, x%u, PAGEOFF(g_u64SavedSp)' % (self.iRegDataPtr, self.iRegDataPtr,));
            self.emitInstr('ldr',   'x%u, [x%u]' % (self.iRegDataPtr, self.iRegDataPtr));
            self.emitInstr('mov',   'sp, x%u' % (self.iRegDataPtr,));
        self.asCode.append('');
        self.emitInstr('ldp',   'fp, lr, [sp], #64');
        self.emitInstr('ret',   'lr');
        self.asCode.append('ENDPROC %s' % (self.sLabel,));


    def generateBody(self, oOptions, cLeftToAllCheck):
        _ = oOptions; _ = cLeftToAllCheck;
        return None;

    def emitInstr(self, sInstr, sOperands, sComment = None):
        sTmp = '        %-8s %s' % (sInstr, sOperands,);
        if sComment:
            sTmp = '%-47s /* %s */' % (sTmp, sComment,);
        self.asCode.append(sTmp);

    def emitLabel(self, sLabel):
        self.asCode.append('%s:' % (sLabel,));

    def emitRegValCheck(self, iRegToCheck, uExpectedValue, iRegTmp = -1, fReg31IsSp = False):
        """ Emits a register value check. """
        global g_iBrkNo, g_iLabel;

        sRegToCheck = g_dddGprNamesBySpAndBits[fReg31IsSp][64][iRegToCheck];
        sCmpForceExtended = ', UXTX' if iRegToCheck == 31 and fReg31IsSp else '';

        if uExpectedValue == 0 and not fReg31IsSp:
            self.emitInstr('cbz',   '%s, L%s' % (sRegToCheck, g_iLabel,));
        else:
            if uExpectedValue < 0x1000:
                self.emitInstr('cmp',   '%s, #0x%x' % (sRegToCheck, uExpectedValue,));
            elif iRegTmp >= 0:
                self.genGprLoad(iRegTmp, uExpectedValue);
                self.emitInstr('cmp',   '%s, x%u%s' % (sRegToCheck, iRegTmp, sCmpForceExtended,));
            else:
                iRegTmp = self.oGprAllocator.alloc(uExpectedValue);
                self.genGprLoad(iRegTmp, uExpectedValue);
                self.emitInstr('cmp',   '%s, x%u%s' % (sRegToCheck, iRegTmp, sCmpForceExtended,));
                self.oGprAllocator.free(iRegTmp);
            self.emitInstr('b.eq',  'L%s' % (g_iLabel, ));
        self.emitInstr('brk',   '#%#x' % (g_iBrkNo & 0xffff, ));
        self.emitLabel('L%s' % (g_iLabel, ));
        g_iLabel += 1;
        g_iBrkNo += 1;
        return None;

    def emitFlagsCheck(self, fExpectedNzcv, iRegTmp = -1):
        """ Emits a NZCV flags check. """
        assert iRegTmp != 31; # 0x06b53736 + #0xe40 => 0x0000000006b54576 + flags=0x20001000
        iRegTmpToUse = iRegTmp if iRegTmp >= 0 else self.oGprAllocator.alloc(fExpectedNzcv);
        self.emitInstr('mrs',   'x%u, NZCV' % (iRegTmpToUse,));
        self.emitRegValCheck(iRegTmpToUse, fExpectedNzcv);
        if iRegTmpToUse != iRegTmp:
            self.oGprAllocator.free(iRegTmpToUse);
        return None;

    def emitAllGprChecks(self):
        """ Check the content of all GPRs with a known value. """
        self.asCode.append('        /* Start all GPR check: */');
        iRegTmp  = self.oGprAllocator.alloc();
        cChecked = 0;
        for iReg, uValue in enumerate(self.oGprAllocator.auValues):
            if uValue is not None and iReg != 31:
                self.emitRegValCheck(iReg, uValue, iRegTmp = iRegTmp);
                cChecked += 1;
        self.oGprAllocator.free(iRegTmp);
        self.asCode.append('        /* End all GPR check (%u regs checked) */' % (cChecked,));
        return None;

    def maybeEmitAllGprChecks(self, cLeft, oOptions):
        if cLeft > 0:
            return cLeft - 1;
        self.emitAllGprChecks();
        return oOptions.cCheckAllRegsInterval;


    def genGprLoad(self, iReg, uValue, fReg31IsSp = True):
        """
        Generates loading uValue into GPR 'iReg'.
        Returns iReg.
        """
        assert uValue >= 0;
        assert iReg != 31 or fReg31IsSp;

        # Allocate temp register if we're loading SP as it's difficult to do in a generic manner.
        iRegToLoad = iReg;
        if iReg == 31 and fReg31IsSp:
            iRegToLoad = self.oGprAllocator.alloc(uValue);

        ## @todo this can be compacted more!              0xffffffffffffffff
        if 0 <= uValue <= 0xffff:
            self.emitInstr('movz',  'x%u, #0x%x' % (iRegToLoad, uValue));
        elif 0 <= uValue <= 0xffffffff:
            self.emitInstr('ldr',   'w%u, [x%u], #4' % (iRegToLoad, self.iRegDataPtr), '%#x' % (uValue,));
            if self.cbLastData == 4 and self.cLastDataItems < 9:
                self.asData[-1] += ', 0x%08x' % (uValue,);
                self.cLastDataItems += 1;
            else:
                self.asData.append('        .int    0x%08x' % (uValue,));
                self.cbLastData     = 4;
                self.cLastDataItems = 1;
        else:
            self.emitInstr('ldr',   'x%u, [x%u], #8' % (iRegToLoad, self.iRegDataPtr), '%#x' % (uValue,));
            if self.cbLastData == 8 and self.cLastDataItems < 5:
                self.asData[-1] += ', 0x%016x' % (uValue,);
                self.cLastDataItems += 1;
            else:
                self.asData.append('        .quad   0x%016x' % (uValue,));
                self.cbLastData     = 8;
                self.cLastDataItems = 1;

        # SP hack:
        if iReg != iRegToLoad:
            self.emitInstr('mov', 'sp, %s' % (g_dGpr64NamesZr[iRegToLoad],));
            self.oGprAllocator.free(iRegToLoad);

        self.oGprAllocator.updateValue(iReg, uValue);
        return iReg;

    def allocGprAndLoadRandUBits(self, cBits = 64, fIncludingReg31 = True, fReg31IsSp = False):
        """
        Allocates a register and load a random value into them.
        return (uValue, iReg)
        """
        uValue = randUBits(cBits);
        iReg = self.oGprAllocator.alloc(uValue, fIncludingReg31 = fIncludingReg31);
        if iReg != 31 or fReg31IsSp:
            self.genGprLoad(iReg, uValue, fReg31IsSp = fReg31IsSp);
        else:
            uValue = 0;
            assert fIncludingReg31;
        return (uValue, iReg);


#
# The code generators for specific instructions.
#

class A64No1CodeGenAddSubImm(A64No1CodeGenBase):
    """
    C4.1.92.3 addsub_imm

    All variants take SP as input register, but only the non-flag variants
    can use SP a the target register (the flag ones targets XZR in order to
    implement CMP and such).
    """
    def __init__(self, sInstr, fnCalc, fWithFlags = False):
        A64No1CodeGenBase.__init__(self, sInstr + '_imm', sInstr, Arm64GprAllocator(), fMayUseSp = True);
        self.fnCalc      = fnCalc;
        self.fWithFlags  = fWithFlags;
        self.fWithSpDst  = not fWithFlags;

    def generateBody(self, oOptions, cLeftToAllCheck):
        for cBits in (32, 64,):
            for _ in range(oOptions.cTestsPerInstruction):
                (uVal1, iRegIn1) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True, fReg31IsSp = True);
                uVal2   = randUBits(12);

                fShift = randBool();
                uVal2Shifted = uVal2 << 12 if fShift else uVal2;

                uRes, fNzcv = self.fnCalc(cBits, uVal1, uVal2Shifted, 0);
                iRegDst = self.oGprAllocator.alloc(uRes, fIncludingReg31 = True);

                self.emitInstr(self.sInstr,
                               '%s, %s, #0x%x%s' % (g_dddGprNamesBySpAndBits[self.fWithSpDst][cBits][iRegDst],
                                                    g_ddGprNamesSpByBits[cBits][iRegIn1], uVal2, ', LSL #12' if fShift else '',));
                if self.fWithFlags:
                    self.emitFlagsCheck(fNzcv);
                if iRegDst != 31 or self.fWithSpDst:
                    self.emitRegValCheck(iRegDst, uRes, fReg31IsSp = self.fWithSpDst);

                self.oGprAllocator.freeList((iRegIn1, iRegDst,));
                cLeftToAllCheck = self.maybeEmitAllGprChecks(cLeftToAllCheck, oOptions);


class A64No1CodeGenShiftedReg(A64No1CodeGenBase):
    """
    C4.1.94.3 log_shift
    C4.1.94.4 addsub_shift

    None of these instructions operates of SP.
    """
    def __init__(self, sInstr, fnCalc, fWithFlags = False, fWithRor = False):
        A64No1CodeGenBase.__init__(self, sInstr + '_shifted_reg', sInstr, Arm64GprAllocator());
        self.fnCalc      = fnCalc;
        self.fWithFlags  = fWithFlags;
        self.cShiftTypes = 4 if fWithRor else 3;

    kdShiftNm = {0: 'LSL', 1: 'LSR', 2: 'ASR', 3: 'ROR',}

    @staticmethod
    def shiftRegValue(uValue, bShiftType, cShift, cBits):
        fMask = (1 << cBits) - 1;
        if bShiftType == 0:     # LSL
            uValue <<= cShift;
        elif bShiftType == 1:   # LSR
            uValue  &= fMask;
            uValue >>= cShift;
        elif bShiftType == 2:   # ASR
            fOrMask  = 0;
            if (uValue >> (cBits - 1)) & 1:
                fOrMask = 0xffffffffffffffff << (cBits - cShift);
            uValue  &= fMask;
            uValue >>= cShift;
            uValue  |= fOrMask;
        else:                   # ROR
            assert bShiftType == 3;
            uValue  &= fMask;
            uValue   = (uValue >> cShift) | (uValue << (cBits - cShift));
        return uValue & fMask;

    def generateBody(self, oOptions, cLeftToAllCheck):
        for cBits in (32, 64,):
            for i in range(oOptions.cTestsPerInstruction):
                (uVal1, iRegIn1) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);
                (uVal2, iRegIn2) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);

                bShiftType = randU8() % self.cShiftTypes;
                cShift     = randU8() % cBits if i & 1 else 0;
                uVal2Shifted = self.shiftRegValue(uVal2, bShiftType, cShift, cBits);

                uRes, fNzcv = self.fnCalc(cBits, uVal1, uVal2Shifted, 0);
                iRegDst = self.oGprAllocator.alloc(uRes, fIncludingReg31 = True);

                self.emitInstr(self.sInstr, '%s, %s, %s, %s #%u'
                               % (g_ddGprNamesZrByBits[cBits][iRegDst], g_ddGprNamesZrByBits[cBits][iRegIn1],
                                  g_ddGprNamesZrByBits[cBits][iRegIn2], self.kdShiftNm[bShiftType], cShift,));
                if self.fWithFlags:
                    self.emitFlagsCheck(fNzcv);
                if iRegDst != 31:
                    self.emitRegValCheck(iRegDst, uRes);

                self.oGprAllocator.freeList((iRegIn1, iRegIn2, iRegDst,));
                cLeftToAllCheck = self.maybeEmitAllGprChecks(cLeftToAllCheck, oOptions);


class A64No1CodeGenExtendedReg(A64No1CodeGenBase):
    """
    C4.1.94.5 addsub_ext

    All variants take SP as input register, but only the non-flag variants
    can use SP a the target register (the flag ones targets XZR in order to
    implement CMP and such).
    """
    def __init__(self, sInstr, fnCalc, fWithFlags = False):
        A64No1CodeGenBase.__init__(self, sInstr + '_shifted_reg', sInstr, Arm64GprAllocator(), fMayUseSp = True);
        self.fnCalc      = fnCalc;
        self.fWithFlags  = fWithFlags;
        self.fWithSpDst  = not fWithFlags;

    kdOp = { 0: 'UXTB', 1: 'UXTH', 2: 'UXTW', 3: 'UXTX', 4: 'SXTB', 5: 'SXTH', 6: 'SXTW', 7: 'SXTX', };

    @staticmethod
    def extendAndShift(uValue, bOpType, cShift, cBits):
        if   bOpType == 0:  # UXTB
            uValue &= 0xff;
        elif bOpType == 1:  # UXTH
            uValue &= 0xffff;
        elif bOpType == 2:  # UXTW
            uValue &= 0xffffffff;
        elif bOpType == 3:  # UXTX
            pass;
        elif bOpType == 4:  # SXTB
            uValue &= 0xff;
            if uValue & 0x80:
                uValue |= 0xffffffffffffff00;
        elif bOpType == 5:  # SXTH
            uValue &= 0xffff;
            if uValue & 0x8000:
                uValue |= 0xffffffffffff0000;
        elif bOpType == 6:  # SXTW
            uValue &= 0xffffffff;
            if uValue & 0x80000000:
                uValue |= 0xffffffff00000000;
        elif bOpType == 7:  # SXTX
            pass;
        else:
            assert False, str(bOpType);
        uValue <<= cShift;
        return uValue & ((1 << cBits) - 1);

    def generateBody(self, oOptions, cLeftToAllCheck):
        for cBits in (32, 64,):
            for i in range(oOptions.cTestsPerInstruction):
                (uVal1, iRegIn1) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True, fReg31IsSp = True);
                (uVal2, iRegIn2) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True, fReg31IsSp = False);

                bOpType = randUBits(3);
                cShift  = randUBits(2) if i & 1 else 0;
                uVal2Shifted = self.extendAndShift(uVal2, bOpType, cShift, cBits);
                cBitsRegIn2  = cBits;
                if cBits == 64 and bOpType not in (3, 7):
                    cBitsRegIn2 = 32;

                uRes, fNzcv = self.fnCalc(cBits, uVal1, uVal2Shifted, 0);
                iRegDst = self.oGprAllocator.alloc(uRes, fIncludingReg31 = True);

                self.emitInstr(self.sInstr, '%s, %s, %s, %s #%u'
                               % (g_dddGprNamesBySpAndBits[self.fWithSpDst][cBits][iRegDst], g_ddGprNamesSpByBits[cBits][iRegIn1],
                                  g_ddGprNamesZrByBits[cBitsRegIn2][iRegIn2], self.kdOp[bOpType], cShift,));
                if self.fWithFlags:
                    self.emitFlagsCheck(fNzcv);
                if iRegDst != 31 or self.fWithSpDst:
                    self.emitRegValCheck(iRegDst, uRes, fReg31IsSp = self.fWithSpDst);

                self.oGprAllocator.freeList((iRegIn1, iRegIn2, iRegDst,));
                cLeftToAllCheck = self.maybeEmitAllGprChecks(cLeftToAllCheck, oOptions);


class A64No1CodeGenAddSubCarry(A64No1CodeGenBase):
    """
    C4.1.94.6 addsub_carry

    None of these instructions operates of SP.
    """
    def __init__(self, sInstr, fnCalc, fWithFlags = False):
        A64No1CodeGenBase.__init__(self, sInstr + '_carry', sInstr, Arm64GprAllocator());
        self.fnCalc      = fnCalc;
        self.fWithFlags  = fWithFlags;

    def generateBody(self, oOptions, cLeftToAllCheck):
        for cBits in (32, 64,):
            fSignMax = (1 << (cBits - 1)) - 1
            aFixed   = (
                (0, fSignMax, 1),
                (0, fSignMax, 0),
                (fSignMax, 0, 1),
                (fSignMax, 0, 0),
                (fSignMax, fSignMax, 1),
                (fSignMax, fSignMax, 0),
                (0, 0, 1),
                (0, 0, 0)
            );
            for i in range(oOptions.cTestsPerInstruction + len(aFixed)):
                if i < len(aFixed):
                    (uVal1, uVal2, fCarry) = aFixed[i];
                    iRegIn1 = self.genGprLoad(self.oGprAllocator.alloc(uVal1, fIncludingReg31 = False), uVal1);
                    iRegIn2 = self.genGprLoad(self.oGprAllocator.alloc(uVal2, fIncludingReg31 = False), uVal2);
                else:
                    (uVal1, iRegIn1) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);
                    (uVal2, iRegIn2) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);
                    fCarry           = randUBits(1);

                if fCarry:
                    self.emitInstr('subs', 'wzr, wzr, wzr', 'clears carry'); # N=0, Z=1, C=1, V=0
                else:
                    self.emitInstr('adds', 'wzr, wzr, wzr', 'sets carry');   # N=0, Z=1, C=0, V=0

                uRes, fNzcv = self.fnCalc(cBits, uVal1, uVal2, fCarry);
                iRegDst = self.oGprAllocator.alloc(uRes, fIncludingReg31 = True);

                self.emitInstr(self.sInstr, '%s, %s, %s'
                               % (g_ddGprNamesZrByBits[cBits][iRegDst], g_ddGprNamesZrByBits[cBits][iRegIn1],
                                  g_ddGprNamesZrByBits[cBits][iRegIn2],));
                if self.fWithFlags:
                    self.emitFlagsCheck(fNzcv);
                if iRegDst != 31:
                    self.emitRegValCheck(iRegDst, uRes);

                self.oGprAllocator.freeList((iRegIn1, iRegIn2, iRegDst,));
                cLeftToAllCheck = self.maybeEmitAllGprChecks(cLeftToAllCheck, oOptions);


class A64No1CodeGenBitfieldMove(A64No1CodeGenBase):
    """
    C4.1.92.8 bitfield

    None of these instructions operates of SP.
    """
    def __init__(self, sInstr, fnCalc):
        A64No1CodeGenBase.__init__(self, sInstr + '_carry', sInstr, Arm64GprAllocator());
        self.fnCalc      = fnCalc;

    def generateBody(self, oOptions, cLeftToAllCheck):
        for cBits in (32, 64,):
            for _ in range(oOptions.cTestsPerInstruction):
                (uSrc, iRegIn)  = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);
                (uDst, iRegDst) = self.allocGprAndLoadRandUBits(fIncludingReg31 = True);
                uImmR           = randUBits(5 if cBits == 32 else 6);
                uImmS           = randUBits(5 if cBits == 32 else 6);

                uRes = self.fnCalc(cBits, uDst, uSrc, uImmR, uImmS);

                self.emitInstr(self.sInstr, '%s, %s, #0x%x, #0x%x'
                               % (g_ddGprNamesZrByBits[cBits][iRegDst], g_ddGprNamesZrByBits[cBits][iRegIn],
                                  uImmR, uImmS,));
                if iRegDst != 31:
                    self.oGprAllocator.updateValue(iRegDst, uRes);
                    self.emitRegValCheck(iRegDst, uRes);

                self.oGprAllocator.freeList((iRegIn, iRegDst,));
                cLeftToAllCheck = self.maybeEmitAllGprChecks(cLeftToAllCheck, oOptions);

def decodeBitMasks(uImmR, fN, uImmS, cBits, fImmediate = False): # pylint:disable=invalid-name
    uTmp = ((fN & 1) << 6) | (~uImmS & 0x3f);
    cBitsLength = uTmp.bit_length() - 1;
    assert cBitsLength >= 0;

    cLevels = bitsOnes(cBitsLength);
    assert cLevels < cBits, 'r=%#x N=%u s=%#x bits=%u uTmp=%#x: cLevels=%u' % (uImmR, fN, uImmS, cBits, uTmp, cLevels,);
    assert not fImmediate or (uImmS & cLevels) != cLevels;

    uImmS &= cLevels;
    uImmR &= cLevels;
    uDiff  = (uImmS - uImmR) & cLevels;

    cBitsElement = 1 << cBitsLength;

    fWElem   = bitsOnes(uImmS + 1);
    fTopElem = bitsOnes(uDiff + 1);

    fWMask   = bitsReplicate(cBitsElement, bitsRor(cBitsElement, fWElem, uImmR), cBits // cBitsElement);
    fTopMask = bitsReplicate(cBitsElement, fTopElem,                             cBits // cBitsElement);
    return (fWMask, fTopMask)

def calcBfm(cBits, uDst, uSrc, uImmR, uImmS):
    (fWMask, fTopMask) = decodeBitMasks(uImmR, 1 if cBits == 64 else 0,  uImmS, cBits);
    fBottom = (uDst & ~fWMask) | (bitsRor(cBits, uSrc, uImmR) & fWMask);
    return (uDst & ~fTopMask & bitsOnes(cBits)) | (fBottom & fTopMask);

def calcSbfm(cBits, _, uSrc, uImmR, uImmS):
    (fWMask, fTopMask) = decodeBitMasks(uImmR, 1 if cBits == 64 else 0,  uImmS, cBits);
    fBottom = bitsRor(cBits, uSrc, uImmR) & fWMask;
    fTop    = bitsReplicate(1, (uSrc >> uImmS) & 1, cBits);
    return (fTop & ~fTopMask) | (fBottom & fTopMask);

def calcUbfm(cBits, _, uSrc, uImmR, uImmS):
    (fWMask, fTopMask) = decodeBitMasks(uImmR, 1 if cBits == 64 else 0,  uImmS, cBits);
    fBottom = bitsRor(cBits, uSrc, uImmR) & fWMask;
    return fBottom & fTopMask;


#
# Result calculation functions.
#

# Helpers for result calculations:
def flagsToMask(fNegative, fZero, fCarry, fOverflow):
    """ Returns the PSTATE.NZCV value for the 4 status flags. """
    return (fNegative << 31) | (fZero << 30) | (fCarry << 29) | (fOverflow << 28);


# calc result of addition and subtraction:
def calcAdd(cBits, uVal1, uVal2, fCarry):
    fMask      = (1 << cBits) - 1;
    uVal1     &= fMask;
    uVal2     &= fMask;

    uResultRaw = uVal1 + uVal2 + fCarry;
    uResult    = uResultRaw & fMask;

    fNeg       = (uResult >> (cBits - 1)) & 1;
    fZero      = 0 if uResult else 1;
    fCarry     = 1 if uResultRaw > uResult else 0;
    fOverflow  = ((~(uVal1 ^ uVal2) & (uResult ^ uVal1)) >> (cBits - 1)) & 1;
    return (uResult, flagsToMask(fNeg, fZero, fCarry, fOverflow));

def calcSub(cBits, uVal1, uVal2, _):
    fMask      = (1 << cBits) - 1;
    uVal1     &= fMask;
    uVal2     &= fMask;

    uResultRaw = uVal1 - uVal2;
    uResult    = uResultRaw & fMask;

    fNeg       = (uResult >> (cBits - 1)) & 1;
    fZero      = 0 if uResult else 1;
    fCarry     = 0 if uVal1 < uVal2 else 1;
    fOverflow  = (((uVal1 ^ uVal2) & (uResult ^ uVal1)) >> (cBits - 1)) & 1;
    return (uResult, flagsToMask(fNeg, fZero, fCarry, fOverflow));

def calcSubBorrow(cBits, uVal1, uVal2, fCarry):
    fBorrow    = 0 if fCarry else 1;
    fMask      = (1 << cBits) - 1;
    uVal1     &= fMask;
    uVal2     &= fMask;

    uResultRaw = uVal1 - uVal2 - fBorrow;
    uResult    = uResultRaw & fMask;

    fNeg       = (uResult >> (cBits - 1)) & 1;
    fZero      = 0 if uResult else 1;
    fCarry     = 0 if uVal1 < uVal2 + fBorrow else 1;
    fOverflow  = (((uVal1 ^ uVal2) & (uResult ^ uVal1)) >> (cBits - 1)) & 1;
    return (uResult, flagsToMask(fNeg, fZero, fCarry, fOverflow));


# Calc result of logical/bitwise operation:
def calcBitwiseCommon(cBits, uResult):
    uResult &= (1 << cBits) - 1;
    return (uResult, flagsToMask((uResult >> (cBits - 1)) & 1, 0 if uResult else 1, 0, 0));

def calcAnd(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 & uVal2);

def calcBic(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 & ~uVal2);

def calcOrr(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 | uVal2);

def calcOrn(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 | ~uVal2);

def calcEor(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 ^ uVal2);

def calcEon(cBits, uVal1, uVal2, _):
    return calcBitwiseCommon(cBits, uVal1 ^ ~uVal2);


#
# Main class.
#

class Arm64No1CodeGen(object):
    """ Main code generator class. """

    def __init__(self):
        self.oOptions = None;


    def main(self, asArgs):
        """ Main function. """
        oArgParser = argparse.ArgumentParser();
        oArgParser.add_argument('--output',
                                metavar = 'file',
                                dest    = 'sFileOut',
                                action  = 'store',
                                default = None,
                                help    = 'The output file.');

        oArgParser.add_argument('--seed',
                                metavar = 'value',
                                dest    = 'iRandSeed',
                                action  = 'store',
                                default = None,
                                type    = lambda s: int(s, 0),
                                help    = 'The random seed to use.');

        oArgParser.add_argument('--tests-per-instruction',
                                metavar = 'value',
                                dest    = 'cTestsPerInstruction',
                                action  = 'store',
                                default = 256,
                                type    = lambda s: int(s, 0),
                                choices = range(1, 16384),
                                help    = 'Number of tests per instruction.');

        oArgParser.add_argument('--check-all-regs-interval',
                                metavar = 'value',
                                dest    = 'cCheckAllRegsInterval',
                                action  = 'store',
                                default = None,
                                type    = lambda s: int(s, 0),
                                choices = range(1, 16384),
                                help    = 'Number of tests between checking all registers. Zero to disable. '
                                          'Defaults to 1/4 of --tests-per-instruction with a minimum of 8.');



        # Do it!
        oOptions = oArgParser.parse_args(asArgs[1:]);
        self.oOptions = oOptions;
        if oOptions.cCheckAllRegsInterval is None:
            oOptions.cCheckAllRegsInterval = min(max(oOptions.cTestsPerInstruction / 4, 8), oOptions.cTestsPerInstruction);

        # See random.
        if oOptions.iRandSeed in (None, 0):
            oOptions.iRandSeed = sum((int(bVal) << (iByte * 8)) for iByte, bVal in enumerate(os.urandom(7)))
        g_oRandom.seed(oOptions.iRandSeed);
        print('info: Using seed %#x' % (oOptions.iRandSeed,));

        # Instantiate the generators.
        aoGenerators = [
            # addsub_imm:
            A64No1CodeGenAddSubImm(  'add',  calcAdd),
            A64No1CodeGenAddSubImm(  'adds', calcAdd, fWithFlags = True),
            A64No1CodeGenAddSubImm(  'sub',  calcSub),
            A64No1CodeGenAddSubImm(  'subs', calcSub, fWithFlags = True),
            # addsub_shift:
            A64No1CodeGenShiftedReg( 'add',  calcAdd),
            A64No1CodeGenShiftedReg( 'adds', calcAdd, fWithFlags = True),
            A64No1CodeGenShiftedReg( 'sub',  calcSub),
            A64No1CodeGenShiftedReg( 'subs', calcSub, fWithFlags = True),
            # addsub_ext
            A64No1CodeGenExtendedReg('add',  calcAdd),
            A64No1CodeGenExtendedReg('adds', calcAdd, fWithFlags = True),
            A64No1CodeGenExtendedReg('sub',  calcSub),
            A64No1CodeGenExtendedReg('subs', calcSub, fWithFlags = True),
            # addsub_carry:
            A64No1CodeGenAddSubCarry('adc',  calcAdd),
            A64No1CodeGenAddSubCarry('adcs', calcAdd, fWithFlags = True),
            A64No1CodeGenAddSubCarry('sbc',  calcSubBorrow),
            A64No1CodeGenAddSubCarry('sbcs', calcSubBorrow, fWithFlags = True),
            # log_shift:
            A64No1CodeGenShiftedReg( 'and',  calcAnd, fWithRor = True),
            A64No1CodeGenShiftedReg( 'ands', calcAnd, fWithRor = True, fWithFlags = True),
            A64No1CodeGenShiftedReg( 'bic',  calcBic, fWithRor = True),
            A64No1CodeGenShiftedReg( 'bics', calcBic, fWithRor = True, fWithFlags = True),
            A64No1CodeGenShiftedReg( 'orr',  calcOrr, fWithRor = True),
            A64No1CodeGenShiftedReg( 'orn',  calcOrn, fWithRor = True),
            A64No1CodeGenShiftedReg( 'eor',  calcEor, fWithRor = True),
            A64No1CodeGenShiftedReg( 'eon',  calcEon, fWithRor = True),
            # bitfield:
            A64No1CodeGenBitfieldMove('bfm',  calcBfm),
            A64No1CodeGenBitfieldMove('sbfm', calcSbfm),
            A64No1CodeGenBitfieldMove('ubfm', calcUbfm),
        ];


        #
        # Produce the file.
        #
        asLines = [
            '/* Automatically Generated. Do not edit! Seed: %#x */' % (oOptions.iRandSeed,),
            '#include <iprt/asmdefs-arm.h>',
            '',
            '',
            '',
            '/*',
            ' * Globals',
            ' */',
            'BEGINDATA',
            'g_u64SavedSp: .quad 0',
            '',
            '',
            '',
            'BEGINCODE',
            '',
            '/*',
            ' * Top level dispatcher.',
            ' */',
            'BEGINPROC GeneratedTests',
            '        stp     fp, lr, [sp, #-64]!',
        ];
        for oGenerator in aoGenerators:
            asLines.append('        bl      NAME(%s)' % (oGenerator.sName,));
        asLines += [
            '        ldp     fp, lr, [sp], #64',
            '        ret',
            'ENDPROC   GeneratedTests',
        ];

        for oGenerator in aoGenerators:
            asLines.extend(('','','',));
            oGenerator.generate(self.oOptions);
            asLines.extend(oGenerator.asCode);
            asLines.extend(('','',));
            asLines.extend(oGenerator.asData);

        if self.oOptions.sFileOut:
            with open(self.oOptions.sFileOut, 'wt', encoding = 'utf-8', errors = 'strict') as oFile:
                oFile.write('\n'.join(asLines));
                oFile.write('\n');
        else:
            sys.stdout.write('\n'.join(asLines));
            sys.stdout.write('\n');

        return 0;




if __name__ == '__main__':
    sys.exit(pycmn.mainWrapperCatchXcptAndDoProfiling(Arm64No1CodeGen().main));

