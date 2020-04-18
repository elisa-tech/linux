// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform agnostic MC error injector
 *
 * Copyright (c) 2020 Intel Corporation
 *
 * Authors: Gabriele Paoloni <gabriele.paoloni@intel.com>
 *          Corey Minyard <cminyard@mvista.com>
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/stddef.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "edac_module.h"

static struct platform_device dummy_pdev;

#define edac_inj_printk(level, fmt, arg...) \
	edac_printk(level, "EDAC INJECT", fmt, ##arg)
#define EDAC_INJECT_MAX_MSG_SIZE 64

/**
 * Information about an error.
 */
struct inj_errinfo {
	unsigned long page_frame_number;
	unsigned long offset_in_page;
	unsigned long syndrome;
	int top_layer;
	int mid_layer;
	int low_layer;
	char msg[EDAC_INJECT_MAX_MSG_SIZE];
	char other_detail[EDAC_INJECT_MAX_MSG_SIZE];
};

/* A single error type. */
struct inj_err {
	struct list_head link;
	enum hw_event_mc_err_type type;
	u16 count;
	struct inj_errinfo info;
};

/**
 * mci private structure to store the errors
 */
struct inj_pvt {
	struct mutex lock;
	struct list_head errors;

	/* Information to put into the edac error report. */
	struct inj_errinfo info;
};

static void edac_inject_handle(struct mem_ctl_info *mci,
			      struct inj_err *err)
{
	edac_mc_handle_error(err->type, mci, err->count,
			     err->info.page_frame_number,
			     err->info.offset_in_page,
			     err->info.syndrome,
			     err->info.top_layer, err->info.mid_layer,
			     err->info.low_layer,
			     err->info.msg, err->info.other_detail);
	err->count = 0;
}

/**
 * inject_edac_check() - Calls the error checking subroutines
 * @mci: struct mem_ctl_info pointer
 */
static void inject_edac_check(struct mem_ctl_info *mci)
{
	struct inj_pvt *pvt = mci->pvt_info;
	struct inj_err *val, *val2;

	mutex_lock(&pvt->lock);
	list_for_each_entry_safe(val, val2, &pvt->errors, link) {
		edac_inject_handle(mci, val);
		list_del(&val->link);
		kfree(val);
	}
	mutex_unlock(&pvt->lock);
};

static int __init edac_inject_init(void)
{
	struct edac_mc_layer layer;
	struct inj_pvt *pvt;
	struct mem_ctl_info *mci;
	int rc;

	edac_inj_printk(KERN_INFO,
			"EDAC MC error inject module init\n");
	edac_inj_printk(KERN_INFO,
			"\t(c) 2020 Intel Corporation\n");

	/* Only POLL mode supported so far */
	edac_op_state = EDAC_OPSTATE_POLL;

	layer.type = EDAC_MC_LAYER_CHANNEL;
	layer.size = 1;
	layer.is_virt_csrow = false;

	mci = edac_mc_alloc(0, 1, &layer, sizeof(struct inj_pvt));
	if (!mci)
		return -ENOMEM;

	mci->pdev = &dummy_pdev.dev;
	pvt = mci->pvt_info;
	mutex_init(&pvt->lock);
	INIT_LIST_HEAD(&pvt->errors);

	/* Set the function pointer for periodic errors checks */
	mci->edac_check = inject_edac_check;

	rc = edac_mc_add_mc_with_groups(mci, NULL);
	if (rc) {
		edac_inj_printk(KERN_ERR,
				"EDAC INJECT: edac_mc_add_mc failed\n");
		edac_mc_free(mci);
	}

	return rc;
}

static void __exit edac_inject_exit(void)
{
	struct mem_ctl_info *mci = platform_get_drvdata(&dummy_pdev);

	edac_mc_del_mc(&dummy_pdev.dev);
	edac_mc_free(mci);
}

module_init(edac_inject_init);
module_exit(edac_inject_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Paoloni <gabriele.paoloni@intel.com>\n");
MODULE_AUTHOR("Corey Minyard <cminyard@mvista.com>\n");
MODULE_DESCRIPTION("EDAC MC error inject module");
