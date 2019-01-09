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

#include <new>
#include "mythos/InvocationBuf.hh"
#include "util/assert.hh"
#include "util/PhysPtr.hh"
#include "util/alignments.hh"
#include "objects/Fibonacci.hh"
#include "objects/ops.hh"
#include "objects/KernelMemory.hh"
#include "boot/memory-root.hh"
#include "objects/DebugMessage.hh"
#include "util/error-trace.hh"

namespace mythos {

  static mlog::Logger<mlog::FilterAny> mlogfi("FibonacciObj");

  optional<void const*> FibonacciObj::vcast(TypeId id) const
  {
    mlogfi.info("vcast", DVAR(this), DVAR(id.debug()));
    if (id == typeId<FibonacciObj>()) return /*static_cast<FibonacciObj const*>*/(this);
    THROW(Error::TYPE_MISMATCH);
  }

  optional<void> FibonacciObj::deleteCap(Cap self, IDeleter& del)
  {
    mlogfi.info("deleteCap", DVAR(this), DVAR(self), DVAR(self.isOriginal()));
    if (self.isOriginal()) {
      del.deleteObject(del_handle);
    }
    RETURN(Error::SUCCESS);
  }

  void FibonacciObj::deleteObject(Tasklet* t, IResult<void>* r)
  {
    mlogfi.info("deleteObject", DVAR(this), DVAR(t), DVAR(r));
    monitor.doDelete(t, [=](Tasklet* t){
      _mem->free(t, r, this, sizeof(FibonacciObj));
    });
  }

  void FibonacciObj::invoke(Tasklet* t, Cap self, IInvocation* msg)
  {
    monitor.request(t, [=](Tasklet* t){
        Error err = Error::NOT_IMPLEMENTED;
        switch (msg->getProtocol()) {
        case protocol::KernelObject::proto:
          err = protocol::KernelObject::dispatchRequest(this, msg->getMethod(), self, msg);
          break;
        case protocol::Fibonacci::proto:
          err = protocol::Fibonacci::dispatchRequest(this, msg->getMethod(), t, self, msg);
          break;
        }
        if (err != Error::INHIBIT) {
          msg->replyResponse(err);
          monitor.requestDone();
        }
      } );
  }

  Error FibonacciObj::getDebugInfo(Cap self, IInvocation* msg)
  {
    mlogfi.info("invoke getDebugInfo", DVAR(this), DVAR(self), DVAR(msg));
    return writeDebugInfo("FibonacciObj", self, msg);
  }

  Error FibonacciObj::printMessage(Tasklet*, Cap self, IInvocation* msg)
  {
    mlogfi.info("invoke printMessage", DVAR(this), DVAR(self), DVAR(msg));
    auto data = msg->getMessage()->cast<protocol::Fibonacci::PrintMessage>();
    mlogfi.error(mlog::DebugString(data->message, data->bytes));
    return Error::SUCCESS;
  }

  optional<FibonacciObj*>
  FibFactory::factory(CapEntry* dstEntry, CapEntry* memEntry, Cap memCap,
                         IAllocator* mem)
  {
    auto obj = mem->create<FibonacciObj>();
    if (!obj) {
      dstEntry->reset();
      RETHROW(obj);
    }
    Cap cap(*obj);
    auto res = cap::inherit(*memEntry, *dstEntry, memCap, cap);
    if (!res) {
      mem->free(*obj); // mem->release(obj) goes throug IKernelObject deletion mechanism
      RETHROW(res);
    }
    return *obj;
  }

} // mythos
