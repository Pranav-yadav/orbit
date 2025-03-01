// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/flags/flag.h>

#include <cstdint>
#include <string>
#include <vector>

ABSL_FLAG(bool, devmode, false, "Enable developer mode in the client's UI");

ABSL_FLAG(bool, nodeploy, false, "Disable automatic deployment of OrbitService");

ABSL_FLAG(std::string, collector, "", "Full path of collector to be deployed");

ABSL_FLAG(std::string, collector_root_password, "", "Collector's machine root password");

ABSL_FLAG(uint16_t, grpc_port, 44765,
          "The service's GRPC server port (use default value if unsure)");
ABSL_FLAG(bool, local, false, "Connects to local instance of OrbitService");

ABSL_FLAG(std::string, process_name, "",
          "Automatically select and connect to the specified process");

ABSL_FLAG(bool, enable_tutorials_feature, false, "Enable tutorials");

// TODO(kuebler): remove this once we have the validator complete
ABSL_FLAG(bool, enable_frame_pointer_validator, false, "Enable validation of frame pointers");

// TODO: Remove this flag once we have a way to toggle the display return values
ABSL_FLAG(bool, show_return_values, false, "Show return values on time slices");

ABSL_FLAG(bool, enable_tracepoint_feature, false,
          "Enable the setting of the panel of kernel tracepoints");

// TODO(b/185099421): Remove this flag once we have a clear explanation of the memory warning
// threshold (i.e., production limit).
ABSL_FLAG(bool, enable_warning_threshold, false,
          "Enable setting and showing the memory warning threshold");

// Additional folder in which OrbitService will look for symbols
ABSL_FLAG(std::string, instance_symbols_folder, "",
          "Additional folder in which OrbitService will look for symbols");

ABSL_FLAG(bool, enforce_full_redraw, false,
          "Enforce full redraw every frame (used for performance measurements)");

// VSI
ABSL_FLAG(std::string, target_process, "",
          "Process name or path. Specify this together with --target_instance to skip the "
          "connection setup and open the main window instead. If the process can't be found or "
          "deployment is aborted by the user Orbit will exit with return code -1 immediately. "
          "If multiple instances of the same process exist, the one with the highest PID will be "
          "chosen.");
ABSL_FLAG(std::string, target_instance, "",
          "Instance name or id. Specify this together with --target_process to skip the "
          "connection setup and open the main window instead. If the instance can't be found or "
          "deployment is aborted by the user Orbit will exit with return code -1 immediately.");
ABSL_FLAG(std::vector<std::string>, additional_symbol_paths, {},
          "Additional local symbol locations (comma-separated)");
ABSL_FLAG(bool, launched_from_vsi, false, "Indicates Orbit was launched from VSI.");

// TestHub custom protocol support
ABSL_FLAG(std::string, target_uri, "",
          "Target URI in the format orbitprofiler://instance?process. Specify this to skip the "
          "connection setup and open the main window instead. If the process can't be found or "
          "deployment is aborted by the user Orbit will exit with return code -1 immediately. "
          "If multiple instances of the same process exist, the one with the highest PID will be "
          "chosen.");

// Clears QSettings. This is intended for e2e tests.
ABSL_FLAG(bool, clear_settings, false,
          "Clears user defined settings. This includes symbol locations and source path mappings.");

// TODO(http://b/170712621): Remove this flag when we decide which timestamp format we will use.
ABSL_FLAG(bool, iso_timestamps, true, "Show timestamps using ISO-8601 format.");

ABSL_FLAG(bool, enable_unsafe_symbols, false,
          "Enable the possibility to use symbol files that do not have a matching build ID.");

ABSL_FLAG(
    bool, auto_symbol_loading, true,
    "Enable automatic symbol loading. This is turned on by default. If Orbit becomes unresponsive, "
    "try turning automatic symbol loading off (--auto_symbol_loading=false)");

ABSL_FLAG(bool, auto_frame_track, true, "Automatically add the default Frame Track.");

ABSL_FLAG(bool, time_range_selection, false, "Enable time range selection feature.");
