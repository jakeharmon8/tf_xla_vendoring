// RUN: xla-cpu-opt %s -split-input-file -xla-cpu-to-cpu-runtime | FileCheck %s

func.func @partition_id() -> i32 {
  %0 = "xla_cpu.partition_id"() : () -> i32
  func.return %0 : i32
}

// CHECK-LABEL: @partition_id
// CHECK: call @local_xla.cpu.partition_id() : () -> i32

// CHECK: func private @local_xla.cpu.partition_id() -> i32 attributes {rt.custom_call = "xla.cpu.partition_id"}

// -----

func.func @replica_id() -> i32 {
  %0 = "xla_cpu.replica_id"() : () -> i32
  func.return %0 : i32
}

// CHECK-LABEL: @replica_id
// CHECK: call @local_xla.cpu.replica_id() : () -> i32

// CHECK: func private @local_xla.cpu.replica_id() -> i32 attributes {rt.custom_call = "xla.cpu.replica_id"}

// -----

#map = affine_map<(d0)[s0] -> (d0 + s0)>
func.func @all_reduce(%arg0: memref<32xf32, #map>, %arg1: memref<32xf32>) {
  "xla_cpu.all_reduce"(%arg0, %arg1) {
    replica_groups = dense<[[0, 2, 4, 6], [1, 3, 5, 7]]> : tensor<2x4xi64>,
    channel_handle = 42 : i64,
    reduction_kind = 3 : i32,
    use_global_device_ids = 0 : i32
  } : (memref<32xf32, #map>, memref<32xf32>) -> ()
  func.return
}

// CHECK-LABEL: @all_reduce
//  CHECK-SAME:   %[[ARG0:.*]]: memref<32xf32,
//  CHECK-SAME:   %[[ARG1:.*]]: memref<32xf32>
//       CHECK: %[[ALLOC:.*]] = memref.alloc
//       CHECK: memref.copy %[[ARG0]], %[[ALLOC]]
//       CHECK: call @local_xla.cpu.all_reduce(%[[ALLOC]], %[[ARG1]])
//  CHECK-SAME:   channel_handle = 42
//  CHECK-SAME:   op_id = 0
//  CHECK-SAME:   reduction_kind = 3
//  CHECK-SAME:   replica_groups = dense<
//       CHECK: func.func private @local_xla.cpu.all_reduce(
//  CHECK-SAME:     memref<32xf32>, memref<32xf32>)
//  CHECK-SAME:     attributes {rt.custom_call = "xla.cpu.all_reduce"}


// -----

func.func @collective_permute(%arg0: memref<16x8xf32>, %arg1: memref<16x8xf32>) {
  "xla_cpu.collective_permute"(%arg0, %arg1) {
    source_target_pairs = dense<[[0, 1], [1, 2], [2, 3]]> : tensor<3x2xi64>,
    channel_handle = 42 : i64
  } : (memref<16x8xf32>, memref<16x8xf32>) -> ()
  func.return
}

// CHECK-LABEL: @collective_permute
//  CHECK-SAME:   %[[ARG0:.*]]: memref<16x8xf32>,
//  CHECK-SAME:   %[[ARG1:.*]]: memref<16x8xf32>
//       CHECK: call @local_xla.cpu.collective_permute(%[[ARG0]], %[[ARG1]])
//  CHECK-SAME:   channel_handle = 42
//  CHECK-SAME:   source_target_pairs = dense<
//       CHECK: func.func private @local_xla.cpu.collective_permute(
//  CHECK-SAME:     attributes {rt.custom_call = "xla.cpu.collective_permute"}

// -----

func.func @rng_bit_generator_default(%state: memref<3xui64>,
    %state_out: memref<3xui64>, %values_out: memref<10xui32>) {
  "xla_cpu.rng_bit_generator"(%state, %state_out, %values_out)
    {rng_algorithm = #mhlo.rng_algorithm<DEFAULT>
  } : (memref<3xui64>, memref<3xui64>, memref<10xui32>) -> ()
  return
}

// CHECK-LABEL: @rng_bit_generator_default
//  CHECK-SAME:   %[[ARG0:.*]]: memref<3xui64>, %[[ARG1:.*]]: memref<3xui64>,
//  CHECK-SAME:   %[[ARG2:.*]]: memref<10xui32>
//       CHECK: call @local_xla_cpu_rng_philox(%[[ARG0]], %[[ARG1]], %[[ARG2]])
//       CHECK: func.func private @local_xla_cpu_rng_philox(
//  CHECK-SAME:     attributes {rt.custom_call = "xla_cpu_rng_philox"}

// -----

func.func @rng_bit_generator_three_fry(%state: memref<2xui64>,
    %state_out: memref<2xui64>, %values_out: memref<10xui32>) {
  "xla_cpu.rng_bit_generator"(%state, %state_out, %values_out)
    {rng_algorithm = #mhlo.rng_algorithm<THREE_FRY>
  } : (memref<2xui64>, memref<2xui64>, memref<10xui32>) -> ()
  return
}

// CHECK-LABEL: @rng_bit_generator_three_fry
//       CHECK: call @local_xla_cpu_rng_three_fry(
//       CHECK: func.func private @local_xla_cpu_rng_three_fry(
//  CHECK-SAME:     attributes {rt.custom_call = "xla_cpu_rng_three_fry"}

// -----

func.func @conv_2d_nhwc_hwcf(%arg0: memref<1x4x5x1xf32>, %arg1: memref<3x2x1x1xf32>, %out: memref<1x2x4x1xf32>) {
  "xla_cpu.convolution"(%arg0, %arg1, %out) {batch_group_count = 1 : i64, feature_group_count = 1 : i64, inputBatchDimension = 0 : i64, inputFeatureDimension = 3 : i64, inputSpatialDimensions = [1, 2], kernelInputFeatureDimension = 2 : i64, kernelOutputFeatureDimension = 3 : i64, kernelSpatialDimensions = [0, 1], lhs_dilation = dense<1> : tensor<2xi64>, outputBatchDimension = 0 : i64, outputFeatureDimension = 3 : i64, outputSpatialDimensions = [1, 2], padding = dense<0> : tensor<2x2xi64>, rhs_dilation = dense<1> : tensor<2xi64>, window_strides = dense<1> : tensor<2xi64>} : (memref<1x4x5x1xf32>, memref<3x2x1x1xf32>, memref<1x2x4x1xf32>) -> ()
  return
}

// -----

func.func @conv_3d_ndhwc_dhwcf(%arg0: memref<1x8x8x8x1xf32>, %arg1: memref<2x2x2x1x1xf32>, %out: memref<1x7x7x7x1xf32>) {
  "xla_cpu.convolution"(%arg0, %arg1, %out) {batch_group_count = 1 : i64, feature_group_count = 1 : i64, inputBatchDimension = 0 : i64, inputFeatureDimension = 4 : i64, inputSpatialDimensions = [1, 2, 3], kernelInputFeatureDimension = 3 : i64, kernelOutputFeatureDimension = 4 : i64, kernelSpatialDimensions = [0, 1, 2], lhs_dilation = dense<1> : tensor<3xi64>, outputBatchDimension = 0 : i64, outputFeatureDimension = 4 : i64, outputSpatialDimensions = [1, 2, 3], padding = dense<0> : tensor<3x2xi64>, rhs_dilation = dense<1> : tensor<3xi64>, window_strides = dense<1> : tensor<3xi64>} : (memref<1x8x8x8x1xf32>, memref<2x2x2x1x1xf32>, memref<1x7x7x7x1xf32>) -> ()
  return
}

// -----

func.func @depthwise_conv1d(%arg0: memref<1x10x8xf32>, %arg1: memref<3x1x16xf32>, %out: memref<1x10x16xf32>) {
  "xla_cpu.convolution"(%arg0, %arg1, %out) {batch_group_count = 1 : i64, feature_group_count = 8 : i64, inputBatchDimension = 0 : i64, inputFeatureDimension = 2 : i64, inputSpatialDimensions = [1], kernelInputFeatureDimension = 1 : i64, kernelOutputFeatureDimension = 2 : i64, kernelSpatialDimensions = [0], lhs_dilation = dense<1> : tensor<1xi64>, outputBatchDimension = 0 : i64, outputFeatureDimension = 2 : i64, outputSpatialDimensions = [1], padding = dense<1> : tensor<1x2xi64>, rhs_dilation = dense<1> : tensor<1xi64>, window_reversal = dense<false> : tensor<1xi1>, window_strides = dense<1> : tensor<1xi64>} : (memref<1x10x8xf32>, memref<3x1x16xf32>, memref<1x10x16xf32>) -> ()
  return
}

// -----

func.func @foo(%arg0: memref<3x9x9x8xf32>, %arg1: memref<1x7x8x8xf32>, %out: memref<3x9x9x8xf32>) {
  "xla_cpu.convolution"(%arg0, %arg1, %out) {batch_group_count = 1 : i64, feature_group_count = 1 : i64, inputBatchDimension = 0 : i64, inputFeatureDimension = 3 : i64, inputSpatialDimensions = [1, 2], kernelInputFeatureDimension = 2 : i64, kernelOutputFeatureDimension = 3 : i64, kernelSpatialDimensions = [0, 1], lhs_dilation = dense<1> : tensor<2xi64>, outputBatchDimension = 0 : i64, outputFeatureDimension = 3 : i64, outputSpatialDimensions = [1, 2], padding = dense<[[0, 0], [3, 3]]> : tensor<2x2xi64>, precision_config = [#mhlo<precision DEFAULT>, #mhlo<precision DEFAULT>], rhs_dilation = dense<1> : tensor<2xi64>, window_reversal = dense<false> : tensor<2xi1>, window_strides = dense<1> : tensor<2xi64>} : (memref<3x9x9x8xf32>, memref<1x7x8x8xf32>, memref<3x9x9x8xf32>) -> ()
  return
}

