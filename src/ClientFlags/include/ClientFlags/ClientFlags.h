// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_FLAGS_CLIENT_FLAGS_H_
#define CLIENT_FLAGS_CLIENT_FLAGS_H_

#include <absl/flags/declare.h>

#include <cstdint>
#include <string>
#include <vector>

ABSL_DECLARE_FLAG(bool, devmode);

ABSL_DECLARE_FLAG(bool, nodeploy);

ABSL_DECLARE_FLAG(std::string, collector);

ABSL_DECLARE_FLAG(std::string, collector_root_password);

ABSL_DECLARE_FLAG(uint16_t, grpc_port);

ABSL_DECLARE_FLAG(bool, local);

ABSL_DECLARE_FLAG(std::string, process_name);

ABSL_DECLARE_FLAG(bool, enable_tutorials_feature);

// TODO(b/160549506): Remove this flag once it can be specified in the ui.
ABSL_DECLARE_FLAG(bool, frame_pointer_unwinding);

// TODO(kuebler): remove this once we have the validator complete
ABSL_DECLARE_FLAG(bool, enable_frame_pointer_validator);

// TODO: Remove this flag once we have a way to toggle the display return values
ABSL_DECLARE_FLAG(bool, show_return_values);

ABSL_DECLARE_FLAG(bool, enable_tracepoint_feature);

// TODO(b/185099421): Remove this flag once we have a clear explanation of the memory warning
// threshold (i.e., production limit).
ABSL_DECLARE_FLAG(bool, enable_warning_threshold);

// additional folder in which OrbitService will look for symbols
ABSL_DECLARE_FLAG(std::string, instance_symbols_folder);

ABSL_DECLARE_FLAG(bool, enforce_full_redraw);

// VSI
ABSL_DECLARE_FLAG(std::string, target_process);
ABSL_DECLARE_FLAG(std::string, target_instance);
ABSL_DECLARE_FLAG(std::vector<std::string>, additional_symbol_paths);
ABSL_DECLARE_FLAG(bool, launched_from_vsi);

// TestHub custom protocol support
ABSL_DECLARE_FLAG(std::string, target_uri);

// Clears QSettings. This is intended for e2e tests.
ABSL_DECLARE_FLAG(bool, clear_settings);

// TODO(http://b/170712621): Remove this flag when we decide which timestamp format we will use.
ABSL_DECLARE_FLAG(bool, iso_timestamps);

// Enables to use symbol files without a build id, or with a mismatching build ID
ABSL_DECLARE_FLAG(bool, enable_unsafe_symbols);

// Enables automatic symbol loading
ABSL_DECLARE_FLAG(bool, auto_symbol_loading);

ABSL_DECLARE_FLAG(bool, auto_frame_track);

// Enables time range selection feature.
ABSL_DECLARE_FLAG(bool, time_range_selection);

#endif  // CLIENT_FLAGS_CLIENT_FLAGS_H_
