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

#include "xla/mlir/backends/openxla/conversion/convert_while_op.h"

#include <memory>
#include <utility>

#include "third_party/iree/llvm-external-projects/iree-dialects/include/iree-dialects/Dialect/Input/InputOps.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/Arith/IR/Arith.h"  // from @llvm-project
#include "mlir/Dialect/Bufferization/IR/Bufferization.h"  // from @llvm-project
#include "mlir/Dialect/MemRef/IR/MemRef.h"  // from @llvm-project
#include "mlir/Dialect/SCF/IR/SCF.h"  // from @llvm-project
#include "mlir/IR/BuiltinTypes.h"  // from @llvm-project
#include "mlir/IR/ImplicitLocOpBuilder.h"  // from @llvm-project
#include "mlir/IR/Value.h"  // from @llvm-project
#include "xla/mlir_hlo/lhlo/IR/lhlo_ops.h"

namespace xla {
namespace gpu {

namespace {
using namespace mlir;                 // NOLINT
using namespace mlir::iree_compiler;  // NOLINT

//===----------------------------------------------------------------------===//
// Helper functions for de-bufferizing operatrions with nested regions
//===----------------------------------------------------------------------===//

struct UsedBuffers {
  llvm::SetVector<TypedValue<MemRefType>> read;
  llvm::SetVector<TypedValue<MemRefType>> write;
};

UsedBuffers getUsedBuffers(ArrayRef<Block *> blocks) {
  UsedBuffers buffers;

  // TODO(ezhulenev): Add support for all lmhlo and lmhlo_gpu operations.
  for (Block *block : blocks) {
    block->walk([&](bufferization::ToTensorOp op) {
      buffers.read.insert(stripReinterpretCast(op.getMemref()));
    });

    block->walk([&](memref::TensorStoreOp op) {
      buffers.write.insert(stripReinterpretCast(op.getMemref()));
    });
  }

  // Remove written buffers from read buffers.
  buffers.read.remove_if(
      [&](auto memref) { return buffers.write.contains(memref); });

  return buffers;
}

//===----------------------------------------------------------------------===//
// Converts lmhlo.view op to a scf.while + iree_input.tensor.load
//===----------------------------------------------------------------------===//

// Keep track of converted while operations to correctly lower terminators in
// the loop before and after regions (condition and body regions).
struct ConvertedWhileOp {
  TypedValue<MemRefType> predicate;
  UsedBuffers buffers;
};

using ConvertedWhileOps = llvm::DenseMap<scf::WhileOp, ConvertedWhileOp>;

// TODO(ezhulenev): Rewrite while loops with statically known trip count to
// scf.for loops (see `op.getTripCount()` attribute).
struct ConvertWhileOp : public OpConversionPattern<lmhlo::WhileOp> {
  ConvertWhileOp(TypeConverter &converter, MLIRContext *ctx,
                 std::shared_ptr<DeBufferization> state,
                 std::shared_ptr<ConvertedWhileOps> converted)
      : OpConversionPattern(converter, ctx),
        state(std::move(state)),
        converted(std::move(converted)) {}

  LogicalResult matchAndRewrite(
      lmhlo::WhileOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    ImplicitLocOpBuilder b(op.getLoc(), rewriter);

    // Collect all buffers accessed in the loop condition and loop body.
    auto bufs = getUsedBuffers({&op.getCond().front(), &op.getBody().front()});

    Block *block = op->getBlock();

    // Pass updated tensors as loop iteration argument.
    SmallVector<Value> iter_args =
        llvm::to_vector(llvm::map_range(bufs.write, [&](auto memref) -> Value {
          return state->remapped[block][memref];
        }));

    // Set up buffer to tensor remapping inside nested regions.
    auto remap_iteration_args = [&](Block *nested_block, ValueRange iter_args) {
      // Read-only buffers remapped to tensors defined in the parent block.
      for (auto r : bufs.read)
        state->remapped[nested_block][r] = state->remapped[block][r];

      // Written-to buffers remapped to iteration arguments.
      for (auto tuple : llvm::zip_equal(bufs.write, iter_args))
        state->remapped[nested_block][std::get<0>(tuple)] =
            cast<TypedValue<TensorType>>(std::get<1>(tuple));
    };

    // Create an `scf.while` loop in place of `lmhlo.while` loop.
    auto loop = b.create<scf::WhileOp>(
        TypeRange(iter_args), iter_args,
        [&](OpBuilder &before_builder, Location before_loc, ValueRange args) {
          Block *cond = before_builder.getBlock();
          rewriter.mergeBlocks(&op.getCond().front(), cond);
          remap_iteration_args(cond, args);
        },
        [&](OpBuilder &after_builder, Location after_loc, ValueRange args) {
          Block *body = after_builder.getBlock();
          rewriter.mergeBlocks(&op.getBody().front(), body);
          remap_iteration_args(body, args);
        });

    // Use loop results to remap buffers in the parent block.
    for (auto tuple : llvm::zip_equal(bufs.write, loop.getResults()))
      state->remapped[block][std::get<0>(tuple)] =
          cast<TypedValue<TensorType>>(std::get<1>(tuple));

    // Predicate buffer placed on the device.
    auto predicate = cast<TypedValue<MemRefType>>(op.getOperand(0));
    (*converted)[loop] = ConvertedWhileOp{predicate, std::move(bufs)};

    // Erase the original while loop.
    rewriter.eraseOp(op);

    return success();
  }

  std::shared_ptr<DeBufferization> state;
  std::shared_ptr<ConvertedWhileOps> converted;
};

//===----------------------------------------------------------------------===//
// Converts lmhlo.terminator in the scf.while regions
//===----------------------------------------------------------------------===//

struct ConvertTerminatorOp : public OpConversionPattern<lmhlo::TerminatorOp> {
  ConvertTerminatorOp(TypeConverter &converter, MLIRContext *ctx,
                      std::shared_ptr<DeBufferization> state,
                      std::shared_ptr<ConvertedWhileOps> converted)
      : OpConversionPattern(converter, ctx),
        state(std::move(state)),
        converted(std::move(converted)) {}

  LogicalResult matchAndRewrite(
      lmhlo::TerminatorOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto loop = dyn_cast<scf::WhileOp>(op->getParentOp());
    if (!loop) return failure();

    ImplicitLocOpBuilder b(op.getLoc(), rewriter);
    assert(converted->contains(loop) && "loop conversion state was not found");

    auto iter_args = llvm::to_vector(llvm::map_range(
        (*converted)[loop].buffers.write, [&](auto memref) -> Value {
          return state->remapped[op->getBlock()][memref];
        }));

    // Convert lmhlo.terminator in the before block to scf.condition operation
    if (auto *cond = op->getBlock(); cond == &loop.getBefore().front()) {
      Value offset = b.create<arith::ConstantIndexOp>(0);
      auto predicate = b.create<IREE::Input::TensorLoadOp>(
          state->remapped[cond][(*converted)[loop].predicate],
          /*source_dims=*/ValueRange(), /*indices=*/offset);

      rewriter.replaceOpWithNewOp<scf::ConditionOp>(op, predicate, iter_args);
      return success();
    }

    // Convert lmhlo.terminator in the after block to scf.yield operation
    if (auto *body = op->getBlock(); body == &loop.getAfter().front()) {
      rewriter.replaceOpWithNewOp<scf::YieldOp>(op, TypeRange(), iter_args);
      return success();
    }

    return failure();
  }

  std::shared_ptr<DeBufferization> state;
  std::shared_ptr<ConvertedWhileOps> converted;
};

}  // namespace

//===----------------------------------------------------------------------===//

void populateWhileOpConversionPatterns(mlir::RewritePatternSet &patterns,
                                       mlir::TypeConverter &converter,
                                       std::shared_ptr<DeBufferization> state) {
  auto *ctx = patterns.getContext();
  auto converted = std::make_shared<ConvertedWhileOps>();
  patterns.insert<ConvertWhileOp, ConvertTerminatorOp>(converter, ctx, state,
                                                       converted);
}

}  // namespace gpu
}  // namespace xla
