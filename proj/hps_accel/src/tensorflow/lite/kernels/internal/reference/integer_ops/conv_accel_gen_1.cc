/*
 * Copyright 2021 The CFU-Playground Authors
 * Copyright 2019 The TensorFlow Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tensorflow/lite/kernels/internal/reference/integer_ops/conv_accel_gen_1.h"

#if GATEWARE_GEN == 1

#include "blocks.h"
#include "cfu.h"
#include "gateware_constants.h"

using hps_accel::multiply_accumulate;
using hps_accel::Vector16;

namespace tflite {
namespace reference_integer_ops {

bool CanAccelerateConv4x4(const ConvParams& params,
                          const RuntimeShape& input_shape,
                          const RuntimeShape& filter_shape,
                          const RuntimeShape& output_shape,
                          const int32_t* bias_data) {
  const int input_depth = MatchingDim(input_shape, 3, filter_shape, 3);
  const int filter_height = filter_shape.Dims(1);
  const int filter_width = filter_shape.Dims(2);
  const int dilation_width_factor = params.dilation_width_factor;
  const int dilation_height_factor = params.dilation_height_factor;
  const int batches = MatchingDim(input_shape, 0, output_shape, 0);
 
  return params.padding_type == PaddingType::kValid &&
         (input_depth == 1 || input_depth % 4 == 0) && filter_width == 4 &&
         filter_height == 4 && dilation_width_factor == 1 &&
         dilation_height_factor == 1 && batches == 1 && bias_data != NULL;
}

void ConvPerChannel4x4(const ConvParams& params,
                       const int32_t* output_multiplier,
                       const int32_t* output_shift,
                       const RuntimeShape& input_shape,
                       const int8_t* input_data,
                       const RuntimeShape& filter_shape,
                       const int8_t* filter_data,
                       const RuntimeShape& bias_shape, const int32_t* bias_data,
                       const RuntimeShape& output_shape, int8_t* output_data) {
  // Get parameters.
  const int32_t input_offset = params.input_offset;  // r = s(q - Z)
  const int stride_width = params.stride_width;
  const int stride_height = params.stride_height;
  TFLITE_DCHECK_EQ(params.dilation_width_factor, 1);
  TFLITE_DCHECK_EQ(params.dilation_height_factor, 1);
  TFLITE_DCHECK_EQ(params.padding_type, PaddingType::kValid);
  TFLITE_DCHECK_EQ(params.padding_values.width, 0);
  TFLITE_DCHECK_EQ(params.padding_values.height, 0);
  const int32_t output_offset = params.output_offset;

  // Set min and max value of the output.
  const int32_t output_activation_min = params.quantized_activation_min;
  const int32_t output_activation_max = params.quantized_activation_max;

  // Consistency check.
  TFLITE_DCHECK_LE(output_activation_min, output_activation_max);
  TFLITE_DCHECK_EQ(input_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(filter_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(output_shape.DimensionsCount(), 4);
  const int batches = MatchingDim(input_shape, 0, output_shape, 0);
  TFLITE_DCHECK_EQ(batches, 1);
  const int input_depth = MatchingDim(input_shape, 3, filter_shape, 3);
  TFLITE_DCHECK(input_depth == 1 || input_depth % 4 == 0);
  const int output_depth = MatchingDim(filter_shape, 0, output_shape, 3);
  TFLITE_DCHECK(bias_data);
  TFLITE_DCHECK_EQ(bias_shape.FlatSize(), output_depth);

  // Check dimensions of the tensors.
  const int input_height = input_shape.Dims(1);
  const int input_width = input_shape.Dims(2);
  TFLITE_DCHECK_EQ(filter_shape.Dims(1), 4);
  const int filter_height = 4;
  TFLITE_DCHECK_EQ(filter_shape.Dims(2), 4);
  const int filter_width = 4;

  const int output_height = output_shape.Dims(1);
  const int output_width = output_shape.Dims(2);

  hps_accel::LoadInputOffset(input_offset);
  hps_accel::SetOutputOffsets(output_offset, output_activation_min,
                              output_activation_max);

  // Work out maximum output channels we can do per filter load
  // First calculate how many filter words one output channel takes
  // Then determine how many will fit in filter memory
  // Finally, round down to a multiple of 4
  const int filter_words_per_output_channel =
      input_depth * filter_height * filter_width / 4;
  const int max_output_channels_per_load =
      (MAX_FILTER_WORDS / filter_words_per_output_channel) / 4 * 4;

  for (int out_channel_offset = 0; out_channel_offset < output_depth;
       out_channel_offset += max_output_channels_per_load) {
    const int output_channels = std::min(output_depth - out_channel_offset,
                                         max_output_channels_per_load);
    hps_accel::LoadFilter(
        input_depth, output_channels,
        filter_data + Offset(filter_shape, out_channel_offset, 0, 0, 0));
    hps_accel::LoadOutputParams(out_channel_offset, output_channels, bias_data,
                                output_multiplier, output_shift);

    uint32_t* output_data32_base = static_cast<uint32_t*>(static_cast<void*>(
        output_data + Offset(output_shape, 0, 0, 0, out_channel_offset)));

    for (int out_y = 0; out_y < output_height; ++out_y) {
      const int in_y_origin = out_y * stride_height;
      // Check bounds for input buffer. This assumes "valid" padding type.
      TFLITE_DCHECK_LE(in_y_origin + filter_height, input_height);
      for (int out_x = 0; out_x < output_width; ++out_x) {
        const int in_x_origin = out_x * stride_width;
        // Check bounds for input buffer. This assumes "valid" padding type.
        TFLITE_DCHECK_LE(in_x_origin + filter_width, input_width);
        const int8_t* current_input_data =
            input_data + Offset(input_shape, 0, in_y_origin, in_x_origin, 0);

        TFLITE_DCHECK_LE(input_depth * filter_height * filter_width / 4,
                         MAX_INPUT_WORDS);
        hps_accel::LoadInput(input_width, input_depth, current_input_data);

        // Calculate all outputs for a single output pixel
        for (int out_channel = out_channel_offset;
             out_channel < out_channel_offset + output_channels;
             ++out_channel) {
          int iterations = filter_height * filter_width * input_depth / 16;
          hps_accel::AdvanceFilterInput(iterations);
          int32_t acc = multiply_accumulate();
          hps_accel::PostProcess(acc);
        }

        // Pull result data from output FIFO and place into memory, a word at a
        // time.
        uint32_t* output_data32 = output_data32_base;
        for (int i = 0; i < output_channels; i += 4) {
          *(output_data32++) = hps_accel::GetOutputWord();
        }
        // Point to start of next pixel
        output_data32_base += (output_depth / 4);
      }
    }
  }
}

}  // namespace reference_integer_ops
}  // namespace tflite
#endif  // GATEWARE_GEN
