/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * Copyright (c) 2007-2008 The Florida State University
 * Copyright (c) 2009 The University of Edinburgh
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
 * Authors: Timothy M. Jones
 *          Gabe Black
 *          Stephen Hines
 */

#ifndef __ARCH_POWER_ISA_TRAITS_HH__
#define __ARCH_POWER_ISA_TRAITS_HH__

#include "arch/power/types.hh"
#include "base/types.hh"

namespace BigEndianGuest {}

class StaticInstPtr;

namespace PowerISA
{

using namespace BigEndianGuest;

StaticInstPtr decodeInst(ExtMachInst);

// POWER DOES NOT have a delay slot
#define ISA_HAS_DELAY_SLOT 0

const Addr PageShift = 12;
const Addr PageBytes = ULL(1) << PageShift;
const Addr Page_Mask = ~(PageBytes - 1);
const Addr PageOffset = PageBytes - 1;

const Addr PteShift = 3;
const Addr NPtePageShift = PageShift - PteShift;
const Addr NPtePage = ULL(1) << NPtePageShift;
const Addr PteMask = NPtePage - 1;

const int LogVMPageSize = 12;  // 4K bytes
const int VMPageSize = (1 << LogVMPageSize);

const int MachineBytes = 4;

// This is ori 0, 0, 0
const ExtMachInst NoopMachInst = 0x60000000;

// Memory accesses can be unaligned
const bool HasUnalignedMemAcc = true;

} // namespace PowerISA

#endif // __ARCH_POWER_ISA_TRAITS_HH__
