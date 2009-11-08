# Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
# Copyright (c) 2009 The Hewlett-Packard Development Company
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
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

from m5.util.code_formatter import code_formatter

from slicc.ast.DeclAST import DeclAST
from slicc.symbols import Func, Type

class FuncDeclAST(DeclAST):
    def __init__(self, slicc, return_type, ident, formals, pairs, statements):
        super(FuncDeclAST, self).__init__(slicc, pairs)

        self.return_type = return_type
        self.ident = ident
        self.formals = formals
        self.statements = statements

    def __repr__(self):
        return "[FuncDecl: %s]" % self.ident

    def files(self, parent=None):
        if "external" in self or self.statements is None:
            return set()

        if parent:
            ident = "%s_%s" % (parent, self.ident)
        else:
            ident = self.ident
        return set(("%s.cc" % ident,))

    def generate(self):
        types = []
        params = []
        void_type = self.symtab.find("void", Type)

        # Generate definition code
        self.symtab.pushFrame()

        # Lookup return type
        return_type = self.return_type.type

        # Generate function header
        for formal in self.formals:
            # Lookup parameter types
            type, ident = formal.generate()
            types.append(type)
            params.append(ident)

        body = code_formatter()
        if self.statements is None:
            self["external"] = "yes"
        else:
            rtype = self.statements.generate(body, return_type)

        self.symtab.popFrame()

        machine = self.state_machine
        func = Func(self.symtab, self.ident, self.location, return_type,
                    types, params, str(body), self.pairs, machine)

        if machine is not None:
            machine.addFunc(func)
        else:
            self.symtab.newSymbol(func)
