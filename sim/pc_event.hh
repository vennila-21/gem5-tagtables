/*
 * Copyright (c) 2003 The Regents of The University of Michigan
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
 */

#ifndef __PC_EVENT_HH__
#define __PC_EVENT_HH__

#include <vector>

#include "mem_req.hh"

class ExecContext;
class PCEventQueue;

class PCEvent
{
  protected:
    static const Addr badpc = MemReq::inval_addr;

  protected:
    std::string description;
    PCEventQueue *queue;
    Addr evpc;

  public:
    PCEvent() : queue(0), evpc(badpc) { }

    PCEvent(const std::string &desc)
        : description(desc), queue(0), evpc(badpc) { }

    PCEvent(PCEventQueue *q, Addr pc = badpc) : queue(q), evpc(pc) { }

    PCEvent(PCEventQueue *q, const std::string &desc, Addr pc = badpc)
        : description(desc), queue(q), evpc(pc) { }

    virtual ~PCEvent() { if (queue) remove(); }

    std::string descr() const { return description; }
    Addr pc() const { return evpc; }

    bool remove();
    bool schedule();
    bool schedule(Addr pc);
    bool schedule(PCEventQueue *q, Addr pc);
    virtual void process(ExecContext *xc) = 0;
};

class PCEventQueue
{
  protected:
    typedef PCEvent * record_t;
    class MapCompare {
      public:
        bool operator()(const record_t &l, const record_t &r) const {
            return l->pc() < r->pc();
        }
        bool operator()(const record_t &l, Addr pc) const {
            return l->pc() < pc;
        }
        bool operator()(Addr pc, const record_t &r) const {
            return pc < r->pc();
        }
    };
    typedef std::vector<record_t> map_t;

  public:
    typedef map_t::iterator iterator;
    typedef map_t::const_iterator const_iterator;

  protected:
    typedef std::pair<iterator, iterator> range_t;
    typedef std::pair<const_iterator, const_iterator> const_range_t;

  protected:
    map_t pc_map;

  public:
    PCEventQueue();
    ~PCEventQueue();

    bool remove(PCEvent *event);
    bool schedule(PCEvent *event);
    bool service(ExecContext *xc);

    range_t equal_range(Addr pc);
    range_t equal_range(PCEvent *event) { return equal_range(event->pc()); }

    void dump() const;
};

inline bool
PCEvent::remove()
{
    if (!queue)
        panic("cannot remove an uninitialized event;");

    return queue->remove(this);
}

inline bool
PCEvent::schedule()
{
    if (!queue || evpc == badpc)
        panic("cannot schedule an uninitialized event;");

    return queue->schedule(this);
}

inline bool
PCEvent::schedule(Addr pc)
{
    if (evpc != badpc)
        panic("cannot switch PC");
    evpc = pc;

    return schedule();
}

inline bool
PCEvent::schedule(PCEventQueue *q, Addr pc)
{
    if (queue)
        panic("cannot switch event queues");

    if (evpc != badpc)
        panic("cannot switch addresses");

    queue = q;
    evpc = pc;

    return schedule();
}


#ifdef FULL_SYSTEM
class SkipFuncEvent : public PCEvent
{
  public:
    SkipFuncEvent(PCEventQueue *q, const std::string &desc)
        : PCEvent(q, desc) {}
    virtual void process(ExecContext *xc);
};

class BadAddrEvent : public SkipFuncEvent
{
  public:
    BadAddrEvent(PCEventQueue *q, const std::string &desc)
        : SkipFuncEvent(q, desc) {}
    virtual void process(ExecContext *xc);
};

class PrintfEvent : public PCEvent
{
  public:
    PrintfEvent(PCEventQueue *q, const std::string &desc)
        : PCEvent(q, desc) {}
    virtual void process(ExecContext *xc);
};

class DebugPrintfEvent : public PCEvent
{
  private:
    bool raw;

  public:
    DebugPrintfEvent(PCEventQueue *q, const std::string &desc, bool r = false)
        : PCEvent(q, desc), raw(r) {}
    virtual void process(ExecContext *xc);
};

class DumpMbufEvent : public PCEvent
{
  public:
    DumpMbufEvent(PCEventQueue *q, const std::string &desc)
        : PCEvent(q, desc) {}
    virtual void process(ExecContext *xc);
};
#endif

class BreakPCEvent : public PCEvent
{
  protected:
    bool remove;

  public:
    BreakPCEvent(PCEventQueue *q, const std::string &desc, bool del = false);
    virtual void process(ExecContext *xc);
};


#endif // __PC_EVENT_HH__
