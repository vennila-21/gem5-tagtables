/* $Id$ */

/* @file
 * Tsunami DMA fake
 */

#include <deque>
#include <string>
#include <vector>

#include "base/trace.hh"
#include "cpu/exec_context.hh"
#include "dev/console.hh"
#include "dev/etherdev.hh"
#include "dev/scsi_ctrl.hh"
#include "dev/tlaser_clock.hh"
#include "dev/tsunami_io.hh"
#include "dev/tsunamireg.h"
#include "dev/tsunami.hh"
#include "mem/functional_mem/memory_control.hh"
#include "sim/builder.hh"
#include "sim/system.hh"

using namespace std;
TsunamiIO::RTCEvent::RTCEvent()
    : Event(&mainEventQueue)
{
    DPRINTF(Tsunami, "RTC Event Initilizing\n");
    rtc_uip = 0;
    schedule(curTick + (curTick % ticksPerSecond));
}

void
TsunamiIO::RTCEvent::process()
{
    DPRINTF(Tsunami, "Timer Interrupt\n");
    if (rtc_uip == 0) {
        rtc_uip = 1; //Signal a second has occured
        schedule(curTick + (curTick % ticksPerSecond) - 10);
            }
    else
        rtc_uip = 0; //Done signaling second has occured
        schedule(curTick + (curTick % ticksPerSecond));
}

const char *
TsunamiIO::RTCEvent::description()
{
    return "tsunami RTC changte second";
}

uint8_t
TsunamiIO::RTCEvent::rtc_uip_value()
{
    return rtc_uip;
}

TsunamiIO::ClockEvent::ClockEvent()
    : Event(&mainEventQueue)
{
    DPRINTF(Tsunami, "Clock Event Initilizing\n");
    mode = 0;
}

void
TsunamiIO::ClockEvent::process()
{
    DPRINTF(Tsunami, "Timer Interrupt\n");
    if (mode == 0)
       status = 0x20; // set bit that linux is looking for
    else
        schedule(curTick + interval);
}

void
TsunamiIO::ClockEvent::Program(int count)
{
    DPRINTF(Tsunami, "Timer set to curTick + %d\n", count);
    interval = count * ticksPerSecond/1193180UL; // should be count * (cpufreq/pitfreq)
    schedule(curTick + interval);
    status = 0;
}

const char *
TsunamiIO::ClockEvent::description()
{
    return "tsunami 8254 Interval timer";
}

void
TsunamiIO::ClockEvent::ChangeMode(uint8_t md)
{
    mode = md;
}

uint8_t
TsunamiIO::ClockEvent::Status()
{
    return status;
}


TsunamiIO::TsunamiIO(const string &name, /*Tsunami *t,*/
                       Addr addr, Addr mask, MemoryController *mmu)
    : MmapDevice(name, addr, mask, mmu)/*, tsunami(t) */
{
    timerData = 0;
}

Fault
TsunamiIO::read(MemReqPtr req, uint8_t *data)
{
    DPRINTF(Tsunami, "io read  va=%#x size=%d IOPorrt=%#x\n",
            req->vaddr, req->size, req->vaddr & 0xfff);

    Addr daddr = (req->paddr & addr_mask);
//    ExecContext *xc = req->xc;
//    int cpuid = xc->cpu_id;

    switch(req->size) {
        case sizeof(uint8_t):
            switch(daddr) {
                case TSDEV_TMR_CTL:
                    *(uint8_t*)data = timer2.Status();
                    return No_Fault;
                default:
                    panic("I/O Read - va%#x size %d\n", req->vaddr, req->size);
            }
        case sizeof(uint16_t):
        case sizeof(uint32_t):
        case sizeof(uint64_t):
        default:
            panic("I/O Read - invalid size - va %#x size %d\n", req->vaddr, req->size);
    }
     panic("I/O Read - va%#x size %d\n", req->vaddr, req->size);

    return No_Fault;
}

Fault
TsunamiIO::write(MemReqPtr req, const uint8_t *data)
{
    DPRINTF(Tsunami, "io write - va=%#x size=%d IOPort=%#x\n",
            req->vaddr, req->size, req->vaddr & 0xfff);

    Addr daddr = (req->paddr & addr_mask);

    switch(req->size) {
        case sizeof(uint8_t):
            switch(daddr) {
                case TSDEV_PIC1_MASK:
                    mask1 = *(uint8_t*)data;
                    return No_Fault;
                case TSDEV_PIC2_MASK:
                    mask2 = *(uint8_t*)data;
                    return No_Fault;
                case TSDEV_DMA1_RESET:
                    return No_Fault;
                case TSDEV_DMA2_RESET:
                    return No_Fault;
                case TSDEV_DMA1_MODE:
                    mode1 = *(uint8_t*)data;
                    return No_Fault;
                case TSDEV_DMA2_MODE:
                    mode2 = *(uint8_t*)data;
                    return No_Fault;
                case TSDEV_DMA1_MASK:
                case TSDEV_DMA2_MASK:
                    return No_Fault;
                case TSDEV_TMR_CTL:
                    return No_Fault;
                case TSDEV_TMR2_CTL:
                    if ((*(uint8_t*)data & 0x30) != 0x30)
                        panic("Only L/M write supported\n");

                    switch(*(uint8_t*)data >> 6) {
                        case 0:
                            timer0.ChangeMode((*(uint8_t*)data & 0xF) >> 1);
                            break;
                        case 1:
                            timer1.ChangeMode((*(uint8_t*)data & 0xF) >> 1);
                            break;
                        case 2:
                            timer2.ChangeMode((*(uint8_t*)data & 0xF) >> 1);
                            break;
                        case 3:
                        default:
                            panic("Read Back Command not implemented\n");
                    }
                    return No_Fault;
                case TSDEV_TMR2_DATA:
                        /* two writes before we actually start the Timer
                           so I set a flag in the timerData */
                        if(timerData & 0x1000) {
                            timerData &= 0x1000;
                            timerData += *(uint8_t*)data << 8;
                            timer2.Program(timerData);
                        } else {
                            timerData = *(uint8_t*)data;
                            timerData |= 0x1000;
                        }
                        return No_Fault;
                case TSDEV_TMR0_DATA:
                        /* two writes before we actually start the Timer
                           so I set a flag in the timerData */
                        if(timerData & 0x1000) {
                            timerData &= 0x1000;
                            timerData += *(uint8_t*)data << 8;
                            timer0.Program(timerData);
                        } else {
                            timerData = *(uint8_t*)data;
                            timerData |= 0x1000;
                        }
                        return No_Fault;
                 default:
                    panic("I/O Write - va%#x size %d\n", req->vaddr, req->size);
            }
        case sizeof(uint16_t):
        case sizeof(uint32_t):
        case sizeof(uint64_t):
        default:
            panic("I/O Write - invalid size - va %#x size %d\n", req->vaddr, req->size);
    }


    return No_Fault;
}

void
TsunamiIO::serialize(std::ostream &os)
{
    // code should be written
}

void
TsunamiIO::unserialize(Checkpoint *cp, const std::string &section)
{
    //code should be written
}

BEGIN_DECLARE_SIM_OBJECT_PARAMS(TsunamiIO)

 //   SimObjectParam<Tsunami *> tsunami;
    SimObjectParam<MemoryController *> mmu;
    Param<Addr> addr;
    Param<Addr> mask;

END_DECLARE_SIM_OBJECT_PARAMS(TsunamiIO)

BEGIN_INIT_SIM_OBJECT_PARAMS(TsunamiIO)

//    INIT_PARAM(tsunami, "Tsunami"),
    INIT_PARAM(mmu, "Memory Controller"),
    INIT_PARAM(addr, "Device Address"),
    INIT_PARAM(mask, "Address Mask")

END_INIT_SIM_OBJECT_PARAMS(TsunamiIO)

CREATE_SIM_OBJECT(TsunamiIO)
{
    return new TsunamiIO(getInstanceName(), /*tsunami,*/ addr, mask, mmu);
}

REGISTER_SIM_OBJECT("TsunamiIO", TsunamiIO)
