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
 * Copyright 2018 Marcus Zwolsky, University of Ulm & 2016 Randolf Rotta, Robert Kuban, and contributors, BTU Cottbus-Senftenberg
 */

#include "mythos/init.hh"
#include "mythos/invocation.hh"
#include "mythos/protocol/CpuDriverKNC.hh"
#include "mythos/PciMsgQueueMPSC.hh"
#include "runtime/Portal.hh"
#include "runtime/ExecutionContext.hh"
#include "runtime/CapMap.hh"
#include "runtime/Example.hh"
#include "runtime/Fibonacci.hh"
#include "runtime/PageMap.hh"
#include "runtime/KernelMemory.hh"
#include "runtime/SimpleCapAlloc.hh"
#include "runtime/tls.hh"
#include "app/mlog.hh"
#include <cstdint>
#include "util/optional.hh"

#define PARALLEL_ECS 13 // must be > 2, < 100

mythos::InvocationBuf* msg_ptr asm("msg_ptr");
int main() asm("main");

constexpr uint64_t stacksize = PARALLEL_ECS*4096;
char initstack[4096];
char* initstack_top = initstack+4096;

mythos::Portal myPortal(mythos::init::PORTAL, msg_ptr);
mythos::CapMap myCS(mythos::init::CSPACE);
mythos::PageMap myAS(mythos::init::PML4);
mythos::KernelMemory kmem(mythos::init::KM);
mythos::SimpleCapAllocDel capAlloc(myPortal, myCS, mythos::init::APP_CAP_START,
                                  mythos::init::SIZE-mythos::init::APP_CAP_START);

char threadstack[stacksize];
char* threadstack_top = threadstack+stacksize;

mythos::CapPtr portals[PARALLEL_ECS-1];
mythos::InvocationBuf* invocationBuffers[PARALLEL_ECS-1];
mythos::CapPtr createdECs[PARALLEL_ECS-1];
// mythos::CapPtr exampleObject;

void* pgThread1(void* ctx) {
  MLOG_INFO(mlog::app, "playground:", "pgec0 started");
  mythos::ISysretHandler::handle(mythos::syscall_wait());
  MLOG_INFO(mlog::app, "playground:", "pgec0 resumed from wait");
  return 0;
}

void* pgThread2(void* ctx) {
  mythos::ExecutionContext* pgec0 = static_cast<mythos::ExecutionContext*>(ctx);
  MLOG_INFO(mlog::app, "playground:", "pgec1 started");
  for (volatile int i=0; i<100000; i++) {
    for (volatile int j=0; j<1000; j++) {}
  }
  MLOG_INFO(mlog::app, "playground:", "notifying pgec0..");
  mythos::syscall_notify(pgec0->cap());
  mythos::ISysretHandler::handle(mythos::syscall_wait());
  MLOG_INFO(mlog::app, "playground:", "pgec1 resumed from wait");
  return 0;
}

// void* pgWThread(void* ctx) {
//   size_t* msg = static_cast<size_t*>(ctx);
//   MLOG_INFO(mlog::app, "playground:", "worker", msg, "reporting for duty");
//
//   char const fish[] = "playground: So long and thanks for all the fish!";
//   mythos::syscall_debug(fish, sizeof(fish)-1);
//   return 0;
// }

void* portalInvoke(void* ctx) {
  size_t worker = (size_t)ctx;
  MLOG_INFO(mlog::app, "playground:", "Portal invoke example", worker);
  mythos::Fibonacci example(capAlloc());
  mythos::Portal portal(portals[worker], invocationBuffers[worker]);
  mythos::PortalLock pl(portal);

  auto res1 = example.create(pl, kmem).wait();
  if (!res1) {
    MLOG_ERROR(mlog::app, "playground:", "Example", worker, "creation failed!");
    return 0;
  }
  MLOG_INFO(mlog::app, "playground:", "Example created", worker);

  const char* message = "playground: Hello example ";
  char msg[strlen(message)+2+1];  //max 2 digits for worker ID
  memcpy(msg, message, strlen(message));

  //convert worker ID to string
  static char dig[] = "0123456789";
  size_t i = strlen(message);
  if (worker > 9) {
    msg[i++] = dig[worker/10];
  }
  msg[i++] = dig[worker%10];
  // for large numbers but digits reversed
  // do {
  //   msg[i++] = dig[worker%10];
  //   worker /= 10;
  // } while(worker);

  auto res2 = example.printMessage(pl, msg, strlen(msg)).wait();
  if (!res2) {
    // mythos::Error err = static_cast<mythos::Error>(res2);
    MLOG_ERROR(mlog::app, "playground:", "Portal invoke failed:", res2);
  }
  pl.release();
}

void createWorkers() {
  mythos::PortalLock pl(myPortal);
  for (size_t worker = 2; worker < PARALLEL_ECS; worker++) {
    MLOG_INFO(mlog::app, "playground:", "Creating worker", worker);

    //Create a new invocation buffer
    mythos::Frame new_msg_ptr(capAlloc());
    // MLOG_INFO(mlog::app, "playground:", "Creating new invocation buffer...");
    auto res1 = new_msg_ptr.create(pl, kmem, 1<<21, 1<<21).wait();
    if (!res1) {
      MLOG_ERROR(mlog::app, "playground:", "Creation failed");
      break;
    }

    //Map the invocation buffer frame into user space memory
    mythos::InvocationBuf* ib = (mythos::InvocationBuf*)(((11+worker)<<21));
    // MLOG_INFO(mlog::app, "playground:", "Mapping invocation buffer to US...");
    auto res2 = myAS.mmap(pl, new_msg_ptr, (uintptr_t)ib, 1<<21, mythos::protocol::PageMap::MapFlags().writable(true)).wait();
    if (!res2) {
      MLOG_ERROR(mlog::app, "playground:", "Map failed");
      break;
    }
    invocationBuffers[worker] = ib;

    //Create a second portal
    mythos::Portal portal2(capAlloc(), ib);
    // MLOG_INFO(mlog::app, "playground:", "Creating a new portal...");
    res1 = portal2.create(pl, kmem).wait();
    if (!res1) {
      MLOG_ERROR(mlog::app, "playground:", "Creation failed");
      break;
    }
    portals[worker] = portal2.cap();

    //Create EC on its own HWT
    createdECs[worker] = capAlloc();
    mythos::ExecutionContext pgec(createdECs[worker]);
    // char msg[] = "2";
    // MLOG_INFO(mlog::app, "playground:", "Creating ExecutionContext pgec", worker, "...");
    auto res3 = pgec.create(pl, kmem, myAS, myCS, mythos::init::SCHEDULERS_START+(worker % 4), threadstack_top-4096*worker, &portalInvoke, (void*)worker).wait();
    if (!res3) {
      MLOG_ERROR(mlog::app, "playground:", "Creation failed"); //, DVAR(res2));
      break;
    }

    //Bind the second portal to the new EC
    // MLOG_INFO(mlog::app, "playground:", "Binding portal", DVAR(&portal2), "to pgec", worker);
    res1 = portal2.bind(pl, new_msg_ptr, 0, pgec.cap()).wait();
    if (!res1) {
      MLOG_ERROR(mlog::app, "playground:", "Bind failed");
      break;
    }
  }
  pl.release();
}

int main() {
  char const pg[] = "playground: Welcome to the playground!";
  mythos::syscall_debug(pg, sizeof(pg)-1);
  MLOG_INFO(mlog::app, "playground:", "The answer to life, the universe and everything is 42");

  //--create 2 worker ExecutionContexts manually and invoke--
  mythos::PortalLock pl1(myPortal);
  createdECs[0] = capAlloc();
  mythos::ExecutionContext pgec0(createdECs[0]);
  // MLOG_INFO(mlog::app, "playground:", "Creating ExecutionContext pgec0...");
  // auto res1 = pgec0.create(pl1, kmem, myAS, myCS, mythos::init::SCHEDULERS_START, threadstack_top, &pgThread1, nullptr).wait();
  // if (!res1) {
    // MLOG_ERROR(mlog::app, "playground:", "Creation failed");
  // }
  // pgec0.run(pl1).wait();  //will go into a waiting state until notified
  pl1.release();

  mythos::PortalLock pl2(myPortal);
  createdECs[1] = capAlloc();
  mythos::ExecutionContext pgec1(createdECs[1]);
  // MLOG_INFO(mlog::app, "playground:", "Creating ExecutionContext pgec1...");
  // auto res2 = pgec1.create(pl2, kmem, myAS, myCS, mythos::init::SCHEDULERS_START, threadstack_top-4096, &pgThread2, &pgec0).wait();
  // if (!res2) {
  //   MLOG_ERROR(mlog::app, "playground:", "Creation failed");
  // }
  // pgec1.run(pl2).wait();  //will go into a waiting state until notified
  pl2.release();

  //--create a batch of workers and have them create KObjects and invoke those--
  createWorkers();

  for (size_t worker = 2; worker < PARALLEL_ECS; worker++) {
    mythos::PortalLock pl(myPortal);
    mythos::ExecutionContext pgec(createdECs[worker]);
    MLOG_INFO(mlog::app, "playground:", "Starting worker", worker);
    auto res = pgec.run(pl).wait();
    if (!res) {
      MLOG_ERROR(mlog::app, "playground:", "Worker", worker, "could not be started!")
      continue;
    }
    pl.release();
  }

  //--syscall KObject--
  // uint16_t worker = 7;
  // mythos::syscall_invoke(portals[ctx-1], createdECs[ctx], &ctx);
  // mythos::PortalLock pl(myPortal);
  // pl.invoke(exampleObject);
  // pl.release();

  //--calculate the fibonacci number for a given sequence position N--
  //! make/modify a KObject for the fibonacci formula
  //! have 4 new/existing workers calculate the number (tree with 4 worker leafs)

  //--notify pgec1 to resume from waiting state--
  // MLOG_INFO(mlog::app, "playground:", "notifying pgec1..");
  // mythos::syscall_notify(pgec1.cap());
  //pgec1 will do idle counting for some cycles and then notify pgec0

  char const fish[] = "playground: So long and thanks for all the fish!";
  mythos::syscall_debug(fish, sizeof(fish)-1);

  return 0;
}
