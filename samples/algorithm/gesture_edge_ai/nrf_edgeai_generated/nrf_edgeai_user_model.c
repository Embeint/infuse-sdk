/* 2026-09-23T01:35:23.451705 */
/*
* Copyright (c) 2026 Nordic Semiconductor ASA
* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
*/
#include "nrf_edgeai_user_model.h"
#include "nrf_edgeai_user_types.h"
#include <nrf_edgeai/nrf_edgeai_platform.h>
#include <nrf_edgeai/rt/private/nrf_edgeai_interfaces.h>
#include <assert.h>

//////////////////////////////////////////////////////////////////////////////
/* Nordic EdgeAI Lab Solution ID and Runtime Version */
#define EDGEAI_LAB_SOLUTION_ID_STR      "96375"
#define EDGEAI_RUNTIME_VERSION_COMBINED 0x00020202

//////////////////////////////////////////////////////////////////////////////
#define INPUT_TYPE                         f32

/** User input features type */
#define INPUT_FEATURE_DATA_TYPE            NRF_EDGEAI_INPUT_F32

/** Number of unique features in the original input sample */
#define INPUT_UNIQ_FEATURES_NUM            4

/** Number of unique features actually used by NN from the original input sample */
#define INPUT_UNIQ_FEATURES_USED_NUM       4

/** Number of input feature samples that should be collected in the input window
 *  feature_sample = 1 * INPUT_UNIQ_FEATURES_NUM
 */
#define INPUT_WINDOW_SIZE                  25

/** Number of input feature samples on that the input window is shifted */
#define INPUT_WINDOW_SHIFT                 5

/** Number of subwindows in input feature window,
* the SUBWINDOW_SIZE = INPUT_WINDOW_SIZE / INPUT_SUBWINDOW_NUM
* if the window size is not divisible by the number of subwindows without a remainder,
* the remainder is added to the last subwindow size */
#define INPUT_SUBWINDOW_NUM                 0

#define INPUT_UNIQUE_SCALES_NUM (sizeof(INPUT_FEATURES_SCALE_MIN) / sizeof(INPUT_FEATURES_SCALE_MIN[0])) 

/** Defines input(also used for LAG) features MIN scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MIN[] = {
 -32768.0000000, -32614.0000000, -32768.0000000, 522.0000000 };

/** Defines input(also used for LAG) features MAX scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MAX[] = {
 32767.0000000, 32767.0000000, 32767.0000000, 54307.0000000 };

/** Defines which unique features from the input data will be used/collected,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USAGE_MASK NULL

/** Defines which unique input features is used for LAG features processing,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USED_FOR_LAGS_MASK NULL

//////////////////////////////////////////////////////////////////////////////
#define MODEL_TYPE                 __NRF_EDGEAI_MODEL_NEUTON
#define MODEL_TASK                 0
#define MODEL_OUTPUTS_NUM          7

#define MODEL_USES_AS_INPUT_INPUT_FEATURES 0
#define MODEL_USES_AS_INPUT_DSP_FEATURES 1
#define MODEL_USES_AS_INPUT_MASK ((MODEL_USES_AS_INPUT_INPUT_FEATURES << 0) | (MODEL_USES_AS_INPUT_DSP_FEATURES << 1)) 

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON 
#include <drivers/axon/nrf_axon_nn_infer.h>  
#include <axon/nrf_axon_platform.h> 
#include "nrf_edgeai_user_model_axon.h" 
#define P_MODEL_INSTANCE &model_axon_user_instance_96375
#else  // MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
#define P_MODEL_INSTANCE &model_neuton_user_instance_ 
#endif


#define NN_DECODED_OUTPUT_INIT                 \
.classif = {                                   \
   .predicted_class = 0,                       \
   .num_classes = MODEL_OUTPUTS_NUM,           \
}

//////////////////////////////////////////////////////////////////////////////
#define MODEL_NEURONS_NUM          26
#define MODEL_WEIGHTS_NUM          181
#define MODEL_PARAMS_TYPE          f32
#define MODEL_REORDERING           0

static const nrf_user_weight_t MODEL_WEIGHTS[] = {
 0.1356670, -0.5811272, -0.5084407, 0.9999979, 0.9708215, 0.2108291,
 -0.0057987, -0.1497352, 0.0901954, 0.2261870, 0.4403007, 1.0000000,
 -1.0000000, 0.2788839, 0.5294335, -0.2843294, -0.2608061, -0.4371449,
 0.9722773, -0.6131684, -0.4960986, -0.3038979, -0.7933262, -0.8932790,
 1.0000000, -0.8749989, -0.7569492, 0.0414528, 0.7564363, -0.2450719,
 -0.0456719, 0.6234072, -0.5539193, 0.3205627, 0.2978554, -0.2966034,
 0.3237374, 0.3182940, -0.4943888, -0.0248780, -0.3919618, -0.5523604,
 0.0983855, -0.1014653, 0.4567838, -0.3014629, -0.3534074, -0.9409182,
 0.7384263, -0.7600237, 0.4980565, 0.7506733, 1.0000000, 0.8050750,
 -1.0000000, -0.9785351, -0.3618870, -1.0000000, -0.1299891, 0.8888206,
 -1.0000000, -0.8638918, 0.5714122, 0.1490055, -0.4745246, 0.4959305,
 -0.3194453, 0.5225018, 0.9619751, -0.6396175, 0.2731402, -0.0771885,
 0.9861329, -0.2866566, -1.0000000, -0.8332337, 0.6842821, -1.0000000,
 -1.0000000, 0.0720726, 1.0000000, -0.7397951, 0.5141292, -0.1719278,
 0.1073678, -0.2774594, -0.9546033, 0.8482562, 1.0000000, -0.7617593,
 0.1731495, -0.9588344, 0.6501445, 0.9978364, -1.0000000, -0.6606370,
 0.9732506, -1.0000000, 0.8226237, 1.0000000, -0.7633214, 1.0000000,
 0.0183316, 0.4281892, -0.1903819, -0.2291292, -0.9013798, 0.8039243,
 -1.0000000, -0.0465964, 1.0000000, -0.1483743, 0.6839690, -0.7041767,
 0.9566148, -0.1858378, 1.0000000, 1.0000000, -0.4322847, 0.1540430,
 -0.9232903, 0.6223512, -0.2545605, 0.6075832, -0.9536936, 0.9999989,
 0.8089243, 0.6735367, -0.9960934, 0.3254693, 0.3359488, 0.1873076,
 -0.0937858, -0.8196129, 0.9616022, -1.0000000, 0.9999974, -0.7037603,
 -0.4426832, 0.9119523, 1.0000000, -0.8562922, 0.1830924, 0.2519529,
 -0.8463289, -0.9902344, -0.7599195, -0.4122240, 0.8905975, -0.4638531,
 0.3411511, 0.2121934, 0.1149065, 0.8318633, -0.0163478, 1.0000000,
 0.7226697, -0.8315108, 0.6642821, 0.4427256, 0.1642541, -0.5885231,
 0.5538874, -0.1640310, 0.5416666, 0.3091316, -1.0000000, -1.0000000,
 -0.9717967, -0.1321506, 0.5312814, -0.5362626, 0.6348400, -0.3828570,
 0.8103685, -1.0000000, -1.0000000, -1.0000000, -1.0000000, 0.9204343,
 -0.1566295 };

static const uint16_t MODEL_NEURONS_LINKS[] = {
 1, 2, 3, 4, 5, 6, 8, 9, 0, 0, 1, 2, 3, 4, 5, 7, 8, 9, 0, 1, 1, 2, 3, 4, 5,
 6, 7, 8, 9, 0, 1, 0, 2, 3, 6, 8, 9, 0, 1, 2, 3, 0, 1, 2, 3, 4, 6, 8, 9, 0,
 2, 3, 4, 0, 1, 2, 3, 4, 6, 7, 8, 9, 0, 1, 3, 5, 1, 3, 5, 6, 9, 3, 4, 0, 5,
 6, 9, 3, 7, 9, 0, 2, 6, 1, 4, 8, 9, 0, 9, 9, 0, 1, 9, 1, 11, 9, 1, 2, 4,
 5, 1, 6, 9, 1, 3, 4, 7, 11, 0, 1, 6, 7, 8, 9, 5, 9, 5, 15, 9, 2, 6, 1, 6,
 9, 6, 17, 9, 2, 13, 0, 2, 4, 7, 9, 2, 13, 19, 9, 0, 3, 7, 11, 0, 9, 0, 3,
 4, 1, 2, 6, 7, 9, 1, 2, 4, 9, 1, 2, 3, 6, 8, 9, 3, 4, 7, 13, 14, 22, 23,
 1, 5, 6, 8, 9, 4, 14, 21, 22, 23, 24, 9 };

static const uint16_t MODEL_NEURON_INTERNAL_LINKS_NUM[] = {
 0, 9, 20, 31, 41, 53, 66, 73, 79, 83, 89, 92, 95, 100, 108, 115, 118, 121,
 126, 129, 137, 142, 147, 156, 169, 180 };

static const uint16_t MODEL_NEURON_EXTERNAL_LINKS_NUM[] = {
 8, 18, 29, 37, 49, 62, 71, 77, 80, 87, 90, 93, 96, 103, 114, 116, 119,
 124, 127, 134, 138, 144, 152, 162, 174, 181 };

static const nrf_user_coeff_t MODEL_NEURON_ACTIVATION_WEIGHTS[] = {
 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000,
 40.0000000, 35.0124969, 35.0124969, 40.0000000, 40.0000000, 40.0000000,
 40.0000000, 39.9999619, 29.4032478, 39.9999847, 40.0000000, 40.0000000,
 40.0000000, 40.0000000, 40.0000000, 29.4032478, 40.0000000, 40.0000000,
 40.0000000, 40.0000000 };

static const uint8_t MODEL_NEURON_ACTIVATION_TYPE_MASK[] = {
 0xff, 0xea, 0xea, 0x1 };

static const uint16_t MODEL_OUTPUT_NEURONS_INDICES[] = {
 10, 12, 20, 8, 25, 16, 18 };

/** Model neurons activations buffer */ 
static nrf_user_neuron_t model_neurons_[MODEL_NEURONS_NUM];

/** Neuton model instance */ 
static const nrf_edgeai_model_neuton_t model_neuton_user_instance_ = { 
   .meta.p_neuron_internal_links_num = MODEL_NEURON_INTERNAL_LINKS_NUM,
   .meta.p_neuron_external_links_num = MODEL_NEURON_EXTERNAL_LINKS_NUM,
   .meta.p_output_neurons_indices    = MODEL_OUTPUT_NEURONS_INDICES,
   .meta.p_neuron_links              = MODEL_NEURONS_LINKS,
   .meta.p_neuron_act_type_mask      = MODEL_NEURON_ACTIVATION_TYPE_MASK,
   .meta.outputs_num                 = MODEL_OUTPUTS_NUM,
   .meta.neurons_num                 = MODEL_NEURONS_NUM,
   .meta.weights_num                 = MODEL_WEIGHTS_NUM,
   /// 
   .params.MODEL_PARAMS_TYPE = {
       .p_weights      = MODEL_WEIGHTS,
       .p_act_weights  = MODEL_NEURON_ACTIVATION_WEIGHTS,
       .p_neurons      = model_neurons_,
   },
};

//////////////////////////////////////////////////////////////////////////////
/** Input feature buffer element size, 
 * if quantization of model is bigger than input features size in bits, 
 * the size of input buffer should aligned to nrf_user_neuron_t */ 
#define INPUT_TYPE_SIZE \
    ((sizeof(nrf_user_input_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_input_t) : sizeof(nrf_user_neuron_t)) 

/** Input features window size in bytes to allocate statically */ 
#define INPUT_WINDOW_BUFFER_SIZE_BYTES \
    (INPUT_WINDOW_SIZE * INPUT_UNIQ_FEATURES_NUM * INPUT_TYPE_SIZE) 

static uint8_t input_window_[INPUT_WINDOW_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED; 

#define INPUT_WINDOW_MEMORY    &input_window_[0] 

static nrf_edgeai_window_ctx_t input_window_ctx_; 
#define P_INPUT_WINDOW_CTX     &input_window_ctx_ 

//////////////////////////////////////////////////////////////////////////////
/** The maximum number of extracted features that user used for all unique input features */
#define EXTRACTED_FEATURES_NUM  9

#define EXTRACTED_FEATURES_META_TYPE f32 

/** DSP feature buffer element size,
 * if quantization of model is bigger than DSP features size in bits,
 * the size of extracted DSP features buffer should aligned to nrf_user_neuron_t */
#define EXTRACTED_FEATURE_SIZE_BYTES                                                  \
    ((sizeof(nrf_user_feature_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_feature_t) : \
                                                            sizeof(nrf_user_neuron_t))

/** Size of extracted features buffer in bytes */
#define EXTRACTED_FEATURES_BUFFER_SIZE_BYTES (EXTRACTED_FEATURES_NUM * EXTRACTED_FEATURE_SIZE_BYTES) 

/** Defines feature extraction masks used as nrf_edgeai_features_mask_t,
 *  64 bit for one unique input feature, @ref nrf_edgeai_features_mask_t to see bitmask
 */

static const uint64_t FEATURES_EXTRACTION_MASK[] = {
 0x100000000000000, 0x10a00000000, 0xc00000000, 0x110200000000 };

/** Defines arguments used while feature extraction
 */

/** Defines arguments used while feature extraction
 */
static const nrf_user_input_t FEATURES_EXTRACTION_ARGUMENTS[] =
{ 4 };

/** Defines extracted features MIN scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MIN[] = {
 0.4386246, -10581.0000000, -16362.6396484, 71.4923782, 14.0000000,
 -15572.8398438, -16173.0000000, 638.4912109, 6.7500000 };

/** Defines extracted features MAX scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MAX[] = {
 1.9403485, 32767.0000000, 12630.4404297, 18157.4785156, 65381.0000000,
 24637.4394531, 32767.0000000, 20231.0039062, 48299.7500000 };

/** Memory allocation to store extracted features during DSP pipeline */
static uint8_t extracted_features_buffer_[EXTRACTED_FEATURES_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED;


/** Timedomain features processing context  */
#define P_TIMEDOMAIN_FEATURES_CTX  NULL
/** Timedomain features in feature extraction pipeline  */
static const nrf_edgeai_features_pipeline_func_f32_t timedomain_features_[] = {
    nrf_edgeai_feature_utility_tss_sum_f32,
    nrf_edgeai_feature_min_max_range_f32,
    nrf_edgeai_feature_mean_f32,
    nrf_edgeai_feature_rms_f32,
    nrf_edgeai_feature_p2p_lf_f32,
    nrf_edgeai_feature_hjorth_f32
 };

static const nrf_edgeai_features_pipeline_ctx_t timedomain_pipeline_ = {
    .functions_num     = sizeof(timedomain_features_) / sizeof(timedomain_features_[0]),
    .functions.p_void  = timedomain_features_,
    .p_ctx             = P_TIMEDOMAIN_FEATURES_CTX,
};
#define P_TIMEDOMAIN_PIPELINE &timedomain_pipeline_ 

#define P_FREQDOMAIN_PIPELINE NULL

#define P_CUSTOMDOMAIN_PIPELINE NULL

static nrf_edgeai_dsp_pipeline_t dsp_pipeline_ = { 
   .features = {  
       .p_masks = (const nrf_edgeai_features_mask_t*)FEATURES_EXTRACTION_MASK, 
       .buffer.p_void = extracted_features_buffer_, 
       .overall_num = EXTRACTED_FEATURES_NUM, 
       .masks_num = sizeof(FEATURES_EXTRACTION_MASK) / sizeof(FEATURES_EXTRACTION_MASK[0]), 

       .p_timedomain_pipeline = P_TIMEDOMAIN_PIPELINE, 
       .p_freqdomain_pipeline = P_FREQDOMAIN_PIPELINE, 
       .p_customdomain_pipeline = P_CUSTOMDOMAIN_PIPELINE, 

       .meta.EXTRACTED_FEATURES_META_TYPE = { 
           .p_min = EXTRACTED_FEATURES_SCALE_MIN, 
           .p_max = EXTRACTED_FEATURES_SCALE_MAX, 
       .p_arguments = FEATURES_EXTRACTION_ARGUMENTS, 
       },
   }, 
}; 

#define P_DSP_PIPELINE         &dsp_pipeline_ 


//////////////////////////////////////////////////////////////////////////////
#define NN_INPUT_INIT_INTERFACE        nrf_edgeai_input_init_sliding_window 
#define NN_INPUT_FEED_INTERFACE        nrf_edgeai_input_feed_sliding_window_f32 
#define NN_PROCESS_FEATURES_INTERFACE  nrf_edgeai_process_features_dsp_f32_f32 
#define NN_INIT_INFERENCE_INTERFACE    nrf_edgeai_init_inference_neuton 
#define NN_RUN_INFERENCE_INTERFACE     nrf_edgeai_run_inference_neuton_f32 
#define NN_PROPAGATE_OUTPUTS_INTERFACE nrf_edgeai_output_propagate_neuton_f32 
#define NN_DECODE_OUTPUTS_INTERFACE    nrf_edgeai_output_decode_classification_f32 

//////////////////////////////////////////////////////////////////////////////

static nrf_user_output_t model_outputs_[MODEL_OUTPUTS_NUM];

//////////////////////////////////////////////////////////////////////////////

static nrf_edgeai_t nrf_edgeai_ = {
    ///
    .metadata.p_solution_id     = EDGEAI_LAB_SOLUTION_ID_STR,
    .metadata.version.combined  = EDGEAI_RUNTIME_VERSION_COMBINED,
    ///   
    .input.p_used_for_lags_mask = INPUT_FEATURES_USED_FOR_LAGS_MASK,
    .input.p_usage_mask         = INPUT_FEATURES_USAGE_MASK,
    .input.type                 = INPUT_FEATURE_DATA_TYPE,
    .input.unique_num           = INPUT_UNIQ_FEATURES_NUM,
    .input.unique_num_used      = INPUT_UNIQ_FEATURES_USED_NUM,
    .input.unique_scales_num    = INPUT_UNIQUE_SCALES_NUM,
    .input.window_size          = INPUT_WINDOW_SIZE,
    .input.window_shift         = INPUT_WINDOW_SHIFT,
    .input.subwindow_num        = INPUT_SUBWINDOW_NUM,
    .input.window_memory.p_void = INPUT_WINDOW_MEMORY,
    .input.p_window_ctx         = P_INPUT_WINDOW_CTX,

    .input.scale.INPUT_TYPE = {
        .p_min = INPUT_FEATURES_SCALE_MIN,
        .p_max = INPUT_FEATURES_SCALE_MAX,
    }, 
    ///
    .p_dsp = P_DSP_PIPELINE,
    ///
    .model.type                 = (nrf_edgeai_model_type_t)MODEL_TYPE,
    .model.task                 = (nrf_edgeai_model_task_t)MODEL_TASK,
    .model.instance.p_void      = P_MODEL_INSTANCE,
    .model.output.memory.p_void = model_outputs_,
    .model.output.num           = MODEL_OUTPUTS_NUM,
    .model.uses_as_input.all    = MODEL_USES_AS_INPUT_MASK,
    ///
    .interfaces.input_init          = NN_INPUT_INIT_INTERFACE,
    .interfaces.feed_inputs         = NN_INPUT_FEED_INTERFACE,
    .interfaces.process_features    = NN_PROCESS_FEATURES_INTERFACE,
    .interfaces.init_inference      = NN_INIT_INFERENCE_INTERFACE,
    .interfaces.run_inference       = NN_RUN_INFERENCE_INTERFACE,
    .interfaces.propagate_outputs   = NN_PROPAGATE_OUTPUTS_INTERFACE,
    .interfaces.decode_outputs      = NN_DECODE_OUTPUTS_INTERFACE,
    ///
    .decoded_output = { NN_DECODED_OUTPUT_INIT },
};

//////////////////////////////////////////////////////////////////////////////

nrf_edgeai_t* nrf_edgeai_user_model_96375(void)
{
    return &nrf_edgeai_;
}

//////////////////////////////////////////////////////////////////////////////

uint32_t nrf_edgeai_user_model_size_96375(void)
{
    uint32_t model_size = 0;

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
    model_size +=
        (sizeof(MODEL_WEIGHTS) + sizeof(MODEL_NEURONS_LINKS) +
         sizeof(MODEL_NEURON_EXTERNAL_LINKS_NUM) + sizeof(MODEL_NEURON_INTERNAL_LINKS_NUM) +
         sizeof(MODEL_NEURON_ACTIVATION_WEIGHTS) + sizeof(MODEL_NEURON_ACTIVATION_TYPE_MASK) +
         sizeof(MODEL_OUTPUT_NEURONS_INDICES));

#if MODEL_TASK == __NRF_EDGEAI_TASK_ANOMALY_DETECTION
    model_size += sizeof(MODEL_AVERAGE_EMBEDDING) + sizeof(MODEL_OUTPUT_SCALE_MIN) +
                  sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#if MODEL_TASK == __NRF_EDGEAI_TASK_REGRESSION
    model_size += sizeof(MODEL_OUTPUT_SCALE_MIN) + sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#elif MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON
    const nrf_axon_nn_compiled_model_s* p_axon_model = P_MODEL_INSTANCE;

    model_size += sizeof(*p_axon_model);
    model_size += p_axon_model->model_const_size;
    model_size += p_axon_model->cmd_buffer_len * sizeof(NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE);

    if (p_axon_model->persistent_vars.buf_ptr != NULL)
    {
        model_size +=
            sizeof(nrf_axon_nn_model_persistent_var_s) * p_axon_model->persistent_vars.count;
    }

#endif

    return model_size;
}


