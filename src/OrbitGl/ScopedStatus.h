// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_SCOPED_STATUS_H_
#define ORBIT_GL_SCOPED_STATUS_H_

#include <string>
#include <thread>

#include "OrbitBase/Executor.h"
#include "OrbitBase/Logging.h"
#include "StatusListener.h"

/**
 * This class holds scope for status. It also takes care
 * of updating status on the main thread even if Update
 * is called from a different thread.
 *
 * Usage example:
 *
 * {
 *   ScopedStatus scoped_status(main_thread_executor, status_listener_, "Waiting for fish");
 *
 *   ...
 *
 *   scoped_status.Update("Still waiting for fish!");
 *
 *   ...
 *
 *   // Once out of scope it will clear the message.
 * }
 *
 * See also StatusListener class.
 */
class ScopedStatus final {
 public:
  ScopedStatus() = default;
  explicit ScopedStatus(std::weak_ptr<orbit_base::Executor> executor,
                        std::weak_ptr<StatusListener> maybe_status_listener,
                        const std::string& status_message) {
    data_ = std::make_unique<Data>();
    data_->executor = std::move(executor);
    data_->status_listener = std::move(maybe_status_listener);
    data_->main_thread_id = std::this_thread::get_id();

    std::shared_ptr<StatusListener> status_listener = data_->status_listener.lock();
    if (status_listener == nullptr) return;
    data_->status_id = status_listener->AddStatus(status_message);
  }

  ScopedStatus(const ScopedStatus& that) = delete;
  ScopedStatus& operator=(const ScopedStatus& that) = delete;

  ScopedStatus(ScopedStatus&& that) = default;

  ScopedStatus& operator=(ScopedStatus&& that) {
    if (this == &that) {
      return *this;
    }

    reset();
    data_ = std::move(that.data_);
    return *this;
  }

  ~ScopedStatus() { reset(); }

  void UpdateMessage(const std::string& message) {
    ORBIT_CHECK(data_ != nullptr);
    if (std::this_thread::get_id() == data_->main_thread_id) {
      std::shared_ptr<StatusListener> status_listener = data_->status_listener.lock();
      if (status_listener == nullptr) return;
      status_listener->UpdateStatus(data_->status_id, message);
    } else {
      TrySchedule(data_->executor, [status_id = data_->status_id,
                                    maybe_status_listener = data_->status_listener, message] {
        std::shared_ptr<StatusListener> status_listener = maybe_status_listener.lock();
        if (status_listener == nullptr) return;
        status_listener->UpdateStatus(status_id, message);
      });
    }
  }

 private:
  void reset() {
    if (!data_) {
      return;
    }

    if (std::this_thread::get_id() == data_->main_thread_id) {
      std::shared_ptr<StatusListener> status_listener = data_->status_listener.lock();
      if (status_listener == nullptr) return;
      status_listener->ClearStatus(data_->status_id);
    } else {
      TrySchedule(data_->executor,
                  [maybe_status_listener = data_->status_listener, status_id = data_->status_id] {
                    auto status_listener = maybe_status_listener.lock();
                    if (status_listener == nullptr) return;
                    status_listener->ClearStatus(status_id);
                  });
    }

    data_.reset();
  }

  // Instances fo this class are going to be moved a lot so we want data to be
  // stored in easily movable form.
  struct Data {
    std::weak_ptr<orbit_base::Executor> executor;
    std::weak_ptr<StatusListener> status_listener;
    std::thread::id main_thread_id;
    uint64_t status_id = 0;
  };

  std::unique_ptr<Data> data_;
};

#endif  // ORBIT_GL_SCOPED_STATUS_H_
