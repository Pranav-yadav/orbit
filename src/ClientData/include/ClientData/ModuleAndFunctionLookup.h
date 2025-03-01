// Copyright (c) 2022 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_DATA_MODULE_AND_FUNCTION_LOOKUP_H_
#define CLIENT_DATA_MODULE_AND_FUNCTION_LOOKUP_H_

#include "CaptureData.h"
#include "FunctionInfo.h"
#include "ModuleManager.h"
#include "ProcessData.h"

namespace orbit_client_data {

const std::string kUnknownFunctionOrModuleName{"???"};

[[nodiscard]] const std::string& GetFunctionNameByAddress(const ModuleManager& module_manager,
                                                          const CaptureData& capture_data,
                                                          uint64_t absolute_address);

[[nodiscard]] std::optional<uint64_t> FindFunctionAbsoluteAddressByInstructionAbsoluteAddress(
    const ModuleManager& module_manager, const CaptureData& capture_data,
    uint64_t absolute_address);

[[nodiscard]] const FunctionInfo* FindFunctionByModulePathBuildIdAndVirtualAddress(
    const ModuleManager& module_manager, const std::string& module_path,
    const std::string& build_id, uint64_t virtual_address);

[[nodiscard]] const std::string& GetModulePathByAddress(const ModuleManager& module_manager,
                                                        const CaptureData& capture_data,
                                                        uint64_t absolute_address);

[[nodiscard]] std::pair<const std::string&, std::optional<std::string>>
FindModulePathAndBuildIdByAddress(const ModuleManager& module_manager,
                                  const CaptureData& capture_data, uint64_t absolute_address);

[[nodiscard]] const FunctionInfo* FindFunctionByAddress(const ProcessData& process,
                                                        const ModuleManager& module_manager,
                                                        uint64_t absolute_address, bool is_exact);

[[nodiscard]] const orbit_client_data::ModuleData* FindModuleByAddress(
    const ProcessData& process, const ModuleManager& module_manager, uint64_t absolute_address);

[[nodiscard]] std::optional<uint64_t> FindInstrumentedFunctionIdSlow(
    const CaptureData& capture_data, const FunctionInfo& function_info);

}  // namespace orbit_client_data

#endif  // CLIENT_DATA_MODULE_AND_FUNCTION_LOOKUP_H_