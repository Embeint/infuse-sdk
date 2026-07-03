/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/zbus/zbus.h>

#include <math.h>

static int zbus_chan_read_no_wait(const struct zbus_channel *chan, void *msg)
{
	return zbus_chan_read(chan, msg, K_NO_WAIT);
}

/* Symbols available for algorithms to use.
 * Symbols cannot be removed from this list without a TBD major version break.
 * Symbol ABI cannot be changed without a TBD major version break.
 * Compile once run anywhere requires that ALL objects with variable internal
 * fields are accessed through helper functions exported here, and NO variable
 * sized objects can be passed as parameters.
 *
 * Relevant examples include struct zbus_channel and k_timeout_t
 */
EXPORT_GROUP_SYMBOL(INFUSE_ALG, printk);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, zbus_chan_from_id);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, zbus_chan_const_msg);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, zbus_chan_read_no_wait);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, zbus_chan_finish);

/* Common single-precision math functions from 'math.h'
 * Excludes the Gauss error functions 'errf' and 'erfcf'
 */
EXPORT_GROUP_SYMBOL(INFUSE_ALG, atanf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, cosf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, sinf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, tanf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, tanhf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, frexpf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, modff);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, ceilf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fabsf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, floorf);

EXPORT_GROUP_SYMBOL(INFUSE_ALG, acosf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, asinf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, atan2f);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, coshf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, sinhf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, expf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, ldexpf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, logf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, log10f);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, powf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, sqrtf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fmodf);

EXPORT_GROUP_SYMBOL(INFUSE_ALG, exp2f);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, scalblnf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, tgammaf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, nearbyintf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, lrintf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, llrintf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, roundf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, lroundf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, llroundf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, truncf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, remquof);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fdimf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fmaxf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fminf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, fmaf);

EXPORT_GROUP_SYMBOL(INFUSE_ALG, infinityf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, nanf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, copysignf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, logbf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, ilogbf);

EXPORT_GROUP_SYMBOL(INFUSE_ALG, asinhf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, cbrtf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, nextafterf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, rintf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, scalbnf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, scalbf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, log1pf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, expm1f);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, getpayloadf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, significandf);

EXPORT_GROUP_SYMBOL(INFUSE_ALG, acoshf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, atanhf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, remainderf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, gammaf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, lgammaf);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, log2f);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, hypotf);

#ifndef CONFIG_FPU

/* Single precision floating point to integer conversion */
extern int __aeabi_f2iz(float x);
extern unsigned int __aeabi_f2uiz(float x);
extern long long __aeabi_f2lz(float x);
extern unsigned long long __aeabi_f2ulz(float x);

EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_f2iz);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_f2uiz);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_f2lz);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_f2ulz);

/* Integer to Single precision floating point conversion */
extern float __aeabi_i2f(int x);
extern float __aeabi_ui2f(unsigned int x);
extern float __aeabi_l2f(long long x);
extern float __aeabi_ul2f(unsigned long long x);

EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_i2f);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_ui2f);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_l2f);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_ul2f);

/* Single precision floating point arithemtic */
extern float __aeabi_fadd(float x, float y);
extern float __aeabi_fdiv(float n, float d);
extern float __aeabi_fmul(float x, float y);
extern float __aeabi_frsub(float x, float y);
extern float __aeabi_fsub(float x, float y);

EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fadd);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fdiv);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fmul);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_frsub);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fsub);

/* Single precision floating point comparisons */
extern void __aeabi_cfcmpeq(float x, float y);
extern void __aeabi_cfcmple(float x, float y);
extern void __aeabi_cfrcmple(float x, float y);
extern int __aeabi_fcmpeq(float x, float y);
extern int __aeabi_fcmplt(float x, float y);
extern int __aeabi_fcmple(float x, float y);
extern int __aeabi_fcmpge(float x, float y);
extern int __aeabi_fcmpgt(float x, float y);
extern int __aeabi_fcmpun(float x, float y);

EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_cfcmpeq);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_cfcmple);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_cfrcmple);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmpeq);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmplt);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmple);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmpge);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmpgt);
EXPORT_GROUP_SYMBOL(FP_SOFT, __aeabi_fcmpun);

#endif /* CONFIG_FPU */
