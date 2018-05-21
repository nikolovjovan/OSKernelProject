/*
 * System.cpp
 *
 * Created on: May 16, 2018
 *     Author: Jovan Nikolov 2016/0040
 */

#include <dos.h>
#include <stdio.h>

#include "Schedule.h"

#include "Macro.h"
#include "KThread.h"
#include "System.h"

// Only for VS Code to stop IntelliSense from being annoying.
#ifdef BCC_BLOCK_IGNORE
#define interrupt
#endif

extern void tick();

// Initializing System variables.
pInterrupt System::oldTimerRoutine = 0;
volatile SysCallData *System::callData = 0;
volatile unsigned System::locked = 0, System::changeContext = 0,
                  System::systemChangeContext, System::restoreUserThread = 0;
volatile unsigned System::tickCount = 0, System::readyThreadCount = 0;
volatile PCB *System::initial = new PCB(), *System::idle = new PCB(idleBody, 0, 1);
volatile PCB *System::running = System::initial,
             *System::runningKernelThread = new PCB(kernelBody);
volatile PCB *System::prioritized = 0, *System::sleeping = 0,
             *System::blocked = 0;

// Temporary variables for context change.
volatile unsigned tempBP, tempSP, tempSS;

void System::initialize()
{
    // Modifying timer IVT entry and initializing
    // sysCall IVT entry.
    #ifndef BCC_BLOCK_IGNORE
    asmLock();
    oldTimerRoutine = getvect(TimerEntry);
    setvect(TimerEntry, newTimerRoutine);
    setvect(NewTimerEntry, oldTimerRoutine);
    setvect(SysCallEntry, sysCallRoutine);
    asmUnlock();
    #endif

    ((PCB*) initial)->setTimeSlice(20);
    ((PCB*) runningKernelThread)->setTimeSlice(0);
    tickCount = running->mTimeSlice;
}

void System::finalize()
{
    // Restoring timer IVT entry.
    #ifndef BCC_BLOCK_IGNORE
    asmLock();
    setvect(TimerEntry, oldTimerRoutine);
    asmUnlock();
    #endif
    // todo dispose all the stuff...
    delete initial;
    delete idle;
    delete runningKernelThread;
}

void System::threadStop()
{
    if (!running) return; // Exception, no running thread!
    running->mState = PCB::Terminated;
    PCB::dispatch();
}

void System::threadPut(PCB *thread)
{
    #ifndef BCC_BLOCK_IGNORE
    asmLock();
    #endif
    // Cannot put the idle thread into the Scheduler!
    if (thread != idle)
    {
        //printf("Put: SS = %d, SP = %d, BP = %d, timeSlice = %d\n", thread->mSS, thread->mSP, thread->mBP, thread->mTimeSlice);
        readyThreadCount++;
        Scheduler::put(thread);
    }
    else printf("ERROR: idle thread\n");
    #ifndef BCC_BLOCK_IGNORE
    asmUnlock();
    #endif
}

void System::threadPriorityPut(PCB *thread)
{
    // ovde stavljati u red prioritetnih niti
}

PCB* System::threadGet()
{
    // ovde obraditi prioritetne niti
    // PCB *res = Scheduler::get();
    // if (res != 0) return res;
    // return (PCB*) idle;
    PCB *thread = Scheduler::get();
    if (thread) readyThreadCount--;
    else
    {
        thread = (PCB*) idle;
        printf("Idle thread!\n");
    }
    //printf("Get: SS = %d, SP = %d, BP = %d, timeSlice = %d\n", thread->mSS, thread->mSP, thread->mBP, thread->mTimeSlice);
    return thread;
}

void interrupt System::newTimerRoutine(...)
{
    if (!changeContext)
    {
        tick();
        #ifndef BCC_BLOCK_IGNORE
        asmInterrupt(NewTimerEntry);
        #endif
        if (tickCount > 0)
        {
            tickCount--;
            if (tickCount == 0) changeContext = 1;
        }
    }
    // If a context change is required and preemption is allowed,
    // and there is at least one Ready thread or the running thread
    // is Terminated (we need to switch to the idle thread).
    if (changeContext && !locked && (readyThreadCount > 0 || 
        running->mState == PCB::Terminated))
    {
        // Saving the context of the running thread.
        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov tempBP, bp
            mov tempSP, sp
            mov tempSS, ss
        };
        #endif

        running->mSS = tempSS;
        running->mSP = tempSP;
        running->mBP = tempBP;

        // Getting the next thread.
        if (running->mState != PCB::Terminated) threadPut((PCB*) running);
        running = threadGet();
        
        // This should not ever happen!
        if (!running) printf("ERROR: running is null!\n");

        // Restoring the context of the next thread.
        tempSS = running->mSS;
        tempSP = running->mSP;
        tempBP = running->mBP;

        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov ss, tempSS
            mov sp, tempSP
            mov bp, tempBP
        };
        #endif

        tickCount = running->mTimeSlice;
        changeContext = 0;
    }
}

void interrupt System::sysCallRoutine(...)
{
    if (restoreUserThread)
    {
        // We must unlock the context switch here because we
        // could not have done it in kernelBody because of
        // the call to this interrupt routine.
        unlock();

        // Saving the context of the kernel thread.
        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov tempBP, bp
            mov tempSP, sp
            mov tempSS, ss
        };
        #endif
        
        runningKernelThread->mSS = tempSS;
        runningKernelThread->mSP = tempSP;
        runningKernelThread->mBP = tempBP;

        // If any of the blocking requests are made, we need to
        // change the user thread context before switching to it.
        if (systemChangeContext)
        {
            if (running->mState != PCB::Terminated) threadPut((PCB*) running);
            running = threadGet();
            tickCount = running->mTimeSlice;
            systemChangeContext = 0;
        }
        
        // This should not ever happen!
        if (!running) printf("ERROR: running is null!\n");

        // Restoring the context of the user thread.
        tempSS = running->mSS;
        tempSP = running->mSP;
        tempBP = running->mBP;

        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov ss, tempSS
            mov sp, tempSP
            mov bp, tempBP
        };
        #endif
        restoreUserThread = 0;
    }
    else
    {
        // Getting the call params.
        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov tempSS, cx
            mov tempSP, dx
        };
        callData = (SysCallData*) MK_FP(tempSS, tempSP);
        #endif
    
        // Saving the context of the user thread.
        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov tempBP, bp
            mov tempSP, sp
            mov tempSS, ss
        };
        #endif
        
        running->mSS = tempSS;
        running->mSP = tempSP;
        running->mBP = tempBP;

        // Restoring the context of the kernel thread.
        tempSS = runningKernelThread->mSS;
        tempSP = runningKernelThread->mSP;
        tempBP = runningKernelThread->mBP;

        #ifndef BCC_BLOCK_IGNORE
        asm {
            mov ss, tempSS
            mov sp, tempSP
            mov bp, tempBP
        };
        #endif

        tickCount = runningKernelThread->mTimeSlice;
    }
}

void System::idleBody()
{
    // Using readyThreadCount to prevent the host OS from killing the process.
    while (!readyThreadCount);
}

void System::kernelBody()
{
    while (1)
    {
        lock();
        //printf("Running kernel thread!\n");
        switch (callData->objType)
        {
            case ObjectType::Thread:
            {
                //printf("Thread call!\n");
                switch (callData->reqType)
                {
                    case ThreadRequestType::Dispatch:
                    {
                        //printf("Dispatching...\n");
                        // Set the flag to change the current user thread.
                        systemChangeContext = 1;
                        break;
                    }
                    //default: printf("Default Thread system call!");
                }
                break;
            }
            case ObjectType::Semaphore:
            {
                //printf("Semaphore call!\n");
                break;
            }
            case ObjectType::Event:
            {
                //printf("Event call!\n");
                break;
            }
        }
        restoreUserThread = 1;
        #ifndef BCC_BLOCK_IGNORE
        asmInterrupt(SysCallEntry);
        #endif
    }
}

void System::lock()
{
    #ifndef BCC_BLOCK_IGNORE
    asmLock();
    locked = 1;
    asmUnlock();
    #endif
}

void System::unlock()
{
    #ifndef BCC_BLOCK_IGNORE
    asmLock();
    locked = 0;
    asmUnlock();
    #endif
}