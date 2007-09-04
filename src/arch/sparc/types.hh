/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
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

#ifndef __ARCH_SPARC_TYPES_HH__
#define __ARCH_SPARC_TYPES_HH__

#include <inttypes.h>
#include "base/bigint.hh"

namespace SparcISA
{
    typedef uint32_t MachInst;
    typedef uint64_t ExtMachInst;

    typedef uint64_t IntReg;
    typedef Twin64_t LargestRead;
    typedef uint64_t MiscReg;
    typedef double FloatReg;
    typedef uint64_t FloatRegBits;
    typedef union
    {
        IntReg intReg;
        FloatReg fpreg;
        MiscReg ctrlreg;
    } AnyReg;

    enum RegContextParam
    {
        CONTEXT_CWP,
        CONTEXT_GLOBALS
    };

    typedef int RegContextVal;

    typedef uint16_t RegIndex;
}

#endif
