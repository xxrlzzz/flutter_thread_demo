// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FML_MESSAGE_LOOP_TASK_QUEUES_H_
#define FLUTTER_FML_MESSAGE_LOOP_TASK_QUEUES_H_

#include <map>
#include <mutex>
#include <vector>

#include "flutter/fml/closure.h"
#include "flutter/fml/delayed_task.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/memory/ref_counted.h"
#include "flutter/fml/synchronization/shared_mutex.h"
#include "flutter/fml/wakeable.h"

namespace fml {

class TaskQueueId {
 public:
  explicit TaskQueueId(size_t value) : value_(value) {}

  operator int() const { return value_; }

 private:
  size_t value_;
};

// This is keyed by the |TaskQueueId| and contains all the queue
// components that make up a single TaskQueue.
class TaskQueueEntry {
 public:
  using TaskObservers = std::map<intptr_t, fml::closure>;
  Wakeable* wakeable;
  TaskObservers task_observers;
  DelayedTaskQueue delayed_tasks;

  TaskQueueEntry();

 private:
  FML_DISALLOW_COPY_ASSIGN_AND_MOVE(TaskQueueEntry);
};

enum class FlushType {
  kSingle,
  kAll,
};

// This class keeps track of all the tasks and observers that
// need to be run on it's MessageLoopImpl. This also wakes up the
// loop at the required times.
class MessageLoopTaskQueues
    : public fml::RefCountedThreadSafe<MessageLoopTaskQueues> {
 public:
  // Lifecycle.

  static fml::RefPtr<MessageLoopTaskQueues> GetInstance();

  TaskQueueId CreateTaskQueue();

  void Dispose(TaskQueueId queue_id);

  void DisposeTasks(TaskQueueId queue_id);

  // Tasks methods.

  void RegisterTask(TaskQueueId queue_id,
                    const fml::closure& task,
                    fml::TimePoint target_time);

  bool HasPendingTasks(TaskQueueId queue_id) const;

  fml::closure GetNextTaskToRun(TaskQueueId queue_id, fml::TimePoint from_time);

  size_t GetNumPendingTasks(TaskQueueId queue_id) const;

  // Observers methods.

  void AddTaskObserver(TaskQueueId queue_id,
                       intptr_t key,
                       const fml::closure& callback);

  void RemoveTaskObserver(TaskQueueId queue_id, intptr_t key);

  std::vector<fml::closure> GetObserversToNotify(TaskQueueId queue_id) const;

  // Misc.

  void SetWakeable(TaskQueueId queue_id, fml::Wakeable* wakeable);

 private:
  MessageLoopTaskQueues();

  ~MessageLoopTaskQueues();

  void WakeUpUnlocked(TaskQueueId queue_id, fml::TimePoint time) const;

  bool HasPendingTasksUnlocked(TaskQueueId queue_id) const;

  const DelayedTask& PeekNextTaskUnlocked(TaskQueueId queue_id) const;

  // fml::TimePoint GetNextWakeTimeUnlocked(TaskQueueId queue_id) const;

  static fml::RefPtr<MessageLoopTaskQueues> instance_;

  mutable std::mutex queue_mutex_;
  std::map<TaskQueueId, std::unique_ptr<TaskQueueEntry>> queue_entries_;

  size_t task_queue_id_counter_;
  size_t task_order_counter_;

  FML_FRIEND_MAKE_REF_COUNTED(MessageLoopTaskQueues);
  FML_FRIEND_REF_COUNTED_THREAD_SAFE(MessageLoopTaskQueues);
  FML_DISALLOW_COPY_ASSIGN_AND_MOVE(MessageLoopTaskQueues);
};

}  // namespace fml

#endif  // FLUTTER_FML_MESSAGE_LOOP_TASK_QUEUES_H_
