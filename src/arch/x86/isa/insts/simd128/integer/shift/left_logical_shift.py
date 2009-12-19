# Copyright (c) 2007 The Hewlett-Packard Development Company
# All rights reserved.
#
# Redistribution and use of this software in source and binary forms,
# with or without modification, are permitted provided that the
# following conditions are met:
#
# The software must be used only for Non-Commercial Use which means any
# use which is NOT directed to receiving any direct monetary
# compensation for, or commercial advantage from such use.  Illustrative
# examples of non-commercial use are academic research, personal study,
# teaching, education and corporate research & development.
# Illustrative examples of commercial use are distributing products for
# commercial advantage and providing services using the software for
# commercial advantage.
#
# If you wish to use this software or functionality therein that may be
# covered by patents for commercial use, please contact:
#     Director of Intellectual Property Licensing
#     Office of Strategy and Technology
#     Hewlett-Packard Company
#     1501 Page Mill Road
#     Palo Alto, California  94304
#
# Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.  Redistributions
# in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.  Neither the name of
# the COPYRIGHT HOLDER(s), HEWLETT-PACKARD COMPANY, nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.  No right of
# sublicense is granted herewith.  Derivatives of the software and
# output created using the software may be prepared, but only for
# Non-Commercial Uses.  Derivatives of the software may be shared with
# others provided: (i) the others agree to abide by the list of
# conditions herein which includes the Non-Commercial Use restrictions;
# and (ii) such Derivatives of the software include the above copyright
# notice to acknowledge the contribution from this software where
# applicable, this list of conditions and the disclaimer below.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Gabe Black

microcode = '''
def macroop PSLLW_XMM_XMM {
    msll xmmh, xmmh, xmmlm, size=2, ext=0
    msll xmml, xmml, xmmlm, size=2, ext=0
};

def macroop PSLLW_XMM_M {
    ldfp ufp1, seg, sib, disp, dataSize=8
    msll xmml, xmml, ufp1, size=2, ext=0
    msll xmmh, xmmh, ufp1, size=2, ext=0
};

def macroop PSLLW_XMM_P {
    rdip t7
    ldfp ufp1, seg, riprel, disp, dataSize=8
    msll xmml, xmml, ufp1, size=2, ext=0
    msll xmmh, xmmh, ufp1, size=2, ext=0
};

def macroop PSLLW_XMM_I {
    mslli xmml, xmml, imm, size=2, ext=0
    mslli xmmh, xmmh, imm, size=2, ext=0
};

def macroop PSLLD_XMM_XMM {
    msll xmmh, xmmh, xmmlm, size=4, ext=0
    msll xmml, xmml, xmmlm, size=4, ext=0
};

def macroop PSLLD_XMM_M {
    ldfp ufp1, seg, sib, disp, dataSize=8
    msll xmml, xmml, ufp1, size=4, ext=0
    msll xmmh, xmmh, ufp1, size=4, ext=0
};

def macroop PSLLD_XMM_P {
    rdip t7
    ldfp ufp1, seg, riprel, disp, dataSize=8
    msll xmml, xmml, ufp1, size=4, ext=0
    msll xmmh, xmmh, ufp1, size=4, ext=0
};

def macroop PSLLD_XMM_I {
    mslli xmml, xmml, imm, size=4, ext=0
    mslli xmmh, xmmh, imm, size=4, ext=0
};

def macroop PSLLQ_XMM_XMM {
    msll xmmh, xmmh, xmmlm, size=8, ext=0
    msll xmml, xmml, xmmlm, size=8, ext=0
};

def macroop PSLLQ_XMM_M {
    ldfp ufp1, seg, sib, disp, dataSize=8
    msll xmml, xmml, ufp1, size=8, ext=0
    msll xmmh, xmmh, ufp1, size=8, ext=0
};

def macroop PSLLQ_XMM_P {
    rdip t7
    ldfp ufp1, seg, riprel, disp, dataSize=8
    msll xmml, xmml, ufp1, size=8, ext=0
    msll xmmh, xmmh, ufp1, size=8, ext=0
};

def macroop PSLLQ_XMM_I {
    mslli xmml, xmml, imm, size=8, ext=0
    mslli xmmh, xmmh, imm, size=8, ext=0
};

def macroop PSLLDQ_XMM_I {

    limm  t2, 8
    subi t1, t2, imm, flags=(ECF,), dataSize=1
    br label("pslldq_less_8"), flags=(nCECF,)

    # Greater than 8

    limm  t2, 16
    subi t1, t2, imm, flags=(ECF,), dataSize=1
    br label("pslldq_less_16"), flags=(nCECF,)

    # Greater than 16

    lfpimm  xmml, 0
    lfpimm  xmmh, 0
    br label("pslldq_end")

pslldq_less_16:

    # Between 8 and 16

    mslli xmmh, xmml, "(IMMEDIATE-8)<<3", size=8, ext=0
    lfpimm  xmml, 0
    br label("pslldq_end")

pslldq_less_8:

    # Less than 8

    msrli ufp1, xmml, "(8-IMMEDIATE) << 3", size=8, ext=0
    mslli xmmh, xmmh, "IMMEDIATE << 3", size=8, ext=0
    mslli xmml, xmml, "IMMEDIATE << 3", size=8, ext=0
    mor   xmmh, xmmh, ufp1

pslldq_end:
    fault "NoFault"
};
'''
