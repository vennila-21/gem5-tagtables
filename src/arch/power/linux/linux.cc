/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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
 */

#include "arch/power/linux/linux.hh"

#include <fcntl.h>

// open(2) flags translation table
OpenFlagTransTable PowerLinux::openFlagTable[] = {
#ifdef _MSC_VER
  { PowerLinux::TGT_O_RDONLY,    _O_RDONLY },
  { PowerLinux::TGT_O_WRONLY,    _O_WRONLY },
  { PowerLinux::TGT_O_RDWR,      _O_RDWR },
  { PowerLinux::TGT_O_APPEND,    _O_APPEND },
  { PowerLinux::TGT_O_CREAT,     _O_CREAT },
  { PowerLinux::TGT_O_TRUNC,     _O_TRUNC },
  { PowerLinux::TGT_O_EXCL,      _O_EXCL },
#ifdef _O_NONBLOCK
  { PowerLinux::TGT_O_NONBLOCK,  _O_NONBLOCK },
#endif
#ifdef _O_NOCTTY
  { PowerLinux::TGT_O_NOCTTY,    _O_NOCTTY },
#endif
#ifdef _O_SYNC
  { PowerLinux::TGT_O_SYNC,      _O_SYNC },
#endif
#ifdef _O_LARGEFILE
  { PowerLinux::TGT_O_LARGEFILE, _O_LARGEFILE },
#endif
#else /* !_MSC_VER */
  { PowerLinux::TGT_O_RDONLY,    O_RDONLY },
  { PowerLinux::TGT_O_WRONLY,    O_WRONLY },
  { PowerLinux::TGT_O_RDWR,      O_RDWR },
  { PowerLinux::TGT_O_APPEND,    O_APPEND },
  { PowerLinux::TGT_O_CREAT,     O_CREAT },
  { PowerLinux::TGT_O_TRUNC,     O_TRUNC },
  { PowerLinux::TGT_O_EXCL,      O_EXCL },
  { PowerLinux::TGT_O_NONBLOCK,  O_NONBLOCK },
  { PowerLinux::TGT_O_NOCTTY,    O_NOCTTY },
#ifdef O_SYNC
  { PowerLinux::TGT_O_SYNC,      O_SYNC },
#endif
#ifdef O_LARGEFILE
  { PowerLinux::TGT_O_LARGEFILE, O_LARGEFILE },
#endif
#endif /* _MSC_VER */
};

const int PowerLinux::NUM_OPEN_FLAGS =
        (sizeof(PowerLinux::openFlagTable)/sizeof(PowerLinux::openFlagTable[0]));

