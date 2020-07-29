// Copyright (c) 2018-2020, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/backends/tensorflow/graphdef_backend_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "src/backends/tensorflow/graphdef_backend.h"
#include "src/backends/tensorflow/tf_virtual_device.h"
#include "src/core/constants.h"
#include "src/core/filesystem.h"
#include "src/core/logging.h"
#include "src/core/model_config.pb.h"
#include "src/core/model_config_utils.h"

namespace nvidia { namespace inferenceserver {

Status
GraphDefBackendFactory::Create(
    const std::shared_ptr<BackendConfig>& backend_config,
    std::unique_ptr<GraphDefBackendFactory>* factory)
{
  LOG_VERBOSE(1) << "Create GraphDefBackendFactory";

  auto graphdef_backend_config =
      std::static_pointer_cast<Config>(backend_config);
  factory->reset(new GraphDefBackendFactory(graphdef_backend_config));

  // Initalize VGPUs if required
  VirtualDeviceTracker::Init(graphdef_backend_config->memory_limit_mb);

  return Status::Success;
}

Status
GraphDefBackendFactory::CreateBackend(
    const std::string& nonlocal_path, const inference::ModelConfig& model_config,
    const double min_compute_capability,
    std::unique_ptr<InferenceBackend>* backend)
{
  // Localize 'nonlocal_path' so that the entire model version
  // directory is available locally.
  std::shared_ptr<LocalizedDirectory> local_dir;
  RETURN_IF_ERROR(LocalizeDirectory(nonlocal_path, &local_dir));

  // Read all the graphdef files in 'local_dir'.
  std::set<std::string> graphdef_files;
  RETURN_IF_ERROR(GetDirectoryFiles(
      local_dir->Path(), true /* skip_hidden_files */, &graphdef_files));

  std::unordered_map<std::string, std::string> models;
  for (const auto& filename : graphdef_files) {
    const auto graphdef_path = JoinPath({local_dir->Path(), filename});
    models.emplace(
        std::piecewise_construct, std::make_tuple(filename),
        std::make_tuple(graphdef_path));
  }

  // Create the backend for the model and all the execution contexts
  // requested for this model.
  std::unique_ptr<GraphDefBackend> local_backend(
      new GraphDefBackend(min_compute_capability));
  RETURN_IF_ERROR(local_backend->Init(
      nonlocal_path, model_config, backend_config_.get(),
      kTensorFlowGraphDefPlatform));
  RETURN_IF_ERROR(local_backend->CreateExecutionContexts(models));

  *backend = std::move(local_backend);
  return Status::Success;
}

}}  // namespace nvidia::inferenceserver
