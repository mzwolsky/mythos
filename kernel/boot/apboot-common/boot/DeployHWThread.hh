/* -*- mode:C++; indent-tabs-mode:nil; -*- */
/* MIT License -- MyThOS: The Many-Threads Operating System
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Copyright 2014 Randolf Rotta, Maik Krüger, and contributors, BTU Cottbus-Senftenberg
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "plugins/Plugin.hh"
#include "util/compiler.hh"
#include "util/assert.hh"
#include "cpu/GdtAmd64.hh"
#include "cpu/IdtAmd64.hh"
#include "cpu/CoreLocal.hh"
#include "cpu/hwthreadid.hh"
#include "cpu/kernel_entry.hh"
#include "cpu/idle.hh"
#include "async/Place.hh"
#include "objects/DeleteBroadcast.hh"
#include "objects/SchedulingContext.hh"
#include "boot/memory-layout.h"
#include "boot/DeployKernelSpace.hh"
#include "boot/mlog.hh"

namespace mythos {
  namespace boot {

    void initAPTrampoline(size_t startIP);

    extern SchedulingContext schedulers[MYTHOS_MAX_THREADS];
    extern CoreLocal<SchedulingContext*> localScheduler KERNEL_CLM;

    inline SchedulingContext& getScheduler(cpu::ThreadID threadID) { return schedulers[threadID]; }
    inline SchedulingContext& getLocalScheduler() { return *localScheduler.get(); }

struct DeployHWThread
{
  ALIGN_4K static char stackspace[CORE_STACK_SIZE*MYTHOS_MAX_THREADS];
  ALIGN_4K static char nmistackspace[NMI_STACK_SIZE*MYTHOS_MAX_THREADS];

  /** pointers to the AP stacks indexed by APIC ID. This is read from
   * the assembler trampolines to get the initial kernel stack based
   * on the initial apicID, which was gathered from the cpuid
   * instruction.
   */
  static uintptr_t stacks[MYTHOS_MAX_APICID] SYMBOL("ap_startup_stacks");

  static void prepareBSP(size_t startIP) {
    idt.init();
    initAPTrampoline(startIP);
    idle::init_global();
    DeleteBroadcast::init();
    Plugin::initPluginsGlobal();
  }

  void prepare(cpu::ThreadID threadID, cpu::ApicID apicID) {
    PANIC_MSG(threadID<MYTHOS_MAX_THREADS, "unexpectedly large threadID");
    PANIC_MSG(apicID<MYTHOS_MAX_APICID, "unexpectedly large apicID");
    this->threadID = threadID;
    this->apicID = apicID;
    gdt.init();
    auto stackphys = uintptr_t(stackspace)+threadID*CORE_STACK_SIZE - VIRT_ADDR;
    stacks[apicID] = initKernelStack(threadID, stackphys);
    tss_kernel.sp[0] = stacks[apicID];
    tss_kernel.ist[1] = uintptr_t(nmistackspace)+threadID*NMI_STACK_SIZE+NMI_STACK_SIZE;
    gdt.tss_kernel.set(&tss_kernel);
    gdt.kernel_gs.setBaseAddress(uint32_t(KernelCLM::getOffset(threadID)));
    KernelCLM::initOffset(threadID);
    cpu::hwThreadID_.setAt(threadID, threadID);
    async::getPlace(threadID)->init(threadID, apicID);
    localScheduler.setAt(threadID, &getScheduler(threadID));
    getScheduler(threadID).init(async::getPlace(threadID));
    cpu::initSyscallStack(threadID, stacks[apicID]);
    MLOG_DETAIL(mlog::boot, "  hw thread", DVAR(threadID), DVAR(apicID),
                DVARhex(stacks[apicID]), DVARhex(stackphys), DVARhex(tss_kernel.ist[1]),
                DVARhex(KernelCLM::getOffset(threadID)));
    firstboot = true;
  }

  void initThread() {
    loadKernelSpace();
    gdt.load();
    gdt.tss_kernel_load();
    // no logging before loading the GDT for the core-local memory
    idt.load();
    cpu::initSyscallEntry();
    idle::init_thread();

    if (UNLIKELY(this->firstboot)) {
      mythos::lapic.init();
      Plugin::initPluginsOnThread(threadID);
      this->firstboot = false;
    } else {
      // not needed, would through away pending irqs: mythos::lapic.init();
    }
  }

  TSS64 tss_kernel;
  GdtAmd64 gdt;
  cpu::ThreadID threadID;
  cpu::ApicID apicID;
  static IdtAmd64 idt;
  bool firstboot;
};

  } // namespace boot
} // namespace mythos
