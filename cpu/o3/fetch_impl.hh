/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#include "arch/isa_traits.hh"
#include "sim/byteswap.hh"
#include "cpu/exetrace.hh"
#include "cpu/o3/fetch.hh"
#include "mem/base_mem.hh"
#include "mem/mem_interface.hh"
#include "mem/mem_req.hh"

#include "sim/root.hh"

#if FULL_SYSTEM
#include "base/remote_gdb.hh"
#include "mem/functional/memory_control.hh"
#include "mem/functional/physical.hh"
#include "sim/system.hh"
#include "arch/tlb.hh"
#include "arch/vtophys.hh"
#else // !FULL_SYSTEM
#include "mem/functional/functional.hh"
#endif // FULL_SYSTEM

#include <algorithm>

using namespace std;

template<class Impl>
DefaultFetch<Impl>::CacheCompletionEvent::CacheCompletionEvent(MemReqPtr &_req,
                                                               DefaultFetch *_fetch)
    : Event(&mainEventQueue, Delayed_Writeback_Pri),
      req(_req),
      fetch(_fetch)
{
    this->setFlags(Event::AutoDelete);
}

template<class Impl>
void
DefaultFetch<Impl>::CacheCompletionEvent::process()
{
    fetch->processCacheCompletion(req);
}

template<class Impl>
const char *
DefaultFetch<Impl>::CacheCompletionEvent::description()
{
    return "DefaultFetch cache completion event";
}

template<class Impl>
DefaultFetch<Impl>::DefaultFetch(Params *params)
    : icacheInterface(params->icacheInterface),
      branchPred(params),
      decodeToFetchDelay(params->decodeToFetchDelay),
      renameToFetchDelay(params->renameToFetchDelay),
      iewToFetchDelay(params->iewToFetchDelay),
      commitToFetchDelay(params->commitToFetchDelay),
      fetchWidth(params->fetchWidth),
      numThreads(params->numberOfThreads),
      numFetchingThreads(params->smtNumFetchingThreads),
      interruptPending(false)
{
    if (numThreads > Impl::MaxThreads)
        fatal("numThreads is not a valid value\n");

    DPRINTF(Fetch, "Fetch constructor called\n");

    // Set fetch stage's status to inactive.
    _status = Inactive;

    string policy = params->smtFetchPolicy;

    // Convert string to lowercase
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   (int(*)(int)) tolower);

    // Figure out fetch policy
    if (policy == "singlethread") {
        fetchPolicy = SingleThread;
    } else if (policy == "roundrobin") {
        fetchPolicy = RoundRobin;
        DPRINTF(Fetch, "Fetch policy set to Round Robin\n");
    } else if (policy == "branch") {
        fetchPolicy = Branch;
        DPRINTF(Fetch, "Fetch policy set to Branch Count\n");
    } else if (policy == "iqcount") {
        fetchPolicy = IQ;
        DPRINTF(Fetch, "Fetch policy set to IQ count\n");
    } else if (policy == "lsqcount") {
        fetchPolicy = LSQ;
        DPRINTF(Fetch, "Fetch policy set to LSQ count\n");
    } else {
        fatal("Invalid Fetch Policy. Options Are: {SingleThread,"
              " RoundRobin,LSQcount,IQcount}\n");
    }

    // Size of cache block.
    cacheBlkSize = icacheInterface ? icacheInterface->getBlockSize() : 64;

    // Create mask to get rid of offset bits.
    cacheBlkMask = (cacheBlkSize - 1);

    for (int tid=0; tid < numThreads; tid++) {

        fetchStatus[tid] = Running;

        priorityList.push_back(tid);

        // Create a new memory request.
        memReq[tid] = NULL;
//        memReq[tid] = new MemReq();
/*
        // Need a way of setting this correctly for parallel programs
        // @todo: Figure out how to properly set asid vs thread_num.
        memReq[tid]->asid = tid;
        memReq[tid]->thread_num = tid;
        memReq[tid]->data = new uint8_t[64];
*/
        // Create space to store a cache line.
        cacheData[tid] = new uint8_t[cacheBlkSize];

        stalls[tid].decode = 0;
        stalls[tid].rename = 0;
        stalls[tid].iew = 0;
        stalls[tid].commit = 0;
    }

    // Get the size of an instruction.
    instSize = sizeof(MachInst);
}

template <class Impl>
std::string
DefaultFetch<Impl>::name() const
{
    return cpu->name() + ".fetch";
}

template <class Impl>
void
DefaultFetch<Impl>::regStats()
{
    icacheStallCycles
        .name(name() + ".icacheStallCycles")
        .desc("Number of cycles fetch is stalled on an Icache miss")
        .prereq(icacheStallCycles);

    fetchedInsts
        .name(name() + ".fetchedInsts")
        .desc("Number of instructions fetch has processed")
        .prereq(fetchedInsts);

    fetchedBranches
        .name(name() + ".fetchedBranches")
        .desc("Number of branches that fetch encountered")
        .prereq(fetchedBranches);

    predictedBranches
        .name(name() + ".predictedBranches")
        .desc("Number of branches that fetch has predicted taken")
        .prereq(predictedBranches);

    fetchCycles
        .name(name() + ".fetchCycles")
        .desc("Number of cycles fetch has run and was not squashing or"
              " blocked")
        .prereq(fetchCycles);

    fetchSquashCycles
        .name(name() + ".fetchSquashCycles")
        .desc("Number of cycles fetch has spent squashing")
        .prereq(fetchSquashCycles);

    fetchIdleCycles
        .name(name() + ".fetchIdleCycles")
        .desc("Number of cycles fetch was idle")
        .prereq(fetchIdleCycles);

    fetchBlockedCycles
        .name(name() + ".fetchBlockedCycles")
        .desc("Number of cycles fetch has spent blocked")
        .prereq(fetchBlockedCycles);

    fetchedCacheLines
        .name(name() + ".fetchedCacheLines")
        .desc("Number of cache lines fetched")
        .prereq(fetchedCacheLines);

    fetchIcacheSquashes
        .name(name() + ".fetchIcacheSquashes")
        .desc("Number of outstanding Icache misses that were squashed")
        .prereq(fetchIcacheSquashes);

    fetchNisnDist
        .init(/* base value */ 0,
              /* last value */ fetchWidth,
              /* bucket size */ 1)
        .name(name() + ".rateDist")
        .desc("Number of instructions fetched each cycle (Total)")
        .flags(Stats::pdf);

    idleRate
        .name(name() + ".idleRate")
        .desc("Percent of cycles fetch was idle")
        .prereq(idleRate);
    idleRate = fetchIdleCycles * 100 / cpu->numCycles;

    branchRate
        .name(name() + ".branchRate")
        .desc("Number of branch fetches per cycle")
        .flags(Stats::total);
    branchRate = predictedBranches / cpu->numCycles;

    fetchRate
        .name(name() + ".rate")
        .desc("Number of inst fetches per cycle")
        .flags(Stats::total);
    fetchRate = fetchedInsts / cpu->numCycles;

    branchPred.regStats();
}

template<class Impl>
void
DefaultFetch<Impl>::setCPU(FullCPU *cpu_ptr)
{
    DPRINTF(Fetch, "Setting the CPU pointer.\n");
    cpu = cpu_ptr;

    // Set ExecContexts for Memory Requests
//    for (int tid=0; tid < numThreads; tid++)
//        memReq[tid]->xc = cpu->xcBase(tid);

    // Fetch needs to start fetching instructions at the very beginning,
    // so it must start up in active state.
    switchToActive();
}

template<class Impl>
void
DefaultFetch<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *time_buffer)
{
    DPRINTF(Fetch, "Setting the time buffer pointer.\n");
    timeBuffer = time_buffer;

    // Create wires to get information from proper places in time buffer.
    fromDecode = timeBuffer->getWire(-decodeToFetchDelay);
    fromRename = timeBuffer->getWire(-renameToFetchDelay);
    fromIEW = timeBuffer->getWire(-iewToFetchDelay);
    fromCommit = timeBuffer->getWire(-commitToFetchDelay);
}

template<class Impl>
void
DefaultFetch<Impl>::setActiveThreads(list<unsigned> *at_ptr)
{
    DPRINTF(Fetch, "Setting active threads list pointer.\n");
    activeThreads = at_ptr;
}

template<class Impl>
void
DefaultFetch<Impl>::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    DPRINTF(Fetch, "Setting the fetch queue pointer.\n");
    fetchQueue = fq_ptr;

    // Create wire to write information to proper place in fetch queue.
    toDecode = fetchQueue->getWire(0);
}

#if 0
template<class Impl>
void
DefaultFetch<Impl>::setPageTable(PageTable *pt_ptr)
{
    DPRINTF(Fetch, "Setting the page table pointer.\n");
#if !FULL_SYSTEM
    pTable = pt_ptr;
#endif
}
#endif

template<class Impl>
void
DefaultFetch<Impl>::initStage()
{
    for (int tid = 0; tid < numThreads; tid++) {
        PC[tid] = cpu->readPC(tid);
        nextPC[tid] = cpu->readNextPC(tid);
    }
}

template<class Impl>
void
DefaultFetch<Impl>::processCacheCompletion(MemReqPtr &req)
{
    unsigned tid = req->thread_num;

    DPRINTF(Fetch, "[tid:%u] Waking up from cache miss.\n",tid);

    // Only change the status if it's still waiting on the icache access
    // to return.
    // Can keep track of how many cache accesses go unused due to
    // misspeculation here.
    if (fetchStatus[tid] != IcacheMissStall ||
        req != memReq[tid]) {
        ++fetchIcacheSquashes;
        return;
    }

    // Wake up the CPU (if it went to sleep and was waiting on this completion
    // event).
    cpu->wakeCPU();

    DPRINTF(Activity, "[tid:%u] Activating fetch due to cache completion\n",
            tid);

    switchToActive();

    // Only switch to IcacheMissComplete if we're not stalled as well.
    if (checkStall(tid)) {
        fetchStatus[tid] = Blocked;
    } else {
        fetchStatus[tid] = IcacheMissComplete;
    }

//    memcpy(cacheData[tid], memReq[tid]->data, memReq[tid]->size);

    // Reset the completion event to NULL.
    memReq[tid] = NULL;
//    memReq[tid]->completionEvent = NULL;
}

template <class Impl>
void
DefaultFetch<Impl>::wakeFromQuiesce()
{
    DPRINTF(Fetch, "Waking up from quiesce\n");
    // Hopefully this is safe
    fetchStatus[0] = Running;
}

template <class Impl>
inline void
DefaultFetch<Impl>::switchToActive()
{
    if (_status == Inactive) {
        DPRINTF(Activity, "Activating stage.\n");

        cpu->activateStage(FullCPU::FetchIdx);

        _status = Active;
    }
}

template <class Impl>
inline void
DefaultFetch<Impl>::switchToInactive()
{
    if (_status == Active) {
        DPRINTF(Activity, "Deactivating stage.\n");

        cpu->deactivateStage(FullCPU::FetchIdx);

        _status = Inactive;
    }
}

template <class Impl>
bool
DefaultFetch<Impl>::lookupAndUpdateNextPC(DynInstPtr &inst, Addr &next_PC)
{
    // Do branch prediction check here.
    // A bit of a misnomer...next_PC is actually the current PC until
    // this function updates it.
    bool predict_taken;

    if (!inst->isControl()) {
        next_PC = next_PC + instSize;
        inst->setPredTarg(next_PC);
        return false;
    }

    predict_taken = branchPred.predict(inst, next_PC, inst->threadNumber);

    ++fetchedBranches;

    if (predict_taken) {
        ++predictedBranches;
    }

    return predict_taken;
}

template <class Impl>
bool
DefaultFetch<Impl>::fetchCacheLine(Addr fetch_PC, Fault &ret_fault, unsigned tid)
{
    // Check if the instruction exists within the cache.
    // If it does, then proceed on to read the instruction and the rest
    // of the instructions in the cache line until either the end of the
    // cache line or a predicted taken branch is encountered.
    Fault fault = NoFault;

#if FULL_SYSTEM
    // Flag to say whether or not address is physical addr.
    unsigned flags = cpu->inPalMode(fetch_PC) ? PHYSICAL : 0;
#else
    unsigned flags = 0;
#endif // FULL_SYSTEM

    if (interruptPending && flags == 0) {
        // Hold off fetch from getting new instructions while an interrupt
        // is pending.
        return false;
    }

    // Align the fetch PC so it's at the start of a cache block.
    fetch_PC = icacheBlockAlignPC(fetch_PC);

    // Setup the memReq to do a read of the first instruction's address.
    // Set the appropriate read size and flags as well.
    memReq[tid] = new MemReq();

    memReq[tid]->asid = tid;
    memReq[tid]->thread_num = tid;
    memReq[tid]->data = new uint8_t[64];
    memReq[tid]->xc = cpu->xcBase(tid);
    memReq[tid]->cmd = Read;
    memReq[tid]->reset(fetch_PC, cacheBlkSize, flags);

    // Translate the instruction request.
//#if FULL_SYSTEM
    fault = cpu->translateInstReq(memReq[tid]);
//#else
//    fault = pTable->translate(memReq[tid]);
//#endif

    // In the case of faults, the fetch stage may need to stall and wait
    // on what caused the fetch (ITB or Icache miss).

    // If translation was successful, attempt to read the first
    // instruction.
    if (fault == NoFault) {
#if FULL_SYSTEM
        if (cpu->system->memctrl->badaddr(memReq[tid]->paddr)) {
            DPRINTF(Fetch, "Fetch: Bad address %#x (hopefully on a "
                    "misspeculating path!",
                    memReq[tid]->paddr);
            ret_fault = TheISA::genMachineCheckFault();
            return false;
        }
#endif

        DPRINTF(Fetch, "Fetch: Doing instruction read.\n");
        fault = cpu->mem->read(memReq[tid], cacheData[tid]);
        // This read may change when the mem interface changes.

        // Now do the timing access to see whether or not the instruction
        // exists within the cache.
        if (icacheInterface && !icacheInterface->isBlocked()) {
            DPRINTF(Fetch, "Doing cache access.\n");

            memReq[tid]->completionEvent = NULL;

            memReq[tid]->time = curTick;

            MemAccessResult result = icacheInterface->access(memReq[tid]);

            fetchedCacheLines++;

            // If the cache missed, then schedule an event to wake
            // up this stage once the cache miss completes.
            // @todo: Possibly allow for longer than 1 cycle cache hits.
            if (result != MA_HIT && icacheInterface->doEvents()) {

                memReq[tid]->completionEvent =
                    new CacheCompletionEvent(memReq[tid], this);

                lastIcacheStall[tid] = curTick;

                DPRINTF(Activity, "[tid:%i]: Activity: Stalling due to I-cache "
                        "miss.\n", tid);

                fetchStatus[tid] = IcacheMissStall;
            } else {
                DPRINTF(Fetch, "[tid:%i]: I-Cache hit. Doing Instruction "
                        "read.\n", tid);

//                memcpy(cacheData[tid], memReq[tid]->data, memReq[tid]->size);
            }
        } else {
            DPRINTF(Fetch, "[tid:%i] Out of MSHRs!\n", tid);
            ret_fault = NoFault;
            return false;
        }
    }

    ret_fault = fault;
    return true;
}

template <class Impl>
inline void
DefaultFetch<Impl>::doSquash(const Addr &new_PC, unsigned tid)
{
    DPRINTF(Fetch, "[tid:%i]: Squashing, setting PC to: %#x.\n",
            tid, new_PC);

    PC[tid] = new_PC;
    nextPC[tid] = new_PC + instSize;

    // Clear the icache miss if it's outstanding.
    if (fetchStatus[tid] == IcacheMissStall && icacheInterface) {
        DPRINTF(Fetch, "[tid:%i]: Squashing outstanding Icache miss.\n",
                tid);
//        icacheInterface->squash(tid);
/*
        if (memReq[tid]->completionEvent) {
            if (memReq[tid]->completionEvent->scheduled()) {
                memReq[tid]->completionEvent->squash();
            } else {
                delete memReq[tid]->completionEvent;
                memReq[tid]->completionEvent = NULL;
            }
        }
*/
        memReq[tid] = NULL;
    }

    if (fetchStatus[tid] == TrapPending) {
        // @todo: Hardcoded number here

        // This is only effective if communication to and from commit
        // is identical.  If it's faster to commit than it is from
        // commit to here, then it causes problems.

        bool found_fault = false;
        for (int i = 0; i > -5; --i) {
            if (fetchQueue->access(i)->fetchFault) {
                DPRINTF(Fetch, "[tid:%i]: Fetch used to be in a trap, "
                        "clearing it.\n",
                        tid);
                fetchQueue->access(i)->fetchFault = NoFault;
                found_fault = true;
            }
        }
        if (!found_fault) {
            warn("%lli Fault from fetch not found in time buffer!",
                 curTick);
        }
        toDecode->clearFetchFault = true;
    }

    fetchStatus[tid] = Squashing;

    ++fetchSquashCycles;
}

template<class Impl>
void
DefaultFetch<Impl>::squashFromDecode(const Addr &new_PC,
                                    const InstSeqNum &seq_num,
                                    unsigned tid)
{
    DPRINTF(Fetch, "[tid:%i]: Squashing from decode.\n",tid);

    doSquash(new_PC, tid);

    // Tell the CPU to remove any instructions that are in flight between
    // fetch and decode.
    cpu->removeInstsUntil(seq_num, tid);
}

template<class Impl>
bool
DefaultFetch<Impl>::checkStall(unsigned tid) const
{
    bool ret_val = false;

    if (cpu->contextSwitch) {
        DPRINTF(Fetch,"[tid:%i]: Stalling for a context switch.\n",tid);
        ret_val = true;
    } else if (stalls[tid].decode) {
        DPRINTF(Fetch,"[tid:%i]: Stall from Decode stage detected.\n",tid);
        ret_val = true;
    } else if (stalls[tid].rename) {
        DPRINTF(Fetch,"[tid:%i]: Stall from Rename stage detected.\n",tid);
        ret_val = true;
    } else if (stalls[tid].iew) {
        DPRINTF(Fetch,"[tid:%i]: Stall from IEW stage detected.\n",tid);
        ret_val = true;
    } else if (stalls[tid].commit) {
        DPRINTF(Fetch,"[tid:%i]: Stall from Commit stage detected.\n",tid);
        ret_val = true;
    }

    return ret_val;
}

template<class Impl>
typename DefaultFetch<Impl>::FetchStatus
DefaultFetch<Impl>::updateFetchStatus()
{
    //Check Running
    list<unsigned>::iterator threads = (*activeThreads).begin();

    while (threads != (*activeThreads).end()) {

        unsigned tid = *threads++;

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == Squashing ||
            fetchStatus[tid] == IcacheMissComplete) {

            if (_status == Inactive) {
                DPRINTF(Activity, "[tid:%i]: Activating stage.\n",tid);

                if (fetchStatus[tid] == IcacheMissComplete) {
                    DPRINTF(Activity, "[tid:%i]: Activating fetch due to cache"
                            "completion\n",tid);
                }

                cpu->activateStage(FullCPU::FetchIdx);
            }

            return Active;
        }
    }

    // Stage is switching from active to inactive, notify CPU of it.
    if (_status == Active) {
        DPRINTF(Activity, "Deactivating stage.\n");

        cpu->deactivateStage(FullCPU::FetchIdx);
    }

    return Inactive;
}

template <class Impl>
void
DefaultFetch<Impl>::squash(const Addr &new_PC, unsigned tid)
{
    DPRINTF(Fetch, "[tid:%u]: Squash from commit.\n",tid);

    doSquash(new_PC, tid);

    // Tell the CPU to remove any instructions that are not in the ROB.
    cpu->removeInstsNotInROB(tid);
}

template <class Impl>
void
DefaultFetch<Impl>::tick()
{
    list<unsigned>::iterator threads = (*activeThreads).begin();
    bool status_change = false;

    wroteToTimeBuffer = false;

    while (threads != (*activeThreads).end()) {
        unsigned tid = *threads++;

        // Check the signals for each thread to determine the proper status
        // for each thread.
        bool updated_status = checkSignalsAndUpdate(tid);
        status_change =  status_change || updated_status;
    }

    DPRINTF(Fetch, "Running stage.\n");

    // Reset the number of the instruction we're fetching.
    numInst = 0;

    if (fromCommit->commitInfo[0].interruptPending) {
        interruptPending = true;
    }
    if (fromCommit->commitInfo[0].clearInterrupt) {
        interruptPending = false;
    }

    for (threadFetched = 0; threadFetched < numFetchingThreads;
         threadFetched++) {
        // Fetch each of the actively fetching threads.
        fetch(status_change);
    }

    // Record number of instructions fetched this cycle for distribution.
    fetchNisnDist.sample(numInst);

    if (status_change) {
        // Change the fetch stage status if there was a status change.
        _status = updateFetchStatus();
    }

    // If there was activity this cycle, inform the CPU of it.
    if (wroteToTimeBuffer || cpu->contextSwitch) {
        DPRINTF(Activity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }
}

template <class Impl>
bool
DefaultFetch<Impl>::checkSignalsAndUpdate(unsigned tid)
{
    // Update the per thread stall statuses.
    if (fromDecode->decodeBlock[tid]) {
        stalls[tid].decode = true;
    }

    if (fromDecode->decodeUnblock[tid]) {
        assert(stalls[tid].decode);
        assert(!fromDecode->decodeBlock[tid]);
        stalls[tid].decode = false;
    }

    if (fromRename->renameBlock[tid]) {
        stalls[tid].rename = true;
    }

    if (fromRename->renameUnblock[tid]) {
        assert(stalls[tid].rename);
        assert(!fromRename->renameBlock[tid]);
        stalls[tid].rename = false;
    }

    if (fromIEW->iewBlock[tid]) {
        stalls[tid].iew = true;
    }

    if (fromIEW->iewUnblock[tid]) {
        assert(stalls[tid].iew);
        assert(!fromIEW->iewBlock[tid]);
        stalls[tid].iew = false;
    }

    if (fromCommit->commitBlock[tid]) {
        stalls[tid].commit = true;
    }

    if (fromCommit->commitUnblock[tid]) {
        assert(stalls[tid].commit);
        assert(!fromCommit->commitBlock[tid]);
        stalls[tid].commit = false;
    }

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(Fetch, "[tid:%u]: Squashing instructions due to squash "
                "from commit.\n",tid);

        // In any case, squash.
        squash(fromCommit->commitInfo[tid].nextPC,tid);

        // Also check if there's a mispredict that happened.
        if (fromCommit->commitInfo[tid].branchMispredict) {
            branchPred.squash(fromCommit->commitInfo[tid].doneSeqNum,
                              fromCommit->commitInfo[tid].nextPC,
                              fromCommit->commitInfo[tid].branchTaken,
                              tid);
        } else {
            branchPred.squash(fromCommit->commitInfo[tid].doneSeqNum,
                              tid);
        }

        return true;
    } else if (fromCommit->commitInfo[tid].doneSeqNum) {
        // Update the branch predictor if it wasn't a squashed instruction
        // that was broadcasted.
        branchPred.update(fromCommit->commitInfo[tid].doneSeqNum, tid);
    }

    // Check ROB squash signals from commit.
    if (fromCommit->commitInfo[tid].robSquashing) {
        DPRINTF(Fetch, "[tid:%u]: ROB is still squashing Thread %u.\n", tid);

        // Continue to squash.
        fetchStatus[tid] = Squashing;

        return true;
    }

    // Check squash signals from decode.
    if (fromDecode->decodeInfo[tid].squash) {
        DPRINTF(Fetch, "[tid:%u]: Squashing instructions due to squash "
                "from decode.\n",tid);

        // Update the branch predictor.
        if (fromDecode->decodeInfo[tid].branchMispredict) {
            branchPred.squash(fromDecode->decodeInfo[tid].doneSeqNum,
                              fromDecode->decodeInfo[tid].nextPC,
                              fromDecode->decodeInfo[tid].branchTaken,
                              tid);
        } else {
            branchPred.squash(fromDecode->decodeInfo[tid].doneSeqNum,
                              tid);
        }

        if (fetchStatus[tid] != Squashing) {
            // Squash unless we're already squashing
            squashFromDecode(fromDecode->decodeInfo[tid].nextPC,
                             fromDecode->decodeInfo[tid].doneSeqNum,
                             tid);

            return true;
        }
    }

    if (checkStall(tid) && fetchStatus[tid] != IcacheMissStall) {
        DPRINTF(Fetch, "[tid:%i]: Setting to blocked\n",tid);

        fetchStatus[tid] = Blocked;

        return true;
    }

    if (fetchStatus[tid] == Blocked ||
        fetchStatus[tid] == Squashing) {
        // Switch status to running if fetch isn't being told to block or
        // squash this cycle.
        DPRINTF(Fetch, "[tid:%i]: Done squashing, switching to running.\n",
                tid);

        fetchStatus[tid] = Running;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause fetch to change its status.  Fetch remains the same as before.
    return false;
}

template<class Impl>
void
DefaultFetch<Impl>::fetch(bool &status_change)
{
    //////////////////////////////////////////
    // Start actual fetch
    //////////////////////////////////////////
    int tid = getFetchingThread(fetchPolicy);

    if (tid == -1) {
        DPRINTF(Fetch,"There are no more threads available to fetch from.\n");

        // Breaks looping condition in tick()
        threadFetched = numFetchingThreads;
        return;
    }

    // The current PC.
    Addr &fetch_PC = PC[tid];

    // Fault code for memory access.
    Fault fault = NoFault;

    // If returning from the delay of a cache miss, then update the status
    // to running, otherwise do the cache access.  Possibly move this up
    // to tick() function.
    if (fetchStatus[tid] == IcacheMissComplete) {
        DPRINTF(Fetch, "[tid:%i]: Icache miss is complete.\n",
                tid);

        fetchStatus[tid] = Running;
        status_change = true;
    } else if (fetchStatus[tid] == Running) {
        DPRINTF(Fetch, "[tid:%i]: Attempting to translate and read "
                "instruction, starting at PC %08p.\n",
                tid, fetch_PC);

        bool fetch_success = fetchCacheLine(fetch_PC, fault, tid);
        if (!fetch_success)
            return;
    } else {
        if (fetchStatus[tid] == Idle) {
            ++fetchIdleCycles;
        } else if (fetchStatus[tid] == Blocked) {
            ++fetchBlockedCycles;
        } else if (fetchStatus[tid] == Squashing) {
            ++fetchSquashCycles;
        } else if (fetchStatus[tid] == IcacheMissStall) {
            ++icacheStallCycles;
        }

        // Status is Idle, Squashing, Blocked, or IcacheMissStall, so
        // fetch should do nothing.
        return;
    }

    ++fetchCycles;

    // If we had a stall due to an icache miss, then return.
    if (fetchStatus[tid] == IcacheMissStall) {
        ++icacheStallCycles;
        status_change = true;
        return;
    }

    Addr next_PC = fetch_PC;
    InstSeqNum inst_seq;
    MachInst inst;
    ExtMachInst ext_inst;
    // @todo: Fix this hack.
    unsigned offset = (fetch_PC & cacheBlkMask) & ~3;

    if (fault == NoFault) {
        // If the read of the first instruction was successful, then grab the
        // instructions from the rest of the cache line and put them into the
        // queue heading to decode.

        DPRINTF(Fetch, "[tid:%i]: Adding instructions to queue to "
                "decode.\n",tid);

        //////////////////////////
        // Fetch first instruction
        //////////////////////////

        // Need to keep track of whether or not a predicted branch
        // ended this fetch block.
        bool predicted_branch = false;

        for (;
             offset < cacheBlkSize &&
                 numInst < fetchWidth &&
                 !predicted_branch;
             ++numInst) {

            // Get a sequence number.
            inst_seq = cpu->getAndIncrementInstSeq();

            // Make sure this is a valid index.
            assert(offset <= cacheBlkSize - instSize);

            // Get the instruction from the array of the cache line.
            inst = gtoh(*reinterpret_cast<MachInst *>
                        (&cacheData[tid][offset]));

            ext_inst = TheISA::makeExtMI(inst, fetch_PC);

            // Create a new DynInst from the instruction fetched.
            DynInstPtr instruction = new DynInst(ext_inst, fetch_PC,
                                                 next_PC,
                                                 inst_seq, cpu);
            instruction->setThread(tid);

            instruction->setASID(tid);

            instruction->setState(cpu->thread[tid]);

            DPRINTF(Fetch, "[tid:%i]: Instruction PC %#x created "
                    "[sn:%lli]\n",
                    tid, instruction->readPC(), inst_seq);

            DPRINTF(Fetch, "[tid:%i]: Instruction is: %s\n",
                    tid, instruction->staticInst->disassemble(fetch_PC));

            instruction->traceData =
                Trace::getInstRecord(curTick, cpu->xcBase(tid), cpu,
                                     instruction->staticInst,
                                     instruction->readPC(),tid);

            predicted_branch = lookupAndUpdateNextPC(instruction, next_PC);

            // Add instruction to the CPU's list of instructions.
            instruction->setInstListIt(cpu->addInst(instruction));

            // Write the instruction to the first slot in the queue
            // that heads to decode.
            toDecode->insts[numInst] = instruction;

            toDecode->size++;

            // Increment stat of fetched instructions.
            ++fetchedInsts;

            // Move to the next instruction, unless we have a branch.
            fetch_PC = next_PC;

            if (instruction->isQuiesce()) {
                warn("%lli: Quiesce instruction encountered, halting fetch!", curTick);
                fetchStatus[tid] = QuiescePending;
                ++numInst;
                status_change = true;
                break;
            }

            offset+= instSize;
        }
    }

    if (numInst > 0) {
        wroteToTimeBuffer = true;
    }

    // Now that fetching is completed, update the PC to signify what the next
    // cycle will be.
    if (fault == NoFault) {

        DPRINTF(Fetch, "[tid:%i]: Setting PC to %08p.\n",tid, next_PC);


        PC[tid] = next_PC;
        nextPC[tid] = next_PC + instSize;
    } else {
        // If the issue was an icache miss, then we can just return and
        // wait until it is handled.
        if (fetchStatus[tid] == IcacheMissStall) {
            panic("Fetch should have exited prior to this!");
        }

        // Handle the fault.
        // This stage will not be able to continue until all the ROB
        // slots are empty, at which point the fault can be handled.
        // The only other way it can wake up is if a squash comes along
        // and changes the PC.  Not sure how to handle that case...perhaps
        // have it handled by the upper level CPU class which peeks into the
        // time buffer and sees if a squash comes along, in which case it
        // changes the status.
#if FULL_SYSTEM
        // Tell the commit stage the fault we had.
        toDecode->fetchFault = fault;
        toDecode->fetchFaultSN = cpu->globalSeqNum;

        DPRINTF(Fetch, "[tid:%i]: Blocked, need to handle the trap.\n",tid);

        fetchStatus[tid] = TrapPending;
        status_change = true;

        warn("%lli fault (%d) detected @ PC %08p", curTick, fault, PC[tid]);
//        cpu->trap(fault);
        // Send a signal to the ROB indicating that there's a trap from the
        // fetch stage that needs to be handled.  Need to indicate that
        // there's a fault, and the fault type.
#else // !FULL_SYSTEM
        fatal("fault (%d) detected @ PC %08p", fault, PC[tid]);
#endif // FULL_SYSTEM
    }
}


///////////////////////////////////////
//                                   //
//  SMT FETCH POLICY MAINTAINED HERE //
//                                   //
///////////////////////////////////////
template<class Impl>
int
DefaultFetch<Impl>::getFetchingThread(FetchPriority &fetch_priority)
{
    if (numThreads > 1) {
        switch (fetch_priority) {

          case SingleThread:
            return 0;

          case RoundRobin:
            return roundRobin();

          case IQ:
            return iqCount();

          case LSQ:
            return lsqCount();

          case Branch:
            return branchCount();

          default:
            return -1;
        }
    } else {
        int tid = *((*activeThreads).begin());

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == IcacheMissComplete ||
            fetchStatus[tid] == Idle) {
            return tid;
        } else {
            return -1;
        }
    }

}


template<class Impl>
int
DefaultFetch<Impl>::roundRobin()
{
    list<unsigned>::iterator pri_iter = priorityList.begin();
    list<unsigned>::iterator end      = priorityList.end();

    int high_pri;

    while (pri_iter != end) {
        high_pri = *pri_iter;

        assert(high_pri <= numThreads);

        if (fetchStatus[high_pri] == Running ||
            fetchStatus[high_pri] == IcacheMissComplete ||
            fetchStatus[high_pri] == Idle) {

            priorityList.erase(pri_iter);
            priorityList.push_back(high_pri);

            return high_pri;
        }

        pri_iter++;
    }

    return -1;
}

template<class Impl>
int
DefaultFetch<Impl>::iqCount()
{
    priority_queue<unsigned> PQ;

    list<unsigned>::iterator threads = (*activeThreads).begin();

    while (threads != (*activeThreads).end()) {
        unsigned tid = *threads++;

        PQ.push(fromIEW->iewInfo[tid].iqCount);
    }

    while (!PQ.empty()) {

        unsigned high_pri = PQ.top();

        if (fetchStatus[high_pri] == Running ||
            fetchStatus[high_pri] == IcacheMissComplete ||
            fetchStatus[high_pri] == Idle)
            return high_pri;
        else
            PQ.pop();

    }

    return -1;
}

template<class Impl>
int
DefaultFetch<Impl>::lsqCount()
{
    priority_queue<unsigned> PQ;


    list<unsigned>::iterator threads = (*activeThreads).begin();

    while (threads != (*activeThreads).end()) {
        unsigned tid = *threads++;

        PQ.push(fromIEW->iewInfo[tid].ldstqCount);
    }

    while (!PQ.empty()) {

        unsigned high_pri = PQ.top();

        if (fetchStatus[high_pri] == Running ||
            fetchStatus[high_pri] == IcacheMissComplete ||
           fetchStatus[high_pri] == Idle)
            return high_pri;
        else
            PQ.pop();

    }

    return -1;
}

template<class Impl>
int
DefaultFetch<Impl>::branchCount()
{
    list<unsigned>::iterator threads = (*activeThreads).begin();

    return *threads;
}
