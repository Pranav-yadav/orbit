// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ClientData/ModuleData.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <filesystem>

#include "GrpcProtos/module.pb.h"
#include "OrbitBase/Logging.h"
#include "absl/synchronization/mutex.h"

using orbit_grpc_protos::ModuleInfo;

namespace orbit_client_data {

std::vector<orbit_grpc_protos::ModuleInfo::ObjectSegment> ModuleData::GetObjectSegments() const {
  absl::MutexLock lock(&mutex_);
  return {module_info_.object_segments().begin(), module_info_.object_segments().end()};
}

uint64_t ModuleData::ConvertFromVirtualAddressToOffsetInFile(uint64_t virtual_address) const {
  absl::MutexLock lock(&mutex_);

  if (object_file_type() == orbit_grpc_protos::ModuleInfo::kElfFile) {
    // For ELF files, we define the load bias as the difference between the executable loadable
    // segment's address and its offset. So note how, for the executable loadable segment (which we
    // assume functions belong to), this computation and the generic one below are equivalent:
    // load_bias = executable_loadable_segment_address - executable_loadable_segment_offset
    // function_address - load_bias = function_address - executable_loadable_segment_address +
    //                                executable_loadable_segment_offset
    return virtual_address - load_bias();
  }

  for (const orbit_grpc_protos::ModuleInfo::ObjectSegment& segment :
       module_info_.object_segments()) {
    if (segment.address() <= virtual_address &&
        virtual_address < segment.address() + segment.size_in_memory()) {
      return virtual_address - segment.address() + segment.offset_in_file();
    }
  }

  // Fall back to the ELF-specific computation if we didn't find a containing segment.
  return virtual_address - load_bias();
}

uint64_t ModuleData::ConvertFromOffsetInFileToVirtualAddress(uint64_t offset_in_file) const {
  absl::MutexLock lock(&mutex_);

  if (object_file_type() == orbit_grpc_protos::ModuleInfo::kElfFile) {
    return offset_in_file + load_bias();
  }

  for (const orbit_grpc_protos::ModuleInfo::ObjectSegment& segment :
       module_info_.object_segments()) {
    if (segment.offset_in_file() <= offset_in_file &&
        offset_in_file < segment.offset_in_file() + segment.size_in_file()) {
      return offset_in_file - segment.offset_in_file() + segment.address();
    }
  }

  return offset_in_file + load_bias();
}

bool ModuleData::is_loaded() const {
  absl::MutexLock lock(&mutex_);
  return is_loaded_;
}

bool ModuleData::NeedsUpdate(const orbit_grpc_protos::ModuleInfo& info) const {
  return name() != info.name() || file_size() != info.file_size() ||
         load_bias() != info.load_bias();
}

bool ModuleData::UpdateIfChangedAndUnload(ModuleInfo info) {
  absl::MutexLock lock(&mutex_);

  ORBIT_CHECK(file_path() == info.file_path());
  ORBIT_CHECK(build_id() == info.build_id());
  ORBIT_CHECK(object_file_type() == info.object_file_type());

  if (!NeedsUpdate(info)) return false;

  // The update only makes sense if build_id is empty.
  ORBIT_CHECK(build_id().empty());

  module_info_ = std::move(info);

  ORBIT_LOG("WARNING: Module \"%s\" changed and will to be updated (it does not have build_id).",
            file_path());

  if (!is_loaded_) return false;

  ORBIT_LOG("Module %s contained symbols. Because the module changed, those are now removed.",
            file_path());
  functions_.clear();
  hash_to_function_map_.clear();
  is_loaded_ = false;

  return true;
}

bool ModuleData::UpdateIfChangedAndNotLoaded(orbit_grpc_protos::ModuleInfo info) {
  absl::MutexLock lock(&mutex_);

  ORBIT_CHECK(file_path() == info.file_path());
  ORBIT_CHECK(build_id() == info.build_id());
  ORBIT_CHECK(object_file_type() == info.object_file_type());

  if (!NeedsUpdate(info)) return true;

  // The update only makes sense if build_id is empty.
  ORBIT_CHECK(build_id().empty());

  if (is_loaded_) return false;

  module_info_ = std::move(info);
  return true;
}

const FunctionInfo* ModuleData::FindFunctionByVirtualAddress(uint64_t virtual_address,
                                                             bool is_exact) const {
  absl::MutexLock lock(&mutex_);
  if (functions_.empty()) return nullptr;

  if (is_exact) {
    auto it = functions_.find(virtual_address);
    return (it != functions_.end()) ? it->second.get() : nullptr;
  }

  auto it = functions_.upper_bound(virtual_address);
  if (it == functions_.begin()) return nullptr;

  --it;
  FunctionInfo* function = it->second.get();
  ORBIT_CHECK(function->address() <= virtual_address);

  if (function->address() + function->size() < virtual_address) return nullptr;

  return function;
}

void ModuleData::AddSymbols(const orbit_grpc_protos::ModuleSymbols& module_symbols) {
  absl::MutexLock lock(&mutex_);
  ORBIT_CHECK(!is_loaded_);

  uint32_t address_reuse_counter = 0;
  uint32_t name_reuse_counter = 0;
  for (const orbit_grpc_protos::SymbolInfo& symbol_info : module_symbols.symbol_infos()) {
    auto [inserted_it, success_functions] = functions_.try_emplace(
        symbol_info.address(),
        std::make_unique<FunctionInfo>(symbol_info, file_path(), build_id()));
    FunctionInfo* function = inserted_it->second.get();
    // It happens that the same address has multiple symbol names associated
    // with it. For example: (all the same address)
    // __cxxabiv1::__enum_type_info::~__enum_type_info()
    // __cxxabiv1::__shim_type_info::~__shim_type_info()
    // __cxxabiv1::__array_type_info::~__array_type_info()
    // __cxxabiv1::__class_type_info::~__class_type_info()
    // __cxxabiv1::__pbase_type_info::~__pbase_type_info()
    if (success_functions) {
      ORBIT_CHECK(!function->pretty_name().empty());
      // Be careful about the scope, the key is a string_view. This is done to avoid name
      // duplication.
      bool success_function_name =
          name_to_function_info_map_.try_emplace(function->pretty_name(), function).second;
      if (!success_function_name) {
        name_reuse_counter++;
      }

      hash_to_function_map_.try_emplace(function->GetPrettyNameHash(), function);
    } else {
      address_reuse_counter++;
    }
  }
  if (address_reuse_counter != 0) {
    ORBIT_LOG("Warning: %d absolute addresses are used by more than one symbol",
              address_reuse_counter);
  }
  if (name_reuse_counter != 0) {
    ORBIT_LOG(
        "Warning: %d function name collisions happened (functions with the same demangled name). "
        "This is currently not supported by presets, since the presets are based on the demangled "
        "name.",
        name_reuse_counter);
  }

  is_loaded_ = true;
}

const FunctionInfo* ModuleData::FindFunctionFromHash(uint64_t hash) const {
  absl::MutexLock lock(&mutex_);
  return hash_to_function_map_.contains(hash) ? hash_to_function_map_.at(hash) : nullptr;
}

const FunctionInfo* ModuleData::FindFunctionFromPrettyName(std::string_view pretty_name) const {
  absl::MutexLock lock(&mutex_);
  auto it = name_to_function_info_map_.find(pretty_name);
  return it != name_to_function_info_map_.end() ? it->second : nullptr;
}

std::vector<const FunctionInfo*> ModuleData::GetFunctions() const {
  absl::MutexLock lock(&mutex_);
  std::vector<const FunctionInfo*> result;
  result.reserve(functions_.size());
  for (const auto& pair : functions_) {
    result.push_back(pair.second.get());
  }
  return result;
}

}  // namespace orbit_client_data
