#pragma once

#include <Python.h>
#include <ATen/ATen.h>
#include <memory>
#include <vector>
#include <iostream>

#include "torch/csrc/autograd/function.h"
#include "torch/csrc/autograd/variable.h"
#include "torch/csrc/autograd/symbolic.h"
#include "torch/csrc/autograd/saved_variable.h"

#ifdef WITH_CUDNN
#include "torch/csrc/cudnn/Conv.h"
using torch::cudnn::Convolution;
#else
struct Convolution {};
#endif

#ifdef WITH_MKLDNN
#include "torch/csrc/mkldnn/Conv.h"
using torch::mkldnn::Context;
#else
struct Context {};
#endif

namespace torch { namespace autograd {

struct ConvParams {
  std::vector<int> stride;
  std::vector<int> padding;
  std::vector<int> dilation;
  bool transposed;
  std::vector<int> output_padding;
  int groups;
  bool benchmark;
  bool deterministic;
  bool cudnn_enabled;

  bool is_strided() const;
  bool is_dilated() const;
  bool is_padded() const;
  bool is_output_padding_neg() const;
  bool is_output_padding_big() const;
  bool is_padding_neg() const;
  void view1d_as_2d();
  bool use_cudnn(const at::Tensor& input) const;
  bool use_mkldnn(const at::Tensor& input) const;
  bool use_nnpack(const at::Tensor& input) const;
  bool is_depthwise(const at::Tensor& input, const at::Tensor& weight, int groups) const;
};

struct ConvForward : public ForwardFunction<>, public ConvParams, public HasSymbolic {
  explicit ConvForward(ConvParams params) : ConvParams(std::move(params)) {}

  virtual std::string name() override;
  virtual variable_list apply(const variable_list& inputs) override;
  virtual jit::node_list symbolic(SymbolicContext* ctx, jit::node_list inputs) override;

  std::vector<int64_t> output_size(at::Tensor& input, at::Tensor& weight) const;
};

struct ConvBackward : public Function, public ConvParams {
  ConvBackward(
      FunctionFlags flags,
      ConvParams params,
      Variable input,
      Variable weight,
      Variable bias,
      tensor_list columns,
      tensor_list ones,
      std::unique_ptr<Convolution> convolution,
      std::unique_ptr<Context> context)
    : Function(std::move(flags))
    , ConvParams(std::move(params))
    , convolution(std::move(convolution))
    , context(std::move(context)) {
      if (is_executable) {
        this->input_ = SavedVariable(input, this);
        this->weight_ = SavedVariable(weight, this);
        if (bias.defined()) {
          this->bias_ = SavedVariable(bias, this);
        }
        this->columns = std::move(columns);
        this->ones = std::move(ones);
      }
    }

  virtual variable_list apply(const variable_list& gradOutputs) override;

  virtual void releaseVariables() override;

  SavedVariable input_;
  SavedVariable weight_;
  SavedVariable bias_;
  tensor_list columns;
  tensor_list ones;
  std::unique_ptr<Convolution> convolution;
  std::unique_ptr<Context> context;
};

struct ConvBackwardBackward : public Function, public ConvParams {
  ConvBackwardBackward(
      FunctionFlags flags,
      ConvParams params,
      Variable input,
      Variable weight,
      Variable bias,
      Variable grad_output)
    : Function(std::move(flags))
    , ConvParams(std::move(params)) {
      if (is_executable) {
        this->input_ = SavedVariable(input, this);
        this->weight_ = SavedVariable(weight, this);
        if (bias.defined()) {
          this->bias_ = SavedVariable(bias, this);
        }
        this->grad_output_ = SavedVariable(grad_output, this);
      }
    }

  virtual variable_list apply(const variable_list& grad_grad_inputs) override;

  virtual void releaseVariables() override;

  SavedVariable input_;
  SavedVariable weight_;
  SavedVariable bias_;
  SavedVariable grad_output_;
};

}} // namespace torch::autograd
