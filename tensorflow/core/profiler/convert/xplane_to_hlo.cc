/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/profiler/convert/xplane_to_hlo.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "tensorflow/compiler/xla/service/hlo.pb.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/status.h"
#include "tensorflow/core/platform/statusor.h"
#include "tensorflow/core/profiler/convert/repository.h"
#include "tensorflow/core/profiler/protobuf/xplane.pb.h"
#include "tensorflow/core/profiler/utils/file_system_utils.h"
#include "tensorflow/core/profiler/utils/hlo_proto_map.h"

namespace tensorflow {
namespace profiler {

namespace {

constexpr char kNoModuleIdentifier[] = "NO_MODULE";
constexpr char kHloProtoSuffix[] = ".hlo_proto.pb";

}  // namespace

StatusOr<xla::HloProto> GetHloProtoByModuleName(
    const SessionSnapshot& session_snapshot,
    const absl::string_view module_name) {
  std::string file_name =
      ProfilerJoinPath(session_snapshot.GetSessionRunDir(),
                       absl::StrCat(module_name, kHloProtoSuffix));
  xla::HloProto hlo_proto;
  TF_RETURN_IF_ERROR(tensorflow::ReadBinaryProto(tensorflow::Env::Default(),
                                                 file_name, &hlo_proto));
  return hlo_proto;
}

Status GetHloProtoFromMultiXSpaceAndSaveToFile(
    const SessionSnapshot& session_snapshot) {
  // Get all HLO protos from XSpaces and deduplicate.
  HloProtoMap hlo_proto_map;
  for (int i = 0; i < session_snapshot.XSpaceSize(); i++) {
    TF_ASSIGN_OR_RETURN(std::unique_ptr<XSpace> xspace,
                        session_snapshot.GetXSpace(i));
    hlo_proto_map.AddHloProtosFromXSpace(*xspace);
  }

  std::vector<absl::string_view> module_list = hlo_proto_map.GetModuleList();
  // Write an empty identifier if there is no HLO module.
  if (module_list.empty()) {
    std::string file_name =
        ProfilerJoinPath(session_snapshot.GetSessionRunDir(),
                         absl::StrCat(kNoModuleIdentifier, kHloProtoSuffix));
    xla::HloProto empty_hlo;
    TF_RETURN_IF_ERROR(tensorflow::WriteBinaryProto(tensorflow::Env::Default(),
                                                    file_name, empty_hlo));
    return OkStatus();
  }

  // Save HLO protos to session run directory.
  for (const absl::string_view module_name : module_list) {
    auto hlo_proto_or = hlo_proto_map.GetHloProtoByModuleName(module_name);
    if (!hlo_proto_or.ok()) {
      return errors::Internal(hlo_proto_or.status().message());
    }
    std::string file_name =
        ProfilerJoinPath(session_snapshot.GetSessionRunDir(),
                         absl::StrCat(module_name, kHloProtoSuffix));
    TF_RETURN_IF_ERROR(tensorflow::WriteBinaryProto(
        tensorflow::Env::Default(), file_name, *hlo_proto_or.value()));
  }

  return OkStatus();
}

}  // namespace profiler
}  // namespace tensorflow
