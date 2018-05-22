/*
 * System.h
 * 
 * Created on: May 16, 2018
 *     Author: Jovan Nikolov 2016/0040
 */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_

// Only for VS Code to stop IntelliSense from crapping out.
#ifdef BCC_BLOCK_IGNORE
#define interrupt
#endif

#include "Thread.h"

typedef void interrupt (*pInterrupt)(...);

const unsigned TimerEntry = 0x08;
const unsigned NewTimerEntry = 0x60;
const unsigned SysCallEntry = 0x61;

struct RequestType
{
    enum RequestTypeEnum
    {
        // Thread specific requests
        TCreate         = 0x00,
        TDestroy        = 0x01,
        TStart          = 0x02,
        TStop           = 0x03,
        TWaitToComplete = 0x04,
        TSleep          = 0x05,
        TDispatch       = 0x06,
        // Semaphore specific requests
        SCreate         = 0x10,
        SDestroy        = 0x11,
        SWait           = 0x12,
        SSignal         = 0x13,
        // Event specific requests
        ECreate         = 0x20,
        EDestroy        = 0x21,
        EWait           = 0x22,
        ESignal         = 0x23
    };
};

struct SysCallData
{
    unsigned reqType;
    void *object;
    StackSize size;
    Time time;
};

class System
{
public:
    static void initialize();
    static void finalize();

    static void threadPut(PCB *thread);
    static void threadPriorityPut(PCB *thread);
    static PCB* threadGet();

    // todo: remove this and put inside wrapper in Thread.h
    static void threadStop();

    static void* getCallResult();
//private:
    static void interrupt newTimerRoutine(...);
    static void interrupt sysCallRoutine(...);

    static void idleBody();
    static void kernelBody();
    
    static void lock();
    static void unlock();

    static pInterrupt oldTimerRoutine;
    // Global system call data and result.
    static volatile SysCallData *callData;
    static volatile void *callResult;
    // Kernel flags: locked (forbid preemption),
    //               changeContext (requested context change)
    //               systemChangeContext (requested context change on system call)
    //               restoreUserThread (restore user thread after system call)
    static volatile unsigned locked, changeContext, systemChangeContext, restoreUserThread;
    // Counters: tickCount (number of timer ticks passed),
    //           readyThreadCount (number of threads in the scheduler)
    static volatile unsigned tickCount, readyThreadCount;
    // System threads: idle (when there are no ready threads this thread must run)
    //                 running (current user thread)
    //                 runningKernelThread (kernel thread for processing system calls)
    static volatile PCB *idle, *running, *runningKernelThread;
    // Thread lists: prioritized (contains the prioritized threads which will be run
    //                            before any thread in the scheduler)
    //               sleeping (contains the threads that are blocked with the sleep method)
    static volatile PCB *prioritized, *sleeping;
};

#endif /* _SYSTEM_H_ */