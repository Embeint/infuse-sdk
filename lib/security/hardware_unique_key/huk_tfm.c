/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <psa/crypto.h>

#include "crypto_keys/tfm_builtin_key_ids.h"

int hardware_unique_key_init(void)
{
	/* All initialisation done by TF-M */
	return 0;
}

psa_key_id_t hardware_unique_key_id(void)
{
	return (psa_key_id_t)TFM_BUILTIN_KEY_ID_HUK;
}
