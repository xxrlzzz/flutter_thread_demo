// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define FML_USED_ON_EMBEDDER

#include "flutter/fml/message_loop_impl.h"

#include <algorithm>
#include <vector>

#include "flutter/fml/build_config.h"
#include "flutter/fml/logging.h"

#if OS_MACOSX
#include "flutter/fml/platform/darwin/message_loop_darwin.h"
#elif OS_LINUX
#include "flutter/fml/platform/linux/message_loop_linux.h"
#elif OS_WIN
#include "flutter/fml/platform/win/message_loop_win.h"
#endif

namespace fml {

fml::RefPtr<MessageLoopImpl> MessageLoopImpl::Create() {
#if OS_MACOSX
  return fml::MakeRefCounted<MessageLoopDarwin>();
#elif OS_LINUX
  return fml::MakeRefCounted<MessageLoopLinux>();
#elif OS_WIN
  return fml::MakeRefCounted<MessageLoopWin>();
#else
  return nullptr;
#endif
}

MessageLoopImpl::MessageLoopImpl()
    : task_queue_(MessageLoopTaskQueues::GetInstance()),
      queue_id_(task_queue_->CreateTaskQueue()),
      terminated_(false) {
  task_queue_->SetWakeable(queue_id_, this);
}

MessageLoopImpl::~MessageLoopImpl() {
  task_queue_->Dispose(queue_id_);
}

void MessageLoopImpl::PostTask(const fml::closure& task,
                               fml::TimePoint target_time) {
  FML_DCHECK(task != nullptr);
  if (terminated_) {
    // If the message loop has already been terminated, PostTask should destruct
    // |task| synchronously within this function.
    return;
  }
  task_queue_->RegisterTask(queue_id_, task, target_time);
}

void MessageLoopImpl::AddTaskObserver(intptr_t key,
                                      const fml::closure& callback) {
  FML_DCHECK(callback != nullptr);
  FML_DCHECK(MessageLoop::GetCurrent().GetLoopImpl().get() == this)
      << "Message loop task observer must be added on the same thread as the "
         "loop.";
  if (callback != nullptr) {
    task_queue_->AddTaskObserver(queue_id_, key, callback);
  } else {
    FML_LOG(ERROR) << "Tried to add a null TaskObserver.";
  }
}

void MessageLoopImpl::RemoveTaskObserver(intptr_t key) {
  FML_DCHECK(MessageLoop::GetCurrent().GetLoopImpl().get() == this)
      << "Message loop task observer must be removed from the same thread as "
         "the loop.";
  task_queue_->RemoveTaskObserver(queue_id_, key);
}

void MessageLoopImpl::DoRun() {
  if (terminated_) {
    // Message loops may be run only once.
    return;
  }

  // Allow the implementation to do its thing.
  Run();

  // The loop may have been implicitly terminated. This can happen if the
  // implementation supports termination via platform specific APIs or just
  // error conditions. Set the terminated flag manually.
  terminated_ = true;

  // The message loop is shutting down. Check if there are expired tasks. This
  // is the last chance for expired tasks to be serviced. Make sure the
  // terminated flag is already set so we don't accrue additional tasks now.
  RunExpiredTasksNow();

  // When the message loop is in the process of shutting down, pending tasks
  // should be destructed on the message loop's thread. We have just returned
  // from the implementations |Run| method which we know is on the correct
  // thread. Drop all pending tasks on the floor.
  task_queue_->DisposeTasks(queue_id_);
}

void MessageLoopImpl::DoTerminate() {
  terminated_ = true;
  Terminate();
}

void MessageLoopImpl::FlushTasks(FlushType type) {
  const auto now = fml::TimePoint::Now();
  fml::closure invocation;

  bool observers_inited = false;
  std::vector<fml::closure> observers;
  do {
    invocation = task_queue_->GetNextTaskToRun(queue_id_, now);
    if (!invocation) {
      break;
    }
    invocation();
    if (!observers_inited) {
      observers = task_queue_->GetObserversToNotify(queue_id_);
      observers_inited = true;
    }
    for (const auto& observer : observers) {
      observer();
    }
    if (type == FlushType::kSingle) {
      break;
    }
  } while (invocation);
}

void MessageLoopImpl::RunExpiredTasksNow(bool single_task) {
  FlushTasks(single_task ? FlushType::kSingle : FlushType::kAll);
}

TaskQueueId MessageLoopImpl::GetTaskQueueId() const {
  return queue_id_;
}

}  // namespace fml
