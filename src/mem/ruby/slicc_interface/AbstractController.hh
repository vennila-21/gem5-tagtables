/*
 * Copyright (c) 2009 Mark D. Hill and David A. Wood
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

#ifndef __MEM_RUBY_SLICC_INTERFACE_ABSTRACTCONTROLLER_HH__
#define __MEM_RUBY_SLICC_INTERFACE_ABSTRACTCONTROLLER_HH__

#include <iostream>
#include <string>

#include "mem/protocol/AccessPermission.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/Consumer.hh"
#include "mem/ruby/common/DataBlock.hh"
#include "mem/ruby/common/Histogram.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/recorder/CacheRecorder.hh"
#include "mem/ruby/system/MachineID.hh"
#include "mem/packet.hh"
#include "params/RubyController.hh"
#include "sim/clocked_object.hh"

class MessageBuffer;
class Network;

class AbstractController : public ClockedObject, public Consumer
{
  public:
    typedef RubyControllerParams Params;
    AbstractController(const Params *p);
    void init();
    const Params *params() const { return (const Params *)_params; }
    virtual MessageBuffer* getMandatoryQueue() const = 0;
    virtual const int & getVersion() const = 0;
    virtual const std::string toString() const = 0;  // returns text version of
                                                     // controller type
    virtual const std::string getName() const = 0;   // return instance name
    virtual void blockOnQueue(Address, MessageBuffer*) = 0;
    virtual void unblock(Address) = 0;
    virtual void initNetworkPtr(Network* net_ptr) = 0;
    virtual AccessPermission getAccessPermission(const Address& addr) = 0;
    virtual DataBlock& getDataBlock(const Address& addr) = 0;

    virtual void print(std::ostream & out) const = 0;
    virtual void printStats(std::ostream & out) const = 0;
    virtual void wakeup() = 0;
    //  virtual void dumpStats(std::ostream & out) = 0;
    virtual void clearStats() = 0;
    virtual void recordCacheTrace(int cntrl, CacheRecorder* tr) = 0;
    virtual Sequencer* getSequencer() const = 0;

    //! These functions are used by ruby system to read/write the message
    //! queues that exist with in the controller.
    //! The boolean return value indicates if the read was performed
    //! successfully.
    virtual bool functionalReadBuffers(PacketPtr&) = 0;
    //! The return value indicates the number of messages written with the
    //! data from the packet.
    virtual uint32_t functionalWriteBuffers(PacketPtr&) = 0;

    //! Function for enqueuing a prefetch request
    virtual void enqueuePrefetch(const Address&, const RubyRequestType&)
    { fatal("Prefetches not implemented!");}

  public:
    MachineID getMachineID() const { return m_machineID; }
    uint64_t getFullyBusyCycles() const { return m_fully_busy_cycles; }
    uint64_t getRequestCount() const { return m_request_count; }
    const std::map<std::string, uint64_t>& getRequestProfileMap() const
    { return m_requestProfileMap; }

    Histogram& getDelayHist() { return m_delayHistogram; }
    Histogram& getDelayVCHist(uint32_t index)
    { return m_delayVCHistogram[index]; }

  protected:
    //! Profiles original cache requests including PUTs
    void profileRequest(const std::string &request);
    //! Profiles the delay associated with messages.
    void profileMsgDelay(uint32_t virtualNetwork, Cycles delay);

  protected:
    int m_transitions_per_cycle;
    int m_buffer_size;
    Cycles m_recycle_latency;
    std::string m_name;
    NodeID m_version;
    Network* m_net_ptr;
    MachineID m_machineID;
    bool m_is_blocking;
    std::map<Address, MessageBuffer*> m_block_map;
    typedef std::vector<MessageBuffer*> MsgVecType;
    typedef std::map< Address, MsgVecType* > WaitingBufType;
    WaitingBufType m_waiting_buffers;
    int m_max_in_port_rank;
    int m_cur_in_port_rank;
    int m_number_of_TBEs;

    //! Counter for the number of cycles when the transitions carried out
    //! were equal to the maximum allowed
    uint64_t m_fully_busy_cycles;

    //! Map for couting requests of different types. The controller should
    //! call requisite function for updating the count.
    std::map<std::string, uint64_t> m_requestProfileMap;
    uint64_t m_request_count;

    //! Histogram for profiling delay for the messages this controller
    //! cares for
    Histogram m_delayHistogram;
    std::vector<Histogram> m_delayVCHistogram;
};

#endif // __MEM_RUBY_SLICC_INTERFACE_ABSTRACTCONTROLLER_HH__
