/*
 * Copyright (c) 2006 The Regents of The University of Michigan
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

#ifndef __ARCH_SPARC_ASI_HH__
#define __ARCH_SPARC_ASI_HH__

namespace SparcISA
{
    enum ASI {
        /* Priveleged ASIs */
        //0x00-0x03 implementation dependent
        ASI_NUCLEUS = 0x4,
        ASI_N = 0x4,
        //0x05-0x0B implementation dependent
        ASI_NL = 0xC,
        ASI_NUCLEUS_LITTLE = ASI_NL,
        //0x0D-0x0F implementation dependent
        ASI_AIUP = 0x10,
        ASI_AS_IF_USER_PRIMARY = ASI_AIUP,
        ASI_AIUS = 0x11,
        ASI_AS_IF_USER_SECONDARY = ASI_AIUS,
        //0x12-0x13 implementation dependent
        ASI_REAL = 0x14,
        ASI_REAL_IO = 0x15,
        ASI_BLK_AIUP = 0x16,
        ASI_BLOCK_AS_IF_USER_PRIMARY = ASI_BLK_AIUP,
        ASI_BLK_AIUS = 0x17,
        ASI_BLOCK_AS_IF_USER_SECONDARY = ASI_BLK_AIUS,
        ASI_AIUPL = 0x18,
        ASI_AS_IF_USER_PRIMARY_LITTLE = ASI_AIUPL,
        ASI_AIUSL = 0x19,
        ASI_AS_IF_USER_SECONDARY_LITTLE = ASI_AIUSL,
        //0x1A-0x1B implementation dependent
        ASI_REAL_L = 0x1C,
        ASI_REAL_LITTLE = ASI_REAL_L,
        ASI_REAL_IO_L = 0x1D,
        ASI_REAL_IO_LITTLE = ASI_REAL_IO_L,
        ASI_BLK_AIUPL = 0x1E,
        ASI_BLOCK_AS_IF_USER_PRIMARY_LITTLE = ASI_BLK_AIUPL,
        ASI_BLK_AIUSL = 0x1F,
        ASI_BLOCK_AS_IF_USER_SECONDARY_LITTLE = ASI_BLK_AIUSL,
        ASI_SCRATCHPAD = 0x20,
        ASI_MMU_CONTEXTID = 0x21,
        ASI_LDTX_AIUP = 0x22,
        ASI_LD_TWINX_AS_IF_USER_PRIMARY = ASI_LDTX_AIUP,
        ASI_LDTX_AIUS = 0x23,
        ASI_LD_TWINX_AS_IF_USER_SECONDARY = ASI_LDTX_AIUS,
        //0x24 implementation dependent
        ASI_QUEUE = 0x25,
        ASI_LDTX_REAL = 0x26,
        ASI_LD_TWINX_REAL = ASI_LDTX_REAL,
        ASI_LDTX_N = 0x27,
        ASI_LD_TWINX_NUCLEUS = ASI_LDTX_N,
        //0x28-0x29 implementation dependent
        ASI_LDTX_AIUPL = 0x2A,
        ASI_LD_TWINX_AS_IF_USER_PRIMARY_LITTLE = ASI_LDTX_AIUPL,
        ASI_LDTX_AIUSL = 0x2B,
        ASI_LD_TWINX_AS_IF_USER_SECONDARY_LITTLE = ASI_LDTX_AIUSL,
        //0x2C-0x2D implementation dependent
        ASI_LDTX_REAL_L = 0x2E,
        ASI_LD_TWINX_REAL_LITTLE = ASI_LDTX_REAL_L,
        ASI_LDTX_NL = 0x2F,
        ASI_LD_TWINX_NUCLEUS_LITTLE = ASI_LDTX_NL,
        //0x30-0x40 implementation dependent
        ASI_CMT_SHARED = 0x41,
        //0x42-0x4F implementation dependent
        ASI_HYP_SCRATCHPAD = 0x4F,
        ASI_IMMU = 0x50,
        ASI_MMU_REAL = 0x52,
        //0x53 implementation dependent
        ASI_MMU = 0x54,
        ASI_ITLB_DATA_ACCESS_REG = 0x55,
        ASI_ITLB_TAG_READ_REG = 0x56,
        ASI_IMMU_DEMAP = 0x57,
        ASI_DMMU = 0x58,
        ASI_UMMU = 0x58,
        //0x59-0x5B reserved
        ASI_DTLB_DATA_IN_REG = 0x5C,
        ASI_DTLB_DATA_ACCESS_REG = 0x5D,
        ASI_DTLB_TAG_READ_REG = 0x5E,
        ASI_DMMU_DEMAP = 0x5F,
        //0x60-62 implementation dependent
        ASI_CMT_PER_STRAND = 0x63,
        //0x64-0x67 implementation dependent
        //0x68-0x7F reserved

        /* Unpriveleged ASIs */
        ASI_P = 0x80,
        ASI_PRIMARY = ASI_P,
        ASI_S = 0x81,
        ASI_SECONDARY = ASI_S,
        ASI_PNF = 0x82,
        ASI_PRIMARY_NO_FAULT = ASI_PNF,
        ASI_SNF = 0x83,
        ASI_SECONDARY_NO_FAULT = ASI_SNF,
        //0x84-0x87 reserved
        ASI_PL = 0x88,
        ASI_PRIMARY_LITTLE = ASI_PL,
        ASI_SL = 0x89,
        ASI_SECONDARY_LITTLE = ASI_SL,
        ASI_PNFL = 0x8A,
        ASI_PRIMARY_NO_FAULT_LITTLE = ASI_PNFL,
        ASI_SNFL = 0x8B,
        ASI_SECONDARY_NO_FAULT_LITTLE = ASI_SNFL,
        //0x8C-0xBF reserved
        ASI_PST8_P = 0xC0,
        ASI_PST8_PRIMARY = ASI_PST8_P,
        ASI_PST8_S = 0xC1,
        ASI_PST8_SECONDARY = ASI_PST8_S,
        ASI_PST16_P = 0xC2,
        ASI_PST16_PRIMARY = ASI_PST16_P,
        ASI_PST16_S = 0xC3,
        ASI_PST16_SECONDARY = ASI_PST16_S,
        ASI_PST32_P = 0xC4,
        ASI_PST32_PRIMARY = ASI_PST32_P,
        ASI_PST32_S = 0xC5,
        ASI_PST32_SECONDARY = ASI_PST32_S,
        //0xC6-0xC7 implementation dependent
        ASI_PST8_PL = 0xC8,
        ASI_PST8_PRIMARY_LITTLE = ASI_PST8_PL,
        ASI_PST8_SL = 0xC9,
        ASI_PST8_SECONDARY_LITTLE = ASI_PST8_SL,
        ASI_PST16_PL = 0xCA,
        ASI_PST16_PRIMARY_LITTLE = ASI_PST16_PL,
        ASI_PST16_SL = 0xCB,
        ASI_PST16_SECONDARY_LITTLE = ASI_PST16_SL,
        ASI_PST32_PL = 0xCC,
        ASI_PST32_PRIMARY_LITTLE = ASI_PST32_PL,
        ASI_PST32_SL = 0xCD,
        ASI_PST32_SECONDARY_LITTLE = ASI_PST32_SL,
        //0xCE-0xCF implementation dependent
        ASI_PL8_P = 0xD0,
        ASI_PL8_PRIMARY = ASI_PL8_P,
        ASI_PL8_S = 0xD1,
        ASI_PL8_SECONDARY = ASI_PL8_S,
        ASI_PL16_P = 0xD2,
        ASI_PL16_PRIMARY = ASI_PL16_P,
        ASI_PL16_S = 0xD3,
        ASI_PL16_SECONDARY = ASI_PL16_S,
        //0xD4-0xD7 implementation dependent
        ASI_PL8_PL = 0xD8,
        ASI_PL8_PRIMARY_LITTLE = ASI_PL8_PL,
        ASI_PL8_SL = 0xD9,
        ASI_PL8_SECONDARY_LITTLE = ASI_PL8_SL,
        ASI_PL16_PL = 0xDA,
        ASI_PL16_PRIMARY_LITTLE = ASI_PL16_PL,
        ASI_PL16_SL = 0xDB,
        ASI_PL16_SECONDARY_LITTLE = ASI_PL16_SL,
        //0xDC-0xDF implementation dependent
        //0xE0-0xE1 reserved
        ASI_LDTX_P = 0xE2,
        ASI_LD_TWINX_PRIMARY = ASI_LDTX_P,
        ASI_LDTX_S = 0xE3,
        ASI_LD_TWINX_SECONDARY = ASI_LDTX_S,
        //0xE4-0xE9 implementation dependent
        ASI_LDTX_PL = 0xEA,
        ASI_LD_TWINX_PRIMARY_LITTLE = ASI_LDTX_PL,
        ASI_LDTX_SL = 0xEB,
        ASI_LD_TWINX_SECONDARY_LITTLE = ASI_LDTX_SL,
        //0xEC-0xEF implementation dependent
        ASI_BLK_P = 0xF0,
        ASI_BLOCK_PRIMARY = ASI_BLK_P,
        ASI_BLK_S = 0xF1,
        ASI_BLOCK_SECONDARY = ASI_BLK_S,
        //0xF2-0xF7 implementation dependent
        ASI_BLK_PL = 0xF8,
        ASI_BLOCK_PRIMARY_LITTLE = ASI_BLK_PL,
        ASI_BLK_SL = 0xF9,
        ASI_BLOCK_SECONDARY_LITTLE = ASI_BLK_SL
        //0xFA-0xFF implementation dependent
    };
};

#endif // __ARCH_SPARC_TLB_HH__
