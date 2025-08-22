#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id$

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

# pylint: disable=too-many-lines

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

## The random number generator we use.
g_oRandom = random.Random();

def randU8():
    return g_oRandom.randrange(0, 0xff);

def randU16():
    return g_oRandom.randrange(0, 0xffff);

def randU32():
    return g_oRandom.randrange(0, 0xffffffff);

def randU64():
    return g_oRandom.randrange(0, 0xffffffffffffffff);

def randUx(iLast):
    return g_oRandom.randrange(0, iLast);

g_dBitsToLast = {
    8:  0xff,
    16: 0xffff,
    32: 0xffffffff,
    64: 0xffffffffffffffff,
}
def randUBits(cBits):
    return g_oRandom.randrange(0, g_dBitsToLast[cBits]);


#
# Register allocators.
#

class Arm64RegAllocator(object):
    """ This is mainly for random register allocating. """
    def __init__(self, oParent = None, cMax = 32):
        self.cMax        = cMax;
        self.bmAllocated = 0;
        if oParent:
            self.bmAllocated |= oParent.bmAllocated;

    def alloc(self, fIncludingReg31 = False):
        """ Allocates a random register. """
        iReg      = randUx(self.cMax - 1);
        fRegMask  = 1 << iReg;
        iStartReg = iReg;
        while (self.bmAllocated & fRegMask) or (iReg == 31 and not fIncludingReg31):
            iReg = (iReg + 1) % self.cMax;
            assert iReg != iStartReg;
            fRegMask = 1 << iReg;

        self.bmAllocated |= fRegMask;
        return iReg;

    def allocFixed(self, iReg):
        """ Allocates a specific register. """
        assert 0 <= iReg < self.cMax;
        fRegMask = 1 << iReg;
        assert not (self.bmAllocated & fRegMask);
        self.bmAllocated |= fRegMask;
        return iReg;

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

    def __init__(self, sName, sInstr, oGprAllocator):
        assert isinstance(oGprAllocator, Arm64GprAllocator)
        self.sName         = sName;
        self.sInstr        = sInstr;
        self.oGprAllocator = oGprAllocator;
        self.iRegDataPtr   = oGprAllocator.alloc();
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
        self.emitInstr('adrp',  'x%u, PAGE(g_DataStart_%s)' % (self.iRegDataPtr, self.sLabel,));
        self.emitInstr('add',   'x%u, x%u, PAGEOFF(g_DataStart_%s)' % (self.iRegDataPtr, self.iRegDataPtr, self.sLabel,));
        self.asCode.append('');

        self.generateBody(oOptions);

        self.asCode.append('');
        self.emitInstr('ldp',   'fp, lr, [sp], #64');
        self.emitInstr('ret',   'lr');
        self.asCode.append('ENDPROC %s' % (self.sLabel,));


    def generateBody(self, oOptions):
        _ = oOptions;
        return None;

    def emitInstr(self, sInstr, sOperands, sComment = None):
        sTmp = '        %-8s %s' % (sInstr, sOperands,);
        if sComment:
            sTmp = '%-47s /* %s */' % (sTmp, sComment,);
        self.asCode.append(sTmp);

    def emitLabel(self, sLabel):
        self.asCode.append('%s:' % (sLabel,));

    def emitRegValCheck(self, iRegToCheck, uExpectedValue, cBits = 64, iRegTmp = -1):
        """ Emits a register check value. """
        global g_iBrkNo, g_iLabel;
        if uExpectedValue == 0:
            self.emitInstr('cbz',   'x%u, L%s' % (iRegToCheck, g_iLabel,));
        else:
            if uExpectedValue < 0x1000:
                self.emitInstr('cmp',   'x%u, #0x%x' % (iRegToCheck, uExpectedValue,));
            elif iRegTmp >= 0:
                self.genGprLoad(iRegTmp, uExpectedValue);
                self.emitInstr('cmp',   'x%u, x%u' % (iRegToCheck, iRegTmp,));
            else:
                iRegTmp = self.oGprAllocator.alloc();
                self.genGprLoad(iRegTmp, uExpectedValue);
                self.emitInstr('cmp',   'x%u, x%u' % (iRegToCheck, iRegTmp,));
                self.oGprAllocator.free(iRegTmp);
            self.emitInstr('b.eq',  'L%s' % (g_iLabel, ));
        self.emitInstr('brk',   '#%#x' % (g_iBrkNo & 0xffff, ));
        self.emitLabel('L%s' % (g_iLabel, ));
        g_iLabel += 1;
        g_iBrkNo += 1;
        return None;

    def genGprLoad(self, iReg, uValue):
        """
        Generates loading uValue into GPR 'iReg'.
        Returns iReg.
        """
        assert uValue >= 0;
        ## @todo this can be compacted more!              0xffffffffffffffff
        if 0 <= uValue <= 0xffff:
            self.emitInstr('movz',  'x%u, #0x%x' % (iReg, uValue));
        elif 0 <= uValue <= 0xffffffff:
            self.emitInstr('ldr',   'w%u, [x%u], #4' % (iReg, self.iRegDataPtr), '%#x' % (uValue,));
            if self.cbLastData == 4 and self.cLastDataItems < 9:
                self.asData[-1] += ', 0x%08x' % (uValue,);
                self.cLastDataItems += 1;
            else:
                self.asData.append('        .int    0x%08x' % (uValue,));
                self.cbLastData     = 4;
                self.cLastDataItems = 1;
        else:
            self.emitInstr('ldr',   'x%u, [x%u], #8' % (iReg, self.iRegDataPtr), '%#x' % (uValue,));
            if self.cbLastData == 8 and self.cLastDataItems < 5:
                self.asData[-1] += ', 0x%016x' % (uValue,);
                self.cLastDataItems += 1;
            else:
                self.asData.append('        .quad   0x%016x' % (uValue,));
                self.cbLastData     = 8;
                self.cLastDataItems = 1;

        return iReg;

    def allocGprAndLoadRandUBits(self, cBits, fIncludingReg31 = True):
        """
        Allocates a register and load a random value into them.
        return (uValue, iReg)
        """
        iReg = self.oGprAllocator.alloc(fIncludingReg31);
        if iReg != 31:
            uValue = randUBits(cBits);
            self.genGprLoad(iReg, uValue);
        else:
            uValue = 0;
            assert fIncludingReg31;
        return (uValue, iReg);

    @staticmethod
    def flagsToMask(fNegative, fZero, fCarry, fOverflow):
        """ Returns the PSTATE.NZCV value for the 4 status flags. """
        return (fNegative << 31) | (fZero << 30) | (fCarry << 29) | (fOverflow << 28);


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
        if bShiftType == 0:     # LSL
            uValue <<= cShift;
        elif bShiftType == 1:   # LSR
            uValue >>= cShift;
        elif bShiftType == 2:   # ASR
            fOrMask = 0;
            if (uValue >> (cBits - 1)) & 1:
                fOrMask = 0xffffffffffffffff << (cBits - cShift);
            uValue >>= cShift;
            uValue  |= fOrMask;
        else:                   # ROR
            assert bShiftType == 3;
            uValue = (uValue >> cShift) | (uValue << (cBits - cShift));
        return uValue & ((1 << cBits) - 1);

    def generateBody(self, oOptions):
        for cBits, chRegPfx in ((32, 'w'), (64, 'x'),):
            for i in range(oOptions.cTestsPerInstruction):
                (uVal1, iRegIn1) = self.allocGprAndLoadRandUBits(cBits, fIncludingReg31 = True);
                (uVal2, iRegIn2) = self.allocGprAndLoadRandUBits(cBits, fIncludingReg31 = True);
                iRegDst = self.oGprAllocator.alloc(fIncludingReg31 = True);

                bShiftType = randU8() % self.cShiftTypes;
                cShift     = randU8() % cBits if i & 1 else 0;
                uVal2Shifted = self.shiftRegValue(uVal2, bShiftType, cShift, cBits);

                uRes, fNzcv = self.fnCalc(cBits, uVal1, uVal2Shifted, 0);

                self.emitInstr(self.sInstr, '%s%u, %s%u, %s%u, %s #%u'
                               % (chRegPfx, iRegDst, chRegPfx, iRegIn1, chRegPfx, iRegIn2, self.kdShiftNm[bShiftType], cShift,));
                iRegTmp = self.oGprAllocator.alloc();
                if self.fWithFlags:
                    self.emitInstr('mrs',   'x%u, NZCV' % (iRegTmp,));
                    self.emitRegValCheck(iRegTmp, fNzcv, cBits);
                if iRegDst != 31:
                    self.emitRegValCheck(iRegDst, uRes, cBits, iRegTmp);

                self.oGprAllocator.freeList((iRegIn1, iRegIn2, iRegDst, iRegTmp,));

    kdMasks = { 32: 0xffffffff, 64: 0xffffffffffffffff };

    @staticmethod
    def calcAdd(cBits, uVal1, uVal2, fCarry):
        uResultRaw = uVal1 + uVal2 + fCarry;
        uResult    = uResultRaw & A64No1CodeGenShiftedReg.kdMasks[cBits];
        fNeg       = (uResult >> (cBits - 1)) & 1;
        fZero      = 0 if uResult else 1;
        fCarry     = 1 if uResultRaw > uResult else 0;
        fOverflow  = ((~(uVal1 ^ uVal2) & (uResult ^ uVal1)) >> (cBits - 1)) & 1;
        return (uResult, A64No1CodeGenBase.flagsToMask(fNeg, fZero, fCarry, fOverflow));

    @staticmethod
    def calcSub(cBits, uVal1, uVal2, fBorrow):
        uResultRaw = uVal1 - uVal2 - fBorrow;
        uResult    = uResultRaw & A64No1CodeGenShiftedReg.kdMasks[cBits];
        fNeg       = (uResult >> (cBits - 1)) & 1;
        fZero      = 0 if uResult else 1;
        fCarry     = 0 if uVal1 < uVal2 + fBorrow else 1;
        fOverflow  = (((uVal1 ^ uVal2) & (uResult ^ uVal1)) >> (cBits - 1)) & 1;
        return (uResult, A64No1CodeGenBase.flagsToMask(fNeg, fZero, fCarry, fOverflow));


    @staticmethod
    def calcBitwiseCommon(cBits, uResult):
        uResult &= A64No1CodeGenShiftedReg.kdMasks[cBits];
        return (uResult, A64No1CodeGenBase.flagsToMask((uResult >> (cBits - 1)) & 1, 0 if uResult else 1, 0, 0));

    @staticmethod
    def calcAnd(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 & uVal2);

    @staticmethod
    def calcBic(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 & ~uVal2);

    @staticmethod
    def calcOrr(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 | uVal2);

    @staticmethod
    def calcOrn(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 | ~uVal2);

    @staticmethod
    def calcEor(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 ^ uVal2);

    @staticmethod
    def calcEon(cBits, uVal1, uVal2, _):
        return A64No1CodeGenShiftedReg.calcBitwiseCommon(cBits, uVal1 ^ ~uVal2);



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
                                default = 128,
                                type    = lambda s: int(s, 0),
                                help    = 'Number of tests per instruction.');

        # Do it!
        oOptions = oArgParser.parse_args(asArgs[1:]);
        self.oOptions = oOptions;

        # See random.
        if oOptions.iRandSeed in (None, 0):
            oOptions.iRandSeed = sum((int(bVal) << (iByte * 8)) for iByte, bVal in enumerate(os.urandom(7)))
        g_oRandom.seed(oOptions.iRandSeed);
        print('info: Using seed %#x' % (oOptions.iRandSeed,));

        # Instantiate the generators.
        aoGenerators = [
            # addsub_shift:
            A64No1CodeGenShiftedReg('add',  A64No1CodeGenShiftedReg.calcAdd),
            A64No1CodeGenShiftedReg('adds', A64No1CodeGenShiftedReg.calcAdd, fWithFlags = True),
            A64No1CodeGenShiftedReg('sub',  A64No1CodeGenShiftedReg.calcSub),
            A64No1CodeGenShiftedReg('subs', A64No1CodeGenShiftedReg.calcSub, fWithFlags = True),
            # log_shift:
            A64No1CodeGenShiftedReg('and',  A64No1CodeGenShiftedReg.calcAnd, fWithRor = True),
            A64No1CodeGenShiftedReg('ands', A64No1CodeGenShiftedReg.calcAnd, fWithRor = True, fWithFlags = True),
            A64No1CodeGenShiftedReg('bic',  A64No1CodeGenShiftedReg.calcBic, fWithRor = True),
            A64No1CodeGenShiftedReg('bics', A64No1CodeGenShiftedReg.calcBic, fWithRor = True, fWithFlags = True),
            A64No1CodeGenShiftedReg('orr',  A64No1CodeGenShiftedReg.calcOrr, fWithRor = True),
            A64No1CodeGenShiftedReg('orn',  A64No1CodeGenShiftedReg.calcOrn, fWithRor = True),
            A64No1CodeGenShiftedReg('eor',  A64No1CodeGenShiftedReg.calcEor, fWithRor = True),
            A64No1CodeGenShiftedReg('eon',  A64No1CodeGenShiftedReg.calcEon, fWithRor = True),
        ]


        #
        # Produce the file.
        #
        oFile = sys.stdout;
        if self.oOptions.sFileOut:
            oFile = open(self.oOptions.sFileOut, 'wt');

        oFile.write('/* Automatically Generated. Do not edit! Seed: %#x */\n' % (oOptions.iRandSeed,));
        oFile.write('#include <iprt/asmdefs-arm.h>\n');

        # Generate top level function.
        oFile.write('\n'
                    '\n'
                    '\n'
                    'BEGINPROC GeneratedTests\n'
                    '        stp     fp, lr, [sp, #-64]!\n');
        for oGenerator in aoGenerators:
            oFile.write('        bl      NAME(%s)\n' % (oGenerator.sName,));
        oFile.write('        ldp     fp, lr, [sp], #64\n'
                    '        ret\n'
                    'ENDPROC   GeneratedTests\n'
                    '\n');

        for oGenerator in aoGenerators:
            oGenerator.generate(self.oOptions);
            oFile.write('\n');
            oFile.write('\n');
            oFile.write('\n'.join(oGenerator.asCode));
            oFile.write('\n');
            oFile.write('\n');
            oFile.write('\n'.join(oGenerator.asData));
            oFile.write('\n');

        if self.oOptions.sFileOut:
            oFile.close();

        return 0;




if __name__ == '__main__':
    sys.exit(pycmn.mainWrapperCatchXcptAndDoProfiling(Arm64No1CodeGen().main));

