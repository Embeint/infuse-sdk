/**
 * @file
 * @copyright 2025 Nordic Semiconductor ASA
 * @copyright 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include <nrf_edgeai/nrf_edgeai.h>
#include "nrf_edgeai_generated/nrf_edgeai_user_model.h"

#define USER_WINDOW_SIZE       128       /* Samples per inference window */
#define USER_UNIQ_INPUTS_NUM   2         /* 2 vibration sensors: axis X and Y */
#define USER_ANOMALY_THRESHOLD 0.000025f /* Anomaly score threshold, high sensitivity */

/**
 * Healthy Gear Vibration Baseline Data
 *
 * Expected model output:
 *   Anomaly score << USER_ANOMALY_THRESHOLD (well below threshold, indicating healthy state)
 *   Model confidence: High certainty of normal operation
 */
static const flt32_t input_normal[USER_WINDOW_SIZE * USER_UNIQ_INPUTS_NUM] = {
	2.523464899, 2.43016754,  2.521493825, 2.430003284, 2.522479362, 2.429674773, 2.521329569,
	2.431810097, 2.522479362, 2.43131733,  2.519358495, 2.430496051, 2.521986593, 2.430660307,
	2.521001056, 2.431153074, 2.521001056, 2.431153074, 2.523957667, 2.431974352, 2.522150849,
	2.430003284, 2.521822337, 2.429839029, 2.524943204, 2.428853495, 2.522643618, 2.429839029,
	2.522479362, 2.430660307, 2.521658081, 2.429839029, 2.520015519, 2.430824563, 2.521001056,
	2.432138608, 2.521822337, 2.431810097, 2.520672544, 2.431645841, 2.521493825, 2.430988818,
	2.522315105, 2.43131733,  2.521986593, 2.431645841, 2.522315105, 2.430496051, 2.522643618,
	2.429510518, 2.522315105, 2.43016754,  2.521165312, 2.429674773, 2.522150849, 2.429510518,
	2.521822337, 2.429839029, 2.521329569, 2.43016754,  2.521493825, 2.429839029, 2.522479362,
	2.429346262, 2.521986593, 2.430660307, 2.521493825, 2.430988818, 2.520344032, 2.430331796,
	2.521165312, 2.429182006, 2.5208368,   2.431974352, 2.522479362, 2.431974352, 2.521329569,
	2.432631375, 2.521165312, 2.431153074, 2.522643618, 2.431810097, 2.523629155, 2.431153074,
	2.52297213,  2.430824563, 2.522479362, 2.430331796, 2.521493825, 2.430331796, 2.520344032,
	2.429510518, 2.522807874, 2.43131733,  2.523629155, 2.429839029, 2.521658081, 2.430824563,
	2.521658081, 2.430824563, 2.521001056, 2.429839029, 2.522479362, 2.430003284, 2.524121923,
	2.428360728, 2.523136386, 2.429510518, 2.521329569, 2.430331796, 2.520672544, 2.431974352,
	2.523629155, 2.431153074, 2.521822337, 2.431153074, 2.521165312, 2.431810097, 2.520508288,
	2.432631375, 2.5208368,   2.432138608, 2.521329569, 2.431153074, 2.523136386, 2.431153074,
	2.523793411, 2.431645841, 2.520179775, 2.431153074, 2.520672544, 2.431810097, 2.524450436,
	2.429346262, 2.522150849, 2.428689239, 2.521493825, 2.429674773, 2.521493825, 2.429674773,
	2.522150849, 2.430660307, 2.522643618, 2.430496051, 2.522479362, 2.431153074, 2.521165312,
	2.431153074, 2.522807874, 2.429839029, 2.523300642, 2.430824563, 2.52297213,  2.429346262,
	2.522150849, 2.429017751, 2.522150849, 2.430988818, 2.523629155, 2.430496051, 2.523136386,
	2.430660307, 2.522150849, 2.430331796, 2.522479362, 2.430988818, 2.522643618, 2.431974352,
	2.522479362, 2.431153074, 2.522807874, 2.431974352, 2.521329569, 2.432631375, 2.520344032,
	2.431481585, 2.521822337, 2.430003284, 2.522643618, 2.43131733,  2.523793411, 2.430496051,
	2.522315105, 2.430824563, 2.522807874, 2.430824563, 2.523629155, 2.429510518, 2.523464899,
	2.429182006, 2.521822337, 2.430496051, 2.521822337, 2.430824563, 2.521329569, 2.432302864,
	2.522315105, 2.432467119, 2.521822337, 2.432302864, 2.522315105, 2.432138608, 2.522150849,
	2.430824563, 2.524778948, 2.430003284, 2.522643618, 2.430003284, 2.522479362, 2.430988818,
	2.522807874, 2.432795631, 2.522479362, 2.430496051, 2.523300642, 2.430003284, 2.521658081,
	2.430003284, 2.5208368,   2.43131733,  2.523629155, 2.431481585, 2.523300642, 2.430660307,
	2.522315105, 2.430660307, 2.522807874, 2.430824563, 2.522315105, 2.430660307, 2.521493825,
	2.430003284, 2.523793411, 2.43016754,  2.523300642, 2.430496051, 2.521493825, 2.430331796,
	2.521822337, 2.43016754,  2.521822337, 2.428853495, 2.523300642, 2.430331796, 2.522150849,
	2.43016754,  2.522315105, 2.431153074, 2.521658081, 2.432302864, 2.523136386, 2.430496051,
	2.521165312, 2.431481585, 2.5208368,   2.430988818,
};

/**
 * Anomalous Gear Vibration Data - Faulty Operation
 *
 * Expected model output:
 *   Anomaly score >= USER_ANOMALY_THRESHOLD (at or above threshold, indicating fault)
 *   Model confidence: High certainty of abnormal operation
 *   Risk Level: High - maintenance or replacement required
 */
static const flt32_t input_anomalous[USER_WINDOW_SIZE * USER_UNIQ_INPUTS_NUM] = {
	2.514595067, 2.435423722, 2.508846103, 2.433288398, 2.516730396, 2.438216068, 2.542682877,
	2.42638966,  2.520179775, 2.425732638, 2.532006221, 2.424582848, 2.51870147,  2.437723301,
	2.546625028, 2.418998157, 2.497019667, 2.43739479,  2.521493825, 2.428032217, 2.501290324,
	2.431810097, 2.532663246, 2.419326669, 2.523300642, 2.448892691, 2.498169459, 2.435423722,
	2.519687007, 2.437559046, 2.5208368,   2.44544332,  2.526257254, 2.442158206, 2.497676691,
	2.402736858, 2.538412214, 2.434602443, 2.514923579, 2.436573511, 2.530692171, 2.409471334,
	2.51360953,  2.421954758, 2.495377107, 2.423104548, 2.511309945, 2.470245947, 2.546460772,
	2.426553916, 2.495705619, 2.441008415, 2.52510746,  2.427703705, 2.512788249, 2.432795631,
	2.551716974, 2.439858625, 2.526750022, 2.406843246, 2.519851263, 2.441172671, 2.522479362,
	2.422776036, 2.491927731, 2.463511457, 2.549088872, 2.389924931, 2.490613683, 2.460390596,
	2.543668415, 2.404050902, 2.501947348, 2.442486717, 2.52987089,  2.415877301, 2.508189079,
	2.44544332,  2.505889494, 2.433452653, 2.558451484, 2.422611781, 2.532498989, 2.433781165,
	2.543668415, 2.429346262, 2.531020683, 2.422119014, 2.513281018, 2.425896893, 2.540054777,
	2.427375194, 2.519687007, 2.456284201, 2.505889494, 2.429182006, 2.524614692, 2.418998157,
	2.520344032, 2.449221202, 2.541697339, 2.442815228, 2.528392584, 2.427210938, 2.5208368,
	2.420147947, 2.540876058, 2.450370993, 2.527407047, 2.405364946, 2.504082677, 2.415384535,
	2.514102298, 2.433781165, 2.509667384, 2.457926759, 2.53463432,  2.426718172, 2.511638457,
	2.411606656, 2.515087835, 2.452834829, 2.537755189, 2.442322461, 2.545146721, 2.411935168,
	2.533648783, 2.42491136,  2.514923579, 2.418505391, 2.52987089,  2.410128357, 2.521165312,
	2.423268803, 2.528228328, 2.44971397,  2.503589909, 2.431810097, 2.520015519, 2.425404126,
	2.500140532, 2.450042481, 2.527735559, 2.445936087, 2.49981202,  2.44199395,  2.519358495,
	2.419819435, 2.53857647,  2.444457786, 2.560258304, 2.423104548, 2.504575445, 2.434109676,
	2.54563949,  2.431153074, 2.506382262, 2.436902023, 2.528064072, 2.428360728, 2.519194239,
	2.446428855, 2.498662227, 2.429839029, 2.530856427, 2.439037347, 2.512952506, 2.44544332,
	2.502604372, 2.45710548,  2.530527915, 2.424582848, 2.528064072, 2.415713046, 2.537590933,
	2.451520783, 2.541368827, 2.414891768, 2.500797556, 2.436573511, 2.52855684,  2.417519857,
	2.509667384, 2.435587977, 2.507039286, 2.424582848, 2.526585766, 2.426718172, 2.501618836,
	2.436245,    2.532827502, 2.433781165, 2.537590933, 2.428032217, 2.519194239, 2.436409256,
	2.52642151,  2.428360728, 2.554016562, 2.42228327,  2.509174615, 2.451028016, 2.522315105,
	2.434438188, 2.511145688, 2.432467119, 2.480594069, 2.423925826, 2.546296515, 2.427867961,
	2.506710774, 2.451356527, 2.513938042, 2.421954758, 2.552045486, 2.448892691, 2.507367798,
	2.419983691, 2.56633579,  2.428196472, 2.521658081, 2.433124142, 2.522643618, 2.436245,
	2.540547545, 2.436245,    2.505232469, 2.428853495, 2.512623993, 2.431645841, 2.521001056,
	2.410456868, 2.513116762, 2.42638966,  2.538412214, 2.442322461, 2.529542378, 2.449549714,
	2.520344032, 2.425404126, 2.566007278, 2.426882427, 2.527899816, 2.432467119, 2.526585766,
	2.436409256, 2.52987089,  2.432467119, 2.480429813, 2.421954758, 2.537426676, 2.421297736,
	2.503918421, 2.430660307, 2.487328563, 2.430988818,
};

ZTEST(nrf_edge_ai, test_nrf_edge_ai_runtime_version)
{
	nrf_edgeai_rt_version_t v = nrf_edgeai_runtime_version();
	uint32_t version_sortable;
	int rc;

	printk("nRF Edge AI runtime version: %d.%d.%d\n", v.field.major, v.field.minor,
	       v.field.patch);

	/* The union combined value is populated in the wrong order to be useful for comparisons */
	version_sortable = (v.field.major << 24) | (v.field.minor << 16) | v.field.patch;

	/* No versions older than 2.2.1 */
	zassert_true(version_sortable >= 0x02020001);

	/* Version should have been pushed into KV store by `common_boot.c` */
	KV_KEY_TYPE(KV_KEY_NRF_EDGE_AI_RUNTIME_VERSION) kv_edgeai_version;

	rc = kv_store_read(KV_KEY_NRF_EDGE_AI_RUNTIME_VERSION, &kv_edgeai_version,
			   sizeof(kv_edgeai_version));
	zassert_equal(sizeof(kv_edgeai_version), rc);
	zassert_equal(v.field.major, kv_edgeai_version.major);
	zassert_equal(v.field.minor, kv_edgeai_version.minor);
	zassert_equal(v.field.patch, kv_edgeai_version.patch);
}

ZTEST(nrf_edge_ai, test_nrf_edge_ai_model_info)
{
	nrf_edgeai_input_type_t input_type;
	nrf_edgeai_model_type_t model_type;
	nrf_edgeai_t *user_model;
	uint16_t inputs_num;
	uint16_t input_window;

	/* Get user generated model pointer */
	user_model = nrf_edgeai_user_model();
	zassert_not_null(user_model);

	model_type = nrf_edgeai_model_type(user_model);
	zassert_equal(NRF_EDGEAI_MODEL_NEUTON, model_type);

	input_type = nrf_edgeai_input_type(user_model);
	zassert_equal(NRF_EDGEAI_INPUT_F32, input_type);

	inputs_num = nrf_edgeai_uniq_inputs_num(user_model);
	zassert_equal(USER_UNIQ_INPUTS_NUM, inputs_num);

	input_window = nrf_edgeai_input_window_size(user_model);
	zassert_equal(USER_WINDOW_SIZE, input_window);
}

static float model_predict_streaming(nrf_edgeai_t *user_model, const float *input, size_t input_len)
{
	nrf_edgeai_err_t res;

	/* Feed vibration samples to Edge AI runtime in streaming mode */
	for (size_t i = 0; i < input_len; i += USER_UNIQ_INPUTS_NUM) {
		/* Extract one sample pair: [X_acceleration, Y_acceleration] */
		flt32_t input_sample[USER_UNIQ_INPUTS_NUM];

		input_sample[0] = input[i];     /* Sensor 1: X-axis */
		input_sample[1] = input[i + 1]; /* Sensor 2: Y-axis */

		/* Feed this sample pair into the model's windowing buffer */
		res = nrf_edgeai_feed_inputs(user_model, input_sample, USER_UNIQ_INPUTS_NUM);
		if (res == NRF_EDGEAI_ERR_INPROGRESS) {
			continue;
		}
		/* No errors */
		zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);
	}
	/* Last input resulted in SUCCESS */
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	/* Input buffer has reached 128 samples - run inference on the complete window */
	res = nrf_edgeai_run_inference(user_model);
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	/* Extract anomaly score from model's decoded output */
	return user_model->decoded_output.anomaly.score;
}

ZTEST(nrf_edge_ai, test_nrf_edge_ai_integration_streaming)
{
	nrf_edgeai_t *user_model;
	float anomaly_score;
	nrf_edgeai_err_t res;

	/* Get user generated model pointer */
	user_model = nrf_edgeai_user_model();
	zassert_not_null(user_model);

	/* Initialize Edge AI runtime for inference execution */
	res = nrf_edgeai_init(user_model);
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	printk("Anomaly Score (threshold): %.6f\n", (double)USER_ANOMALY_THRESHOLD);

	/* Feed normal samples to runtime in streaming mode */
	anomaly_score = model_predict_streaming(user_model, input_normal, ARRAY_SIZE(input_normal));
	printk("Anomaly Score    (normal): %.6f\n", (double)anomaly_score);
	zassert_true(anomaly_score < USER_ANOMALY_THRESHOLD);

	/* Feed anomalous samples to runtime in streaming mode */
	anomaly_score =
		model_predict_streaming(user_model, input_anomalous, ARRAY_SIZE(input_anomalous));
	printk("Anomaly Score (anomalous): %.6f\n", (double)anomaly_score);
	zassert_true(anomaly_score >= USER_ANOMALY_THRESHOLD);
}

static float model_predict_batch(nrf_edgeai_t *user_model, const float *input, size_t input_len)
{
	nrf_edgeai_err_t res;

	/* Feed vibration samples to Edge AI runtime in streaming mode */

	res = nrf_edgeai_feed_inputs(user_model, (void *)input, input_len);
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	/* Input buffer has reached 128 samples - run inference on the complete window */
	res = nrf_edgeai_run_inference(user_model);
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	/* Extract anomaly score from model's decoded output */
	return user_model->decoded_output.anomaly.score;
}

ZTEST(nrf_edge_ai, test_nrf_edge_ai_integration_batch)
{
	nrf_edgeai_t *user_model;
	float anomaly_score;
	nrf_edgeai_err_t res;

	/* Get user generated model pointer */
	user_model = nrf_edgeai_user_model();
	zassert_not_null(user_model);

	/* Initialize Edge AI runtime for inference execution */
	res = nrf_edgeai_init(user_model);
	zassert_equal(NRF_EDGEAI_ERR_SUCCESS, res);

	printk("Anomaly Score (threshold): %.6f\n", (double)USER_ANOMALY_THRESHOLD);

	/* Feed normal samples to runtime in batch mode */
	anomaly_score = model_predict_batch(user_model, input_normal, ARRAY_SIZE(input_normal));
	printk("Anomaly Score    (normal): %.6f\n", (double)anomaly_score);
	zassert_true(anomaly_score < USER_ANOMALY_THRESHOLD);

	/* Feed anomalous samples to runtime in batch mode */
	anomaly_score =
		model_predict_batch(user_model, input_anomalous, ARRAY_SIZE(input_anomalous));
	printk("Anomaly Score (anomalous): %.6f\n", (double)anomaly_score);
	zassert_true(anomaly_score >= USER_ANOMALY_THRESHOLD);
}

ZTEST_SUITE(nrf_edge_ai, NULL, NULL, NULL, NULL, NULL);
