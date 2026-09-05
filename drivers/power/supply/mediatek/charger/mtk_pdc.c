/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <mt-plat/mtk_battery.h>
#include "mtk_intf.h"

#define PD_MIN_WATT 5000000
#define PD_VBUS_IR_DROP_THRESHOLD 1200

/* Cable impedance measurement currents (mA) */
#define PDC_CABLE_MEAS_HIGH_CURRENT	2500000	/* 2.5A */
#define PDC_CABLE_MEAS_LOW_CURRENT	500000	/* 0.5A */
#define PDC_CABLE_MEAS_HIGH_MA		2500
#define PDC_CABLE_MEAS_LOW_MA		500

static struct pdc *pd;

bool pdc_is_ready(void)
{
	return adapter_is_support_pd();
}

/*
 * pdc_check_cable_impedance - measure cable resistance via dual-current sampling
 *
 * Method: set input current to HIGH (2.5A) and LOW (0.5A), measure VBUS at
 * each point. Cable R = |V1 - V2| / |I1 - I2| (in mOhm).
 * Result is stored in pd->r_cable and used to:
 *   1. Limit max input current based on cable quality thresholds
 *   2. Compensate voltage request in pdc_get_setting() for IR drop
 *
 * Reference: MediaTek PE4.0 cable compensation in mtk_pe40.c lines 515-534
 *            and mtk_pdc_intf.c mtk_pdc_check_cable_impedance()
 */
static void pdc_check_cable_impedance(void)
{
	int vchr1 = 0, vchr2 = 0;
	int old_aicr = 0;
	bool mivr_state = false;
	int cable_imp;

	if (!pd || pd->cable_imp_checked)
		return;

	/* Skip if battery voltage too low (pre-charge) */
	if (battery_get_bat_voltage() * 1000 < pd->data.vbat_cable_imp_threshold) {
		chr_err("%s: VBAT too low (%d uV < %d uV), skip\n",
			__func__, battery_get_bat_voltage() * 1000,
			pd->data.vbat_cable_imp_threshold);
		pd->cable_imp_checked = true;
		pd->r_cable = 0;
		pd->cable_imp_good = true;
		return;
	}

	/* Save current input current limit */
	charger_get_input_current(&old_aicr);

	/* Set high current, wait for VBUS to settle */
	charger_set_input_current(PDC_CABLE_MEAS_HIGH_CURRENT);
	msleep(250);

	/* Check MIVR (minimum input voltage regulation) - if active,
	 * charger can't maintain voltage → cable is too resistive */
	charger_get_mivr_state(&mivr_state);
	if (mivr_state) {
		chr_err("%s: MIVR triggered at high current, cable bad\n",
			__func__);
		cable_imp = pd->data.cable_imp_threshold + 1;
		goto done;
	}

	vchr1 = battery_get_vbus();	/* returns mV */

	/* Set low current, wait for VBUS to settle */
	charger_set_input_current(PDC_CABLE_MEAS_LOW_CURRENT);
	msleep(150);

	vchr2 = battery_get_vbus();

	/* Calculate cable impedance: R = |V1 - V2| / |I1 - I2| in mOhm
	 * V in mV, I in mA → R in mOhm directly
	 */
	cable_imp = (vchr1 > vchr2 ? vchr1 - vchr2 : vchr2 - vchr1) * 1000 /
			    (PDC_CABLE_MEAS_HIGH_MA - PDC_CABLE_MEAS_LOW_MA);

done:
	pd->r_cable = cable_imp;
	pd->cable_imp_checked = true;
	pd->cable_imp_good = (cable_imp < pd->data.cable_imp_threshold);

	/* Restore original input current limit */
	charger_set_input_current(old_aicr);

	if (pd->cable_imp_good)
		chr_info("%s: r_cable=%d mOhm vchr1=%d vchr2=%d threshold=%d\n",
			__func__, cable_imp, vchr1, vchr2,
			pd->data.cable_imp_threshold);
	else
		chr_err("%s: BAD cable r_cable=%d mOhm threshold=%d\n",
			__func__, cable_imp, pd->data.cable_imp_threshold);
}

void pdc_init_table(void)
{
	pd->cap.nr = 0;
	pd->cap.selected_cap_idx = -1;

	if (pdc_is_ready())
		adapter_get_cap(&pd->cap);
	else
		chr_err("mtk_is_pdc_ready is fail\n");

	chr_err("[%s] nr:%d default:%d\n", __func__, pd->cap.nr,
	pd->cap.selected_cap_idx);
}

void pdc_get_reset_idx(void)
{
	struct pd_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;

	if (pd->pd_reset_idx == -1) {
		for (i = 0; i < cap->nr; i++) {

			if (cap->min_mv[i] < pd->vbus_l ||
				cap->max_mv[i] < pd->vbus_l ||
				cap->min_mv[i] > pd->vbus_l ||
				cap->max_mv[i] > pd->vbus_l) {
				continue;
			}
			idx = i;
		}
		pd->pd_reset_idx = idx;
		chr_err("[%s]reset idx:%d vbus:%d %d\n", __func__,
			idx, cap->min_mv[idx], cap->max_mv[idx]);
	}
}

int pdc_set_mivr(int uV)
{
	int ret = 0;

	ret = charger_set_mivr(uV);
	if (ret < 0)
		chr_err("%s: failed, ret = %d\n", __func__, ret);

	return ret;
}


int pdc_get_idx(int selected_idx,
	int *boost_idx, int *buck_idx)
{
	struct pd_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;
	idx = selected_idx;

	if (idx < 0) {
		chr_err("[%s] invalid idx:%d\n", __func__, idx);
		*boost_idx = 0;
		*buck_idx = 0;
		return -1;
	}

	/* get boost_idx */
	for (i = 0; i < cap->nr; i++) {

		if (cap->min_mv[i] < pd->vbus_l ||
			cap->max_mv[i] < pd->vbus_l) {
			chr_err("min_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_l);
			continue;
		}

		if (cap->min_mv[i] > pd->vbus_h ||
			cap->max_mv[i] > pd->vbus_h) {
			chr_err("max_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_h);
			continue;
		}

		if (idx == selected_idx) {
			if (cap->maxwatt[i] > cap->maxwatt[idx])
				idx = i;
		} else {
			if (cap->maxwatt[i] < cap->maxwatt[idx] &&
				cap->maxwatt[i] > cap->maxwatt[selected_idx])
				idx = i;
		}
	}
	*boost_idx = idx;
	idx = selected_idx;

	/* get buck_idx */
	for (i = 0; i < cap->nr; i++) {

		if (cap->min_mv[i] < pd->vbus_l ||
			cap->max_mv[i] < pd->vbus_l) {
			chr_err("min_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_l);
			continue;
		}

		if (cap->min_mv[i] > pd->vbus_h ||
			cap->max_mv[i] > pd->vbus_h) {
			chr_err("max_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_h);
			continue;
		}

		if (idx == selected_idx) {
			if (cap->maxwatt[i] < cap->maxwatt[idx])
				idx = i;
		} else {
			if (cap->maxwatt[i] > cap->maxwatt[idx] &&
				cap->maxwatt[i] < cap->maxwatt[selected_idx])
				idx = i;
		}
	}
	*buck_idx = idx;

	return 0;
}

	int pdc_setup(int idx)
{
	int ret = -100;
	unsigned int mivr;
	unsigned int oldmivr = 4600000;
	unsigned int oldmA = 3000000;
	bool force_update = false;
	int vbus_ir_drop;

	if (pd->pd_idx == idx) {
		charger_get_mivr(&oldmivr);

		if (pd->cap.max_mv[idx] - oldmivr / 1000 >
			PD_VBUS_IR_DROP_THRESHOLD)
			force_update = true;
	}

	/*
	 * Calculate expected IR drop across cable:
	 * - If cable impedance was measured, use I × R (mOhm × mA / 1000 = mV)
	 * - Otherwise fall back to fixed 1200mV threshold
	 */
	if (pd->cable_imp_checked && pd->r_cable > 0)
		vbus_ir_drop = pd->r_cable * pd->cap.ma[idx] / 1000;
	else
		vbus_ir_drop = PD_VBUS_IR_DROP_THRESHOLD;

	if (pd->pd_idx != idx || force_update) {
		if (pd->cap.max_mv[idx] > 5000)
			enable_vbus_ovp(false);
		else
			enable_vbus_ovp(true);

		charger_get_mivr(&oldmivr);
		mivr = pd->data.min_charger_voltage / 1000;
		pdc_set_mivr(pd->data.min_charger_voltage);

		charger_get_input_current(&oldmA);
		oldmA = oldmA / 1000;

		if (oldmA > pd->cap.ma[idx])
			charger_set_input_current(pd->cap.ma[idx] * 1000);

		ret = adapter_set_cap(pd->cap.max_mv[idx], pd->cap.ma[idx]);

		if (ret == ADAPTER_OK) {
			if (oldmA < pd->cap.ma[idx])
				charger_set_input_current(pd->cap.ma[idx]
								* 1000);

			/* MIVR = adapter voltage - cable IR drop */
			if ((pd->cap.max_mv[idx] - vbus_ir_drop) > mivr)
				mivr = pd->cap.max_mv[idx] - vbus_ir_drop;

			pdc_set_mivr(mivr * 1000);
		} else {
			if (oldmA > pd->cap.ma[idx])
				charger_set_input_current(oldmA * 1000);

			pdc_set_mivr(oldmivr);
		}

		pdc_get_idx(idx, &pd->pd_boost_idx, &pd->pd_buck_idx);
	}

	pr_info_ratelimited("[%s]idx:%d:%d:%d:%d vbus:%d cur:%d ret:%d ir_drop:%d r_cable:%d\n",
		__func__,
		pd->pd_idx, idx, pd->pd_boost_idx, pd->pd_buck_idx,
		pd->cap.max_mv[idx], pd->cap.ma[idx], ret,
		vbus_ir_drop, pd->r_cable);

	pd->pd_idx = idx;

	return ret;
}

void pdc_get_cap_max_watt(void)
{
	struct pd_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;

	if (pd->pd_cap_max_watt == -1) {
		for (i = 0; i < cap->nr; i++) {
			if (cap->min_mv[i] <= pd->vbus_h ||
				cap->max_mv[i] <= pd->vbus_h) {

				if (cap->maxwatt[i] > pd->pd_cap_max_watt) {
					pd->pd_cap_max_watt = cap->maxwatt[i];
					idx = i;
				}
				continue;
			}
		}
		chr_err("[%s]idx:%d vbus:%d %d maxwatt:%d\n", __func__,
			idx, cap->min_mv[idx], cap->max_mv[idx],
			pd->pd_cap_max_watt);
	}
}

int pdc_reset(void)
{
	if (pd == NULL || !pdc_is_ready())
		return -1;

	chr_err("%s: reset to default profile\n", __func__);
	pdc_init_table();
	pdc_get_reset_idx();
	pdc_setup(pd->pd_reset_idx);

	return 0;
}

int pdc_stop(void)
{
	/* Clear cable measurement so it re-runs on next attach */
	pd->cable_imp_checked = false;
	pd->r_cable = 0;
	pd->cable_imp_good = true;
	pdc_reset();

	return 0;
}

int pdc_get_setting(int *newvbus, int *newcur,
			int *newidx)
{
	int ret = 0;
	int idx, selected_idx;
	unsigned int pd_max_watt, pd_min_watt, now_max_watt;
	int ibus = 0, vbus;
	bool boost = false, buck = false;
	struct pd_cap *cap = NULL;
	unsigned int mivr1 = 0;
	bool chg1_mivr = false;

	pdc_init_table();
	pdc_get_reset_idx();
	pdc_get_cap_max_watt();

	cap = &pd->cap;

	if (cap->nr == 0)
		return -1;

	ret = charger_get_ibus(&ibus);
	if (ret < 0) {
		chr_err("[%s] get ibus fail, keep default voltage\n", __func__);
		return -1;
	}

	charger_get_mivr_state(&chg1_mivr);
	charger_get_mivr(&mivr1);

	vbus = battery_get_vbus();
	ibus = ibus / 1000;

	if ((chg1_mivr && (vbus < mivr1 / 1000 - 500)))
		goto reset;

	selected_idx = cap->selected_cap_idx;
	idx = selected_idx;

	if (idx < 0 || idx >= ADAPTER_CAP_MAX_NR)
		idx = selected_idx = 0;

	pd_max_watt = cap->max_mv[idx] * (cap->ma[idx]
			/ 100 * (100 - pd->data.ibus_err) - 100);
	now_max_watt = cap->max_mv[idx] * ibus;
	pd_min_watt = cap->max_mv[pd->pd_buck_idx] * cap->ma[pd->pd_buck_idx]
			/ 100 * (100 - pd->data.ibus_err)
			- pd->data.vsys_watt;

	if (pd_min_watt <= 5000000)
		pd_min_watt = 5000000;

	if ((now_max_watt >= pd_max_watt) || chg1_mivr) {
		*newidx = pd->pd_boost_idx;
		boost = true;
	} else if (now_max_watt <= pd_min_watt) {
		*newidx = pd->pd_buck_idx;
		buck = true;
	} else {
		*newidx = selected_idx;
		boost = false;
		buck = false;
	}

	*newvbus = cap->max_mv[*newidx];
	*newcur = cap->ma[*newidx];

	/*
	 * Cable impedance compensation: limit input current if cable
	 * resistance is high, so the adapter can actually deliver the
	 * requested voltage at the charger IC input.
	 * Note: For fixed PDO we cannot adjust the requested voltage,
	 * so cable IR drop is handled in pdc_setup() via MIVR adjustment.
	 */
	if (pd->cable_imp_checked) {
		if (pd->cable_imp_good) {
			/* Cable is good: allow full current */
		} else {
			/* Cable is bad: limit current based on resistance level */
			if (pd->r_cable < pd->data.pd_r_cable_2a_lower) {
				if (*newcur > 2000)
					*newcur = 2000;
			} else if (pd->r_cable < pd->data.pd_r_cable_1a_lower) {
				if (*newcur > 1500)
					*newcur = 1500;
			} else {
				if (*newcur > 1000)
					*newcur = 1000;
			}
			chr_err("%s: bad cable, limit cur to %d mA (r_cable=%d)\n",
				__func__, *newcur, pd->r_cable);
		}
	}

	pr_info_ratelimited("[%s]watt:%d,%d,%d vbus:%d:%d:%d cur:%d idx:%d\n",
		__func__, pd_max_watt, now_max_watt, pd_min_watt,
		pd->vbus_h, pd->vbus_l, *newvbus, *newcur, *newidx);

	return 0;

reset:
	pdc_reset();
	*newidx = pd->pd_reset_idx;
	*newvbus = cap->max_mv[*newidx];
	*newcur = cap->ma[*newidx];

	return 0;
}

int pdc_check_leave(void)
{
	struct pd_cap *cap;
	int ibus = 0, vbus = 0;
	unsigned int mivr1 = 0;
	bool mivr_state = false;
	int max_mv = 0;

	cap = &pd->cap;
	max_mv = cap->max_mv[pd->pd_idx];

	charger_get_ibus(&ibus);
	ibus = ibus / 1000;
	vbus = battery_get_vbus();
	charger_get_mivr_state(&mivr_state);
	charger_get_mivr(&mivr1);

	chr_err("[%s]mv:%d vbus:%d ibus:%d idx:%d min_watt:%d mivr:%d mivr_state:%d\n",
		__func__, max_mv, vbus, ibus, pd->pd_idx,
		PD_MIN_WATT, mivr1 / 1000, mivr_state);

	if (max_mv * ibus <= PD_MIN_WATT) {
		if (mivr_state)
			chr_err("[%s] MIVR occurred, ibus can't draw much higher current",
				__func__);
		goto leave;
	}

	return 0;

leave:
	pdc_stop();
	return 2;
}

int pdc_init(void)
{
	struct pdc *pdc = NULL;

	if (pd == NULL) {
		pdc = kzalloc(sizeof(struct pdc), GFP_KERNEL);
		if (pdc == NULL)
			return -ENOMEM;

		pd = pdc;

		pd->data.input_current_limit = 3000000;
		pd->data.charging_current_limit = 3000000;
		pd->data.battery_cv = 4350000;

		pd->data.min_charger_voltage = 4600000;
		pd->data.pd_vbus_low_bound = 5000000;
		pd->data.pd_vbus_upper_bound = 10000000;
		pd->data.ibus_err = 14;
		pd->data.vsys_watt = 5000000;

		/* cable impedance defaults (mOhm) */
		pd->data.cable_imp_threshold = 699;	/* 0x2bb */
		pd->data.vbat_cable_imp_threshold = 3900000; /* 0x3b8260 uV = 3.9V */
		pd->data.pd_r_cable_1a_lower = 553;	/* 0x229 */
		pd->data.pd_r_cable_2a_lower = 415;	/* 0x19f */

		pd->pdc_input_current_limit_setting = -1;
		pd->pdc_max_watt_setting = -1;
		pd->pd_cap_max_watt = -1;
		pd->pd_idx = -1;
		pd->pd_reset_idx = -1;
		pd->pd_boost_idx = 0;
		pd->pd_buck_idx = 0;
		pd->vbus_l = 5000;
		pd->vbus_h = 5000;
		pd->r_cable = 0;
		pd->cable_imp_checked = false;
		pd->cable_imp_good = true;

		return 0;
	}

	return 1;
}

struct pdc_data *pdc_get_data(void)
{
	return &pd->data;
}

int pdc_set_data(struct pdc_data data)
{
	pd->data.input_current_limit = data.input_current_limit;
	pd->data.charging_current_limit = data.charging_current_limit;
	pd->data.battery_cv = data.battery_cv;
	pd->data.min_charger_voltage = data.min_charger_voltage;
	pd->data.pd_vbus_low_bound = data.pd_vbus_low_bound;
	pd->data.pd_vbus_upper_bound = data.pd_vbus_upper_bound;
	pd->data.ibus_err = data.ibus_err;
	pd->data.vsys_watt = data.vsys_watt;
	pd->data.cable_imp_threshold = data.cable_imp_threshold;
	pd->data.vbat_cable_imp_threshold = data.vbat_cable_imp_threshold;
	pd->data.pd_r_cable_1a_lower = data.pd_r_cable_1a_lower;
	pd->data.pd_r_cable_2a_lower = data.pd_r_cable_2a_lower;

	chr_err("[%s]%d %d %d %d %d %d %d %d\n", __func__,
		pd->data.input_current_limit,
		pd->data.charging_current_limit,
		pd->data.battery_cv,
		pd->data.min_charger_voltage,
		pd->data.pd_vbus_low_bound,
		pd->data.pd_vbus_upper_bound,
		pd->data.ibus_err,
		pd->data.vsys_watt);

	pd->vbus_l = pd->data.pd_vbus_low_bound / 1000;
	pd->vbus_h = pd->data.pd_vbus_upper_bound / 1000;

	return 0;
}

int pdc_set_current(void)
{
	if (pd->pdc_input_current_limit_setting != -1 &&
	    pd->pdc_input_current_limit_setting <
	    pd->data.input_current_limit)
		pd->data.input_current_limit =
			pd->pdc_input_current_limit_setting;

	charger_set_input_current(pd->data.input_current_limit);
	charger_set_charging_current(pd->data.charging_current_limit);

	return 0;
}

int pdc_set_cv(void)
{
	charger_set_constant_voltage(pd->data.battery_cv);

	return 0;
}

int pdc_run(void)
{
	int ret = 0;
	int vbus = 0, cur = 0, idx = 0;

	pd->vbus_l = pd->data.pd_vbus_low_bound / 1000;
	pd->vbus_h = pd->data.pd_vbus_upper_bound / 1000;

	/* Measure cable impedance once, on first run after attach */
	pdc_check_cable_impedance();

	pdc_set_cv();

	ret = pdc_get_setting(&vbus, &cur, &idx);

	if (ret != -1 && idx != -1) {
		pd->pdc_input_current_limit_setting =  cur * 1000;
		pdc_set_current();
		pdc_setup(idx);
	}

	ret = pdc_check_leave();

	chr_err("[%s]vbus:%d input_cur:%d idx:%d current:%d ret:%d r_cable:%d\n",
			__func__, vbus, cur, idx,
			pd->data.input_current_limit, ret, pd->r_cable);

	return ret;
}
