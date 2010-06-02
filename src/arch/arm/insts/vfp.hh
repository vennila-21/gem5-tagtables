/*
 * Copyright (c) 2010 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Gabe Black
 */

#ifndef __ARCH_ARM_INSTS_VFP_HH__
#define __ARCH_ARM_INSTS_VFP_HH__

#include "arch/arm/insts/misc.hh"

enum VfpMicroMode {
    VfpNotAMicroop,
    VfpMicroop,
    VfpFirstMicroop,
    VfpLastMicroop
};

template<class T>
static inline void
setVfpMicroFlags(VfpMicroMode mode, T &flags)
{
    switch (mode) {
      case VfpMicroop:
        flags[StaticInst::IsMicroop] = true;
        break;
      case VfpFirstMicroop:
        flags[StaticInst::IsMicroop] =
            flags[StaticInst::IsFirstMicroop] = true;
        break;
      case VfpLastMicroop:
        flags[StaticInst::IsMicroop] =
            flags[StaticInst::IsLastMicroop] = true;
        break;
      case VfpNotAMicroop:
        break;
    }
    if (mode == VfpMicroop || mode == VfpFirstMicroop) {
        flags[StaticInst::IsDelayedCommit] = true;
    }
}

class VfpMacroOp : public PredMacroOp
{
  public:
    static bool
    inScalarBank(IntRegIndex idx)
    {
        return (idx % 32) < 8;
    }

  protected:
    bool wide;

    VfpMacroOp(const char *mnem, ExtMachInst _machInst,
            OpClass __opClass, bool _wide) :
        PredMacroOp(mnem, _machInst, __opClass), wide(_wide)
    {}

    IntRegIndex
    addStride(IntRegIndex idx, unsigned stride)
    {
        if (wide) {
            stride *= 2;
        }
        unsigned offset = idx % 8;
        idx = (IntRegIndex)(idx - offset);
        offset += stride;
        idx = (IntRegIndex)(idx + (offset % 8));
        return idx;
    }

    void
    nextIdxs(IntRegIndex &dest, IntRegIndex &op1, IntRegIndex &op2)
    {
        unsigned stride = (machInst.fpscrStride == 0) ? 1 : 2;
        assert(!inScalarBank(dest));
        dest = addStride(dest, stride);
        op1 = addStride(op1, stride);
        if (!inScalarBank(op2)) {
            op2 = addStride(op2, stride);
        }
    }

    void
    nextIdxs(IntRegIndex &dest, IntRegIndex &op1)
    {
        unsigned stride = (machInst.fpscrStride == 0) ? 1 : 2;
        assert(!inScalarBank(dest));
        dest = addStride(dest, stride);
        if (!inScalarBank(op1)) {
            op1 = addStride(op1, stride);
        }
    }

    void
    nextIdxs(IntRegIndex &dest)
    {
        unsigned stride = (machInst.fpscrStride == 0) ? 1 : 2;
        assert(!inScalarBank(dest));
        dest = addStride(dest, stride);
    }
};

class VfpRegRegOp : public RegRegOp
{
  protected:
    VfpRegRegOp(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
                IntRegIndex _dest, IntRegIndex _op1,
                VfpMicroMode mode = VfpNotAMicroop) :
        RegRegOp(mnem, _machInst, __opClass, _dest, _op1)
    {
        setVfpMicroFlags(mode, flags);
    }
};

class VfpRegImmOp : public RegImmOp
{
  protected:
    VfpRegImmOp(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
                IntRegIndex _dest, uint64_t _imm,
                VfpMicroMode mode = VfpNotAMicroop) :
        RegImmOp(mnem, _machInst, __opClass, _dest, _imm)
    {
        setVfpMicroFlags(mode, flags);
    }
};

class VfpRegRegImmOp : public RegRegImmOp
{
  protected:
    VfpRegRegImmOp(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
                   IntRegIndex _dest, IntRegIndex _op1,
                   uint64_t _imm, VfpMicroMode mode = VfpNotAMicroop) :
        RegRegImmOp(mnem, _machInst, __opClass, _dest, _op1, _imm)
    {
        setVfpMicroFlags(mode, flags);
    }
};

class VfpRegRegRegOp : public RegRegRegOp
{
  protected:
    VfpRegRegRegOp(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
                   IntRegIndex _dest, IntRegIndex _op1, IntRegIndex _op2,
                   VfpMicroMode mode = VfpNotAMicroop) :
        RegRegRegOp(mnem, _machInst, __opClass, _dest, _op1, _op2)
    {
        setVfpMicroFlags(mode, flags);
    }
};

#endif //__ARCH_ARM_INSTS_VFP_HH__
