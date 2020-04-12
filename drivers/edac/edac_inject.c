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
#include "edac_module.h"

static struct platform_device dummy_pdev;

#define edac_inj_printk(level, fmt, arg...) \
	edac_printk(level, "EDAC INJECT", fmt, ##arg)

static int __init edac_inject_init(void)
{
	struct edac_mc_layer layer;
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

	mci = edac_mc_alloc(0, 1, &layer, 0);
	if (!mci)
		return -ENOMEM;

	mci->pdev = &dummy_pdev.dev;

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
