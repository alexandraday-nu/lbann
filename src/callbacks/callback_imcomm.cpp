////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// lbann_callback_imcomm .hpp .cpp - Send gradient updates between models
////////////////////////////////////////////////////////////////////////////////

#include <typeinfo>
#include <typeindex>
#include "lbann/callbacks/callback_imcomm.hpp"
#include "lbann/utils/timer.hpp"
#include "lbann/utils/exception.hpp"
#include "lbann/layers/learning/convolution.hpp"

namespace lbann {

lbann_callback_imcomm::lbann_callback_imcomm(lbann_callback_imcomm::comm_type ct,
    lbann_summary *summarizer) :
  lbann_callback(1, summarizer), m_default_ct(ct) {}

lbann_callback_imcomm::lbann_callback_imcomm(lbann_callback_imcomm::comm_type ct,
    std::unordered_set<Layer *> layers,
    lbann_summary *summarizer) :
  lbann_callback_imcomm(NONE, summarizer) {
  for (const auto& layer : layers) {
    m_layer_params[layer] = {};
    m_layer_params[layer].ct = ct;
  }
}

void lbann_callback_imcomm::set_layer_comm(Layer *layer, comm_type ct) {
  m_layer_params[layer] = {};
  m_layer_params[layer].ct = ct;
}

void lbann_callback_imcomm::set_layer_adaptive(Layer *layer, int proportion) {
  m_layer_params[layer] = {};
  m_layer_params[layer].ct = ADAPTIVE_QUANTIZATION;
  m_layer_params[layer].proportion = proportion;
}

void lbann_callback_imcomm::set_layer_threshold(
  Layer *layer, DataType pos_thresh, DataType neg_thresh) {
  m_layer_params[layer] = {};
  m_layer_params[layer].ct = THRESH_QUANTIZATION;
  m_layer_params[layer].pos_thresh = pos_thresh;
  m_layer_params[layer].neg_thresh = neg_thresh;
}

void lbann_callback_imcomm::setup(model *m) {
  for (Layer *layer : m->get_layers()) {

    // Add layer if not already in list
    learning *learning_layer = dynamic_cast<learning*>(layer);
    if (m_layer_params.find(layer) == m_layer_params.end()) {
      m_layer_params[layer] = {};
      m_layer_params[layer].ct = learning_layer != nullptr ? m_default_ct : NONE;
    }
    
    // Setup imcomm parameters if needed
    imcomm_params& params = m_layer_params[layer];
    if (params.ct != NONE) {
      if (learning_layer == nullptr) {
        throw(std::string{} + __FILE__ + " " + std::to_string(__LINE__) + " :: "
          + "imcomm: trying to do inter-model gradient communication on layer " 
          + layer->get_name() + " without gradients");
      }
      if (ct_needs_reshape(params.ct)) {
        // Currently, no layers need reshaping.
        //const std::type_info& layer_type = typeid(*layer));
      }
      if (ct_does_quantization(params.ct)) {
        const AbsDistMat& gradients = learning_layer->get_weights_gradient();
        if (params.reshape_height > 0) {
          El::Zeros(params.error, params.reshape_height, params.reshape_width);
        } else {
          El::Zeros(params.error, gradients.LocalHeight(), gradients.LocalWidth());
        }
      }
    }

  }
}

void lbann_callback_imcomm::on_epoch_end(model *m) {
  lbann_comm *comm = m->get_comm();
  if (comm->get_num_models() == 1 ||
      m->get_execution_mode() != execution_mode::training) {
    return;  // No point with only one model.
  }
  for (Layer *layer : m->get_layers()) {
    imcomm_params& params = m_layer_params[layer];
    if (ct_does_quantization(params.ct)) {
      comm->intermodel_sum_matrix(params.error);
      Mat *local_gradients = nullptr;
      Mat reshaped;
      learning *learning_layer = dynamic_cast<learning*>(layer);
      if (params.reshape_height > 0) {
        reshape_mat(learning_layer->get_weights_gradient().Matrix(),
                    reshaped, params.reshape_height, params.reshape_width);
        local_gradients = &reshaped;
      } else {
        local_gradients = &(learning_layer->get_weights_gradient().Matrix());
      }
      *local_gradients = params.error;
      // Apply optimizer update with accumulated gradient error.
      layer->update();
      El::Zero(params.error);
    }
  }
}

void lbann_callback_imcomm::on_backward_prop_end(model *m) {
  lbann_comm *comm = m->get_comm();
  if (comm->get_num_models() == 1 ||
      m->get_execution_mode() != execution_mode::training) {
    return;  // No point with only one model.
  }
  for (Layer *layer : m->get_layers()) {
    double start_time = get_time();
    imcomm_params& params = m_layer_params[layer];
    if (params.ct == NONE) {
      continue;
    }
    Mat* local_gradients = nullptr;
    Mat reshaped;
    learning *learning_layer = dynamic_cast<learning*>(layer);
    if (params.reshape_height > 0) {
      reshape_mat(learning_layer->get_weights_gradient().Matrix(),
                  reshaped, params.reshape_height, params.reshape_width);
      local_gradients = &reshaped;
    } else {
      local_gradients = &(learning_layer->get_weights_gradient().Matrix());
    }
    switch (params.ct) {
    case NORMAL:
      comm->intermodel_sum_matrix(*local_gradients);
      break;
    case ONEBIT_QUANTIZATION:
      m_quantizer.intermodel_sum_onebit_quantized(
        comm, *local_gradients, params.error);
      break;
    case THRESH_QUANTIZATION:
      m_quantizer.intermodel_sum_threshold_quantized(
        comm, *local_gradients, params.error, params.pos_thresh, params.neg_thresh);
      break;
    case ADAPTIVE_QUANTIZATION:
      m_quantizer.intermodel_sum_adaptive_quantized(
        comm, *local_gradients, params.error, params.proportion);
      break;
    default:
      throw(std::string{} + __FILE__ + " " + std::to_string(__LINE__) + " :: "
         + "imcomm: unknown comm type");
    }
    double im_time = get_time() - start_time;
    do_summary(m, learning_layer, im_time);
  }
}

void lbann_callback_imcomm::do_summary(model *m, learning *layer,
                                       double im_time) {
  if (m_summarizer == nullptr) {
    return;
  }
  lbann_comm *comm = m->get_comm();
  std::string prefix = layer->get_name() + "/imcomm_";
  m_summarizer->reduce_scalar(prefix + "time",
                              im_time, m->get_cur_step());
  size_t bytes_sent = 0;
  size_t bytes_received = 0;
  if (ct_does_quantization(m_layer_params[layer].ct)) {
    bytes_sent = comm->get_ar_bytes_sent();
    bytes_received = comm->get_ar_bytes_received();
  } else {
    // Use the same approximation the comm layer does.
    const Mat& local_gradients =
      layer->get_weights_gradient().LockedMatrix();
    bytes_sent =
      sizeof(DataType) * local_gradients.Height() * local_gradients.Width();
    bytes_received =
      sizeof(DataType) * local_gradients.Height() * local_gradients.Width();
  }
  m_summarizer->reduce_scalar(prefix + "bytes_sent",
                              bytes_sent, m->get_cur_step());
  m_summarizer->reduce_scalar(prefix + "bytes_received",
                              bytes_received, m->get_cur_step());
  if (ct_does_quantization(m_layer_params[layer].ct)) {
    m_summarizer->reduce_scalar(prefix + "rs_bytes_sent",
                                comm->get_ar_rs_bytes_sent(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "ag_bytes_sent",
                                comm->get_ar_ag_bytes_sent(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "rs_bytes_received",
                                comm->get_ar_rs_bytes_received(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "ag_bytes_received",
                                comm->get_ar_ag_bytes_received(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "ar_send_trans_time",
                                comm->get_ar_send_transform_time(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "ar_recv_trans_time",
                                comm->get_ar_recv_transform_time(),
                                m->get_cur_step());
    m_summarizer->reduce_scalar(prefix + "ar_recv_apply_trans_time",
                                comm->get_ar_recv_apply_transform_time(),
                                m->get_cur_step());
    if (m_layer_params[layer].ct == ADAPTIVE_QUANTIZATION) {
      m_summarizer->reduce_scalar(prefix + "quantized_count",
                                  m_quantizer.get_quantized_count(),
                                  m->get_cur_step());
    }
    m_quantizer.reset_counters();
    comm->reset_stats_counters();
  }
}

static std::vector<std::string> comm_type_names  =
    { "none", "normal", "onebit_quantization", "thresh_quantization", "adaptive_quantization" };

/** returns a string representation of the weight_initialization */
std::string get_comm_type_name(lbann_callback_imcomm::comm_type m) {
  if ((int)m < 0 or (int)m >= (int)comm_type_names.size()) {
    throw(std::string{} + __FILE__ + " " + std::to_string(__LINE__) + " :: "
           + " Invalid comm_type");
  }
  return comm_type_names[(int)m];
}

}  // namespace lbann
