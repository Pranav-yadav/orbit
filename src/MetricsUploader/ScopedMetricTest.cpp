// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "MetricsUploader/MetricsUploader.h"
#include "MetricsUploader/ScopedMetric.h"

namespace orbit_metrics_uploader {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Lt;

class MockUploader : public MetricsUploader {
 public:
  MOCK_METHOD(bool, SendLogEvent, (OrbitLogEvent::LogEventType /*log_event_type*/), (override));
  MOCK_METHOD(bool, SendLogEvent,
              (OrbitLogEvent::LogEventType /*log_event_type*/,
               std::chrono::milliseconds /*event_duration*/),
              (override));
  MOCK_METHOD(bool, SendLogEvent,
              (OrbitLogEvent::LogEventType /*log_event_type*/,
               std::chrono::milliseconds /*event_duration*/,
               OrbitLogEvent::StatusCode /*status_code*/),
              (override));
  MOCK_METHOD(bool, SendCaptureEvent,
              (OrbitCaptureData /*capture data*/, OrbitLogEvent::StatusCode /*status_code*/),
              (override));
};

TEST(ScopedMetric, Constructor) {
  { ScopedMetric metric{nullptr, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN}; }

  MockUploader uploader{};

  EXPECT_CALL(uploader,
              SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN, _, OrbitLogEvent::SUCCESS))
      .Times(1);

  { ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN}; }
}

TEST(ScopedMetric, SetStatusCode) {
  MockUploader uploader{};

  EXPECT_CALL(uploader,
              SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN, _, OrbitLogEvent::CANCELLED))
      .Times(1);

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    metric.SetStatusCode(OrbitLogEvent::CANCELLED);
  }
}

TEST(ScopedMetric, Sleep) {
  MockUploader uploader{};

  std::chrono::milliseconds sleep_time{1};

  EXPECT_CALL(uploader, SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN, Ge(sleep_time),
                                     OrbitLogEvent::SUCCESS))
      .Times(1);

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    std::this_thread::sleep_for(sleep_time);
  }
}

TEST(ScopedMetric, MoveAndSleep) {
  MockUploader uploader{};

  std::chrono::milliseconds sleep_time{1};

  EXPECT_CALL(uploader, SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN, Ge(sleep_time * 2),
                                     OrbitLogEvent::SUCCESS))
      .Times(1);

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    std::this_thread::sleep_for(sleep_time);

    [metric = std::move(metric), sleep_time]() { std::this_thread::sleep_for(sleep_time); }();
  }
}

TEST(ScopedMetric, PauseAndResume) {
  MockUploader uploader{};

  std::chrono::milliseconds sleep_time{200};

  EXPECT_CALL(uploader,
              SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN,
                           AllOf(Ge(sleep_time), Lt(sleep_time * 2)), OrbitLogEvent::SUCCESS))
      .Times(3);

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    std::this_thread::sleep_for(sleep_time / 2);

    metric.Pause();
    std::this_thread::sleep_for(sleep_time);
    metric.Resume();
    std::this_thread::sleep_for(sleep_time / 2);
  }

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    std::this_thread::sleep_for(sleep_time);

    metric.Pause();
    std::this_thread::sleep_for(sleep_time);
  }

  {
    ScopedMetric metric{&uploader, OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN};
    std::this_thread::sleep_for(sleep_time / 2);

    metric.Pause();
    ScopedMetric moved_metric{std::move(metric)};
    std::this_thread::sleep_for(sleep_time);
    moved_metric.Resume();
    std::this_thread::sleep_for(sleep_time / 2);
  }
}

}  // namespace orbit_metrics_uploader