// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#define FML_USED_ON_EMBEDDER

#include "flutter/fml/message_loop_task_queues.h"

#include <iostream>

#include "flutter/fml/make_copyable.h"
#include "flutter/fml/message_loop_impl.h"

namespace fml {

fml::RefPtr<MessageLoopTaskQueues> MessageLoopTaskQueues::instance_;

TaskQueueEntry::TaskQueueEntry()
    : wakeable(nullptr), task_observers(), delayed_tasks() {}

fml::RefPtr<MessageLoopTaskQueues> MessageLoopTaskQueues::GetInstance() {
  static std::mutex creation_mutex;
  std::scoped_lock creation(creation_mutex);
  if (!instance_) {
    instance_ = fml::MakeRefCounted<MessageLoopTaskQueues>();
  }
  return instance_;
}

TaskQueueId MessageLoopTaskQueues::CreateTaskQueue() {
  std::scoped_lock lock_queue(queue_mutex_);
  TaskQueueId loop_id = TaskQueueId(task_queue_id_counter_);
  ++task_queue_id_counter_;
  queue_entries_.insert({loop_id, std::make_unique<TaskQueueEntry>()});
  // queue_entries_[loop_id] = std::make_unique<TaskQueueEntry>();
  return loop_id;
}

MessageLoopTaskQueues::MessageLoopTaskQueues()
    : task_queue_id_counter_(0), task_order_counter_(0) {}

MessageLoopTaskQueues::~MessageLoopTaskQueues() = default;

void MessageLoopTaskQueues::Dispose(TaskQueueId queue_id) {
  std::scoped_lock lock_queue(queue_mutex_);
  const auto& queue_entry = queue_entries_.at(queue_id);
  queue_entries_.erase(queue_id);
}

void MessageLoopTaskQueues::DisposeTasks(TaskQueueId queue_id) {
  std::scoped_lock lock_queue(queue_mutex_);
  const auto& queue_entry = queue_entries_.at(queue_id);
  queue_entry->delayed_tasks = {};
}

void MessageLoopTaskQueues::RegisterTask(TaskQueueId queue_id,
                                         const fml::closure& task,
                                         fml::TimePoint target_time) {
  std::scoped_lock lock_queue(queue_mutex_);
  size_t order = task_order_counter_;
  task_order_counter_++;
  const auto& queue_entry = queue_entries_.at(queue_id);
  queue_entry->delayed_tasks.push({order, task, target_time});
  WakeUpUnlocked(queue_id, PeekNextTaskUnlocked(queue_id).GetTargetTime());
}

bool MessageLoopTaskQueues::HasPendingTasks(TaskQueueId queue_id) const {
  std::scoped_lock lock_queue(queue_mutex_);
  return HasPendingTasksUnlocked(queue_id);
}

fml::closure MessageLoopTaskQueues::GetNextTaskToRun(TaskQueueId queue_id,
                                                     fml::TimePoint from_time) {
  std::scoped_lock lock_queue(queue_mutex_);
  if (!HasPendingTasksUnlocked(queue_id)) {
    return nullptr;
  }

  const auto& entry = queue_entries_.at(queue_id);
  const auto& top = entry->delayed_tasks.top();

  WakeUpUnlocked(queue_id, top.GetTargetTime());

  if (top.GetTargetTime() > from_time) {
    return nullptr;
  }
  fml::closure invocation = top.GetTask();
  entry->delayed_tasks.pop();
  return invocation;
}

size_t MessageLoopTaskQueues::GetNumPendingTasks(TaskQueueId queue_id) const {
  std::scoped_lock lock_queue(queue_mutex_);
  const auto& queue_entry = queue_entries_.at(queue_id);
  return queue_entry->delayed_tasks.size();
}

void MessageLoopTaskQueues::AddTaskObserver(TaskQueueId queue_id,
                                            intptr_t key,
                                            const fml::closure& callback) {
  std::scoped_lock lock_queue(queue_mutex_);
  FML_DCHECK(callback != nullptr) << "Observer callback must be non-null.";
  queue_entries_.at(queue_id)->task_observers[key] = callback;
}

void MessageLoopTaskQueues::RemoveTaskObserver(TaskQueueId queue_id,
                                               intptr_t key) {
  std::scoped_lock lock_queue(queue_mutex_);
  queue_entries_.at(queue_id)->task_observers.erase(key);
}

std::vector<fml::closure> MessageLoopTaskQueues::GetObserversToNotify(
    TaskQueueId queue_id) const {
  std::scoped_lock lock_queue(queue_mutex_);
  std::vector<fml::closure> observers;

  for (const auto& observer : queue_entries_.at(queue_id)->task_observers) {
    observers.push_back(observer.second);
  }

  return observers;
}

void MessageLoopTaskQueues::SetWakeable(TaskQueueId queue_id,
                                        fml::Wakeable* wakeable) {
  std::scoped_lock lock_queue(queue_mutex_);
  FML_CHECK(!queue_entries_.at(queue_id)->wakeable)
      << "Wakeable can only be set once.";
  queue_entries_.at(queue_id)->wakeable = wakeable;
}

void MessageLoopTaskQueues::WakeUpUnlocked(TaskQueueId queue_id,
                                           fml::TimePoint time) const {
  if (queue_entries_.at(queue_id)->wakeable) {
    queue_entries_.at(queue_id)->wakeable->WakeUp(time);
  }
}

// Subsumed queues will never have pending tasks.
// Owning queues will consider both their and their subsumed tasks.
bool MessageLoopTaskQueues::HasPendingTasksUnlocked(
    TaskQueueId queue_id) const {
  const auto& entry = queue_entries_.at(queue_id);
  return !entry->delayed_tasks.empty();
}

const DelayedTask& MessageLoopTaskQueues::PeekNextTaskUnlocked(
    TaskQueueId queue_id) const {
  FML_DCHECK(HasPendingTasksUnlocked(queue_id));
  const auto& entry = queue_entries_.at(queue_id);
  return entry->delayed_tasks.top();
}

}  // namespace fml
