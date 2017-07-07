#include <cfloat>
#include <vector>
#include <string>
#include "gtest/gtest.h"

#include "fpga_caffe/test/test_fpga_caffe_main.hpp"

using std::min;
using std::max;

void ref_pool_layer_hwcn(std::vector<float> input, std::vector<float>& output,
    std::vector<short>& sw_relu_vals, kernel_params params) {

  for (int i = 0; i < output.size(); ++i)
    output[i] = -FLT_MAX;

  int pooled_height_ = static_cast<int>(ceil(static_cast<float>(
       params.ydim - params.pksize) / 2)) + 1;
  int pooled_width_ = pooled_height_;
  int kernel_h_ = params.pksize;
  int kernel_w_ = params.pksize;
  int height_ = params.ydim;
  int width_ = params.xdim;
  int channels_ = params.inchannels;
  int num_ = params.numimages;
  int stride_h_ = 2;
  int stride_w_ = 2;
 
  for (int ph = 0; ph < pooled_height_; ++ph) {
    for (int pw = 0; pw < pooled_width_; ++pw) {
      int hstart = ph * stride_h_;
      int wstart = pw * stride_w_;
      int hend = min(hstart + kernel_h_, height_);
      int wend = min(wstart + kernel_w_, width_);
      hstart = max(hstart, 0);
      wstart = max(wstart, 0);
      for (int h = hstart; h < hend; ++h) {
        for (int w = wstart; w < wend; ++w) {
          for (int c = 0; c < channels_; ++c) {
            for (int n = 0; n < num_; ++n) {
              int top_offset = ((ph * pooled_width_ + pw) * channels_ + c)
                * num_ + n;
              int index = h * width_ + w;
              int bot_offset = (index * channels_ + c) * num_ + n;
              if (input[bot_offset] > output[top_offset]) {
                output[top_offset] = input[bot_offset];
                sw_relu_vals[top_offset] = (h - hstart) * 3 + (w - wstart);
              }
            }
          }
        }
      }
    }
  }
}

void ref_backward_pool_layer_hwcn(std::vector<float> input,
    std::vector<float>& output, std::vector<short> relu_vals,
    kernel_params params) {

  for (int i = 0; i < output.size(); ++i)
    output[i] = 0;

  int pooled_height_ = static_cast<int>(ceil(static_cast<float>(
       params.ydim - params.pksize) / 2)) + 1;
  int pooled_width_ = pooled_height_;
  int width_ = params.xdim;
  int channels_ = params.inchannels;
  int num_ = params.numimages;
  int stride_h_ = 2;
  int stride_w_ = 2;
 
  for (int ph = 0; ph < pooled_height_; ++ph) {
    for (int pw = 0; pw < pooled_width_; ++pw) {
      for (int c = 0; c < channels_; ++c) {
        for (int n = 0; n < num_; ++n) {
          int in_idx = ((ph * pooled_width_ + pw) * channels_ + c) * num_ + n;
          int hw = relu_vals[in_idx];
          int hstart = ph * stride_h_;
          int wstart = pw * stride_w_;
          int w = hw % 3;
          int h = hw / 3;
          int bottom_index = (((hstart + h) * width_ + (wstart + w))
              * channels_ + c) * num_ + n;
          output[bottom_index] += input[in_idx];
        }
      }
    }
  }
}

void ref_conv_layer_hwcn(std::vector<float> input, std::vector<float> weights,
    std::vector<float> bias, std::vector<float>& output,
    kernel_params params) {
  int o_head, k_head;
  int out_idx, in_idx, k_idx;

  int numgroups = params.numgroups;
  int inchannels = params.inchannels * numgroups;
  int outchannels = params.outchannels * numgroups;
  int ydim = params.ydim;
  int xdim = params.xdim;
  int numimages = params.numimages;
  int ksize = params.ksize;

  int pad = params.pad;
  int stride = params.stride;

  // Convolution
  for (int n = 0; n < numimages; n++) {
    for (int g = 0; g < numgroups; g++) {
      o_head = (outchannels / numgroups) * g;
      k_head = (inchannels / numgroups) * g;
      int o_g = outchannels / numgroups;
      int k_g = inchannels / numgroups;
      for (int o = 0; o < o_g; o++) {
        for (int k = 0; k < k_g / 4; k++) {
          for (int m = 0; m < 4; ++m) {
            for (int y = 0; y < ydim; y++) {
              for (int x = 0; x < xdim; x++) {
                for (int p = 0; p < ksize; p++) {
                  for (int q = 0; q < ksize; q++) {
                    int in_y = y * stride - pad + p;
                    int in_x = x * stride - pad + q;
                    if (in_y >= 0 && in_y < ydim && in_x >= 0 && in_x < xdim) {
                      out_idx = ((y * xdim + x) * outchannels + o + o_head)
                        * numimages + n;
                      in_idx = ((in_y * xdim + in_x) * inchannels + m *
                        (k_g / 4) + k + k_head) * numimages + n;
                      k_idx = (((o + o_head) * ksize + p) * ksize + q) * k_g +
                        k * 4 + m;
                      output[out_idx] += input[in_idx] * weights[k_idx];
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  for (int n = 0; n < numimages; n++) {
    for (int o = 0; o < outchannels; ++o) {
      for (int y = 0; y < ydim; ++y) {
        for (int x = 0; x < xdim; ++x) {
          out_idx = ((y * xdim + x) * outchannels + o) * numimages + n;
          output[out_idx] += bias[o];
        }
      }
    }
  }
}

void ref_backward_conv_layer_hwcn(std::vector<float> input,
    std::vector<float> weights, std::vector<float>& output,
    kernel_params params) {
  int o_head, k_head;
  int out_idx, in_idx, k_idx;

  int numgroups = params.numgroups;
  int inchannels = params.inchannels * numgroups;
  int outchannels = params.outchannels * numgroups;
  int ydim = params.ydim;
  int xdim = params.xdim;
  int numimages = params.numimages;
  int ksize = params.ksize;

  int pad = params.pad;
  int stride = params.stride;

  for (int n = 0; n < numimages; n++) {
    for (int g = 0; g < numgroups; g++) {
      o_head = (outchannels / numgroups) * g;
      k_head = (inchannels / numgroups) * g;
      int o_g = outchannels / numgroups;
      int k_g = inchannels / numgroups;
      for (int o = 0; o < o_g; o++) {
        for (int k = 0; k < k_g / 4; k++) {
          for (int m = 0; m < 4; ++m) {
            for (int p = 0; p < ydim; p++) {
              for (int q = 0; q < xdim; q++) {
                for (int y = 0; y < ksize; y++) {
                  for (int x = 0; x < ksize; x++) {
                    int in_y = y * stride - pad + p;
                    int in_x = x * stride - pad + q;
                    if (in_y >= 0 && in_y < ydim
                      && in_x >= 0 && in_x < xdim) {
                      out_idx = ((p * xdim + q) * outchannels + o + o_head)
                        * numimages + n;
                      in_idx = ((in_y * xdim + in_x) * inchannels + m *
                        (k_g / 4) + k + k_head) * numimages + n;
                      k_idx = (((o + o_head) * ksize + y) * ksize + x) *
                        k_g + k * 4 + m;
                      output[k_idx] += input[in_idx] * weights[out_idx];
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
template <typename TypeParam>
class CRLayerHWCNHalfTest : public OCLDeviceTest<TypeParam> {
  typedef typename TypeParam::Dtype Dtype;

 protected:
  CRLayerHWCNHalfTest()
    : ocl("cr_layer_hwcn_half.xclbin", "cr_layer_hwcn_half") 
  {}
  virtual void SetUp() {
    params.resize(1);
    params[0].numgroups = 1;
    params[0].inchannels = 32;
    params[0].outchannels = 1;
    params[0].burstchannels = 16;
    params[0].rpo = 2;
    params[0].rpofm = 0;
    params[0].burstydim = 0;
    params[0].ydim = 24;
    params[0].xdim = 24;
    params[0].xtile_pad = 0;
    params[0].numimages = 256;
    params[0].ksize = 3;
    params[0].backward = 0;
    params[0].relu = 0;
    params[0].stride = 1;
    params[0].pad = 1;
    params[0].pool = 0;
    params[0].pksize = 2;
  }

  virtual ~CRLayerHWCNHalfTest() {}
  
  OCLUtil ocl;
  std::vector<Dtype> input;
  std::vector<Dtype> input_pad;
  std::vector<chalf> input_pad_half;
  std::vector<Dtype> weights;
  std::vector<Dtype> weights_pad;
  std::vector<chalf> weights_pad_half;
  std::vector<Dtype> bias;
  std::vector<chalf> bias_half;
  std::vector<Dtype> hw_results;
  std::vector<chalf> hw_results_half;
  std::vector<Dtype> sw_results;
  std::vector<kernel_params> params;
  std::vector<short> relu_vals;
  std::vector<short> sw_relu_vals;
  cl_mem ocl_input;
  cl_mem ocl_weights;
  cl_mem ocl_output;
  cl_mem ocl_bias;
  cl_mem ocl_relu_vals;
  cl_mem ocl_params;
};

TYPED_TEST_CASE(CRLayerHWCNHalfTest, TestOCLDtypesAndDevices);

TYPED_TEST(CRLayerHWCNHalfTest, TestCR1x1F_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    int outsize = params[i].numimages * params[i].outchannels * params[i].ydim
      * params[i].xdim * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = (outsize > insize) ? outsize : insize;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(relusize / 16, 0);
    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, 0.0, 1.0);
    fillVectorHalf(this->weights, -1.0, 1.0);
    fillVectorHalf(this->bias, -1.0, 1.0);
   
    toHalf(this->input, this->input_pad_half);
    toHalf(this->weights, this->weights_pad_half);
    toHalf(this->bias, this->bias_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize / 16, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize / 16, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);
    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize / 16, this->relu_vals.data(), 0,
        NULL, NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_conv_layer_hwcn(this->input, this->weights, this->bias,
        this->sw_results, params[i]);
    //ref_relu_layer(this->sw_results);
    int size = params[i].numimages * params[i].outchannels *
      params[i].numgroups * params[i].ydim * params[i].xdim;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<" relu: "<<
        ((this->relu_vals[j / 16] >> (j % 16)) & 0x1)<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRLayerHWCNHalfTest, TestCR1x1B_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    // Set sizes
    int ksize = params[i].ksize;
    params[i].backward = 1;
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].numimages * params[i].outchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int outsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(insize / 16, -1);
    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, -1.0, 1.0);
    fillVectorHalf(this->weights, 0.0, 1.0);
    fillVectorHalf(this->bias, -1.0, 1.0);
  
    toHalf(this->input, this->input_pad_half);
    toHalf(this->weights, this->weights_pad_half);
    toHalf(this->bias, this->bias_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * insize / 16, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * insize / 16, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_backward_conv_layer_hwcn(this->input, this->weights,
        this->sw_results, params[i]);
    int size = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j], this->hw_results[j], 1e-1,
            1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRLayerHWCNHalfTest, TestCR2x2F_Pool_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    params[i].pool = 1;
    params[i].pksize = 2;
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;

    int pooled_height = ceil((params[i].ydim - params[i].pksize) / 2.0) + 1;
    int pooled_width = pooled_height;

    int outsize = params[i].numimages * params[i].inchannels * pooled_height
      * pooled_width * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = outsize;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    this->sw_relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, -0.0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(relusize, 0);
    this->sw_relu_vals.resize(relusize, 0);
    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, -1e-7, 1e-7);
    //fillVectorHalf(this->weights, -1.0, 1.0);
    //fillVectorHalf(this->bias, -1.0, 1.0);
   
    toHalf(this->input, this->input_pad_half);
    toHalf(this->weights, this->weights_pad_half);
    toHalf(this->bias, this->bias_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);
    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_pool_layer_hwcn(this->input, this->sw_results, this->sw_relu_vals,
        params[i]);
    int size = params[i].numimages * params[i].inchannels *
      params[i].numgroups * pooled_height * pooled_width;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<" "<<
        this->relu_vals[j]<<" "<<this->sw_relu_vals[j]<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
      EXPECT_EQ(this->relu_vals[j], this->sw_relu_vals[j]);
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRLayerHWCNHalfTest, TestCR2x2B_Pool_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    params[i].pool = 1;
    params[i].pksize = 2;
    params[i].backward = 1;

    int pooled_height = ceil((params[i].ydim - params[i].pksize) / 2.0) + 1;
    int pooled_width = pooled_height;

    int insize = params[i].numimages * params[i].inchannels * pooled_height *
      pooled_width * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    
    int outsize = params[i].numimages * params[i].inchannels * params[i].ydim
      * params[i].xdim * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = insize;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    this->sw_relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(relusize, 0);
    this->sw_relu_vals.resize(relusize, 0);
    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, 0.0, 1.0);
   
    toHalf(this->input, this->input_pad_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_backward_pool_layer_hwcn(this->input, this->sw_results,
        this->relu_vals, params[i]);
    int size = params[i].numimages * params[i].inchannels *
      params[i].numgroups * params[i].ydim * params[i].xdim;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRLayerHWCNHalfTest, TestCR3x3F_Pool_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    params[i].pool = 1;
    params[i].pksize = 3;
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;

    int pooled_height = ceil((params[i].ydim - params[i].pksize) / 2.0) + 1;
    int pooled_width = pooled_height;

    int outsize = params[i].numimages * params[i].inchannels * pooled_height
      * pooled_width * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = outsize;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    this->sw_relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(relusize, 0);
    this->sw_relu_vals.resize(relusize, 0);
    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, 0.0, 1.0);
   
    toHalf(this->input, this->input_pad_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);
    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_pool_layer_hwcn(this->input, this->sw_results, this->sw_relu_vals,
        params[i]);
    int size = params[i].numimages * params[i].inchannels *
      params[i].numgroups * pooled_height * pooled_width;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<" "<<
        this->relu_vals[j]<<" "<<this->sw_relu_vals[j]<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
      EXPECT_EQ(this->relu_vals[j], this->sw_relu_vals[j]);
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRLayerHWCNHalfTest, TestCR3x3B_Pool_HALF) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    params[i].pool = 1;
    params[i].pksize = 3;
    params[i].backward = 1;

    int pooled_height = ceil((params[i].ydim - params[i].pksize) / 2.0) + 1;
    int pooled_width = pooled_height;

    int insize = params[i].numimages * params[i].inchannels * pooled_height *
      pooled_width * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    
    int outsize = params[i].numimages * params[i].inchannels * params[i].ydim
      * params[i].xdim * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = insize;
    // Clear input vectors
    this->input.clear();
    this->input_pad_half.clear();
    this->weights.clear();
    this->weights_pad_half.clear();
    this->bias.clear();
    this->bias_half.clear();
    this->hw_results.clear();
    this->hw_results_half.clear();
    this->sw_results.clear();
    this->relu_vals.clear();
    this->sw_relu_vals.clear();
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_half.resize(insize, chalf(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_half.resize(wsize, chalf(0));
    this->bias.resize(bsize, 0);
    this->bias_half.resize(bsize, chalf(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_half.resize(outsize, chalf(0));
    this->relu_vals.resize(relusize, 0);
    this->sw_relu_vals.resize(relusize, 0);

    this->relu_vals[0] = 2;
    this->sw_relu_vals[0] = 2;

    events.resize(events_size);
    // Populate vectors
    fillVectorHalf(this->input, 0.0, 1.0);
   
    toHalf(this->input, this->input_pad_half);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(chalf) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(chalf) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(chalf) * insize, this->input_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(chalf) * wsize, this->weights_pad_half.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(chalf) * bsize, this->bias_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(chalf) * outsize, this->hw_results_half.data(), 0, NULL,
        NULL);

    toFloat(this->hw_results_half, this->hw_results);

    ref_backward_pool_layer_hwcn(this->input, this->sw_results,
        this->relu_vals, params[i]);
    int size = params[i].numimages * params[i].inchannels *
      params[i].numgroups * params[i].ydim * params[i].xdim;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<" "<<j<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}
