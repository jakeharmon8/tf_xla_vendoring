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

#include "tensorflow/core/profiler/convert/repository.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/path.h"
#include "tensorflow/core/platform/status.h"
#include "tensorflow/core/platform/statusor.h"
#include "tsl/profiler/protobuf/xplane.pb.h"

namespace tensorflow {
namespace profiler {
namespace {
std::string GetHostnameByPath(absl::string_view xspace_path) {
  std::string file_name = std::string(tensorflow::io::Basename(xspace_path));
  std::vector<std::string> parts = absl::StrSplit(file_name, '.');
  return parts[0];
}
}  // namespace

StatusOr<SessionSnapshot> SessionSnapshot::Create(
    std::vector<std::string> xspace_paths,
    std::optional<std::vector<std::unique_ptr<XSpace>>> xspaces) {
  if (xspace_paths.empty()) {
    return errors::InvalidArgument("Can not find XSpace path.");
  }

  if (xspaces.has_value()) {
    if (xspaces->size() != xspace_paths.size()) {
      return errors::InvalidArgument(
          "The size of the XSpace paths: ", xspace_paths.size(),
          " is not equal ",
          "to the size of the XSpace proto: ", xspaces->size());
    }
    for (size_t i = 0; i < xspace_paths.size(); ++i) {
      auto host_name = GetHostnameByPath(xspace_paths.at(i));
      if (xspaces->at(i)->hostnames_size() > 0 && !host_name.empty()) {
        if (!absl::StrContains(host_name, xspaces->at(i)->hostnames(0))) {
          return errors::InvalidArgument(
              "The hostname of xspace path and preloaded xpace don't match at "
              "index: ",
              i, ". \nThe host name of xpace path is ", host_name,
              " but the host name of preloaded xpace is ",
              xspaces->at(i)->hostnames(0), ".");
        }
      }
    }
  }

  return SessionSnapshot(std::move(xspace_paths), std::move(xspaces));
}

StatusOr<std::unique_ptr<XSpace>> SessionSnapshot::GetXSpace(
    size_t index) const {
  if (index >= xspace_paths_.size()) {
    return errors::InvalidArgument("Can not get the ", index,
                                   "th XSpace. The total number of XSpace is ",
                                   xspace_paths_.size());
  }

  // Return the pre-loaded XSpace proto.
  if (xspaces_.has_value()) {
    if (xspaces_->at(index) == nullptr) {
      return errors::Internal("");
    }
    return std::move(xspaces_->at(index));
  }

  // Return the XSpace proto from file.
  auto xspace_from_file = std::make_unique<XSpace>();
  TF_RETURN_IF_ERROR(tensorflow::ReadBinaryProto(tensorflow::Env::Default(),
                                                 xspace_paths_.at(index),
                                                 xspace_from_file.get()));
  return xspace_from_file;
}

StatusOr<std::unique_ptr<XSpace>> SessionSnapshot::GetXSpaceByName(
    absl::string_view name) const {
  if (auto it = hostname_map_.find(name); it != hostname_map_.end()) {
    return GetXSpace(it->second);
  }

  return errors::InvalidArgument("Can not find the XSpace by name: ", name,
                                 ". The total number of XSpace is ",
                                 xspace_paths_.size());
}

std::string SessionSnapshot::GetHostname(size_t index) const {
  return GetHostnameByPath(xspace_paths_.at(index));
}

std::optional<std::string> SessionSnapshot::GetFilePath(
    absl::string_view toolname, absl::string_view hostname) const {
  if (!has_accessible_run_dir_) return std::nullopt;
  std::string file_name = "";
  if (toolname == "trace_viewer@")
    file_name = absl::StrCat(hostname, ".", "SSTABLE");
  if (!file_name.empty())
    return tensorflow::io::JoinPath(session_run_dir_, file_name);
  return std::nullopt;
}

}  // namespace profiler
}  // namespace tensorflow
