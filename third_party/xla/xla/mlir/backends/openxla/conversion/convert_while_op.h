/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

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

#ifndef XLA_MLIR_BACKENDS_OPENXLA_CONVERSION_CONVERT_WHILE_OP_H_
#define XLA_MLIR_BACKENDS_OPENXLA_CONVERSION_CONVERT_WHILE_OP_H_

#include <memory>

#include "mlir/IR/PatternMatch.h"  // from @llvm-project
#include "mlir/Transforms/DialectConversion.h"  // from @llvm-project
#include "xla/mlir/backends/openxla/conversion/de_bufferization.h"

namespace xla {
namespace gpu {

// Appends patterns to convert while loops to scf.while operations.
void populateWhileOpConversionPatterns(mlir::RewritePatternSet &patterns,
                                       mlir::TypeConverter &converter,
                                       std::shared_ptr<DeBufferization> state);

}  // namespace gpu
}  // namespace xla

#endif  // XLA_MLIR_BACKENDS_OPENXLA_CONVERSION_CONVERT_WHILE_OP_H_
