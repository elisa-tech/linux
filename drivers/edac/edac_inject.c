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

struct inject_edac_attribute {
	struct device_attribute attr;
	unsigned long offset;
	const char *default_msg;
};

#define to_inj_edac_attr(x) container_of(x, struct inject_edac_attribute, attr)

static ssize_t inject_edac_store_count(struct device *dev,
				      struct device_attribute *attr,
				      const char *data, size_t count)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	struct inj_err *val;
	int ret;
	u16 errcount;

	ret = kstrtou16(data, 10, &errcount);
	if (ret)
		return ret;
	if (!errcount)
		return -EINVAL;

	val = kzalloc(sizeof(struct inj_err), GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	val->type = ea->offset;
	val->count = errcount;
	val->info = pvt->info;
	if (!pvt->info.msg[0])
		strncpy(val->info.msg, ea->default_msg,
			EDAC_INJECT_MAX_MSG_SIZE);

	mutex_lock(&pvt->lock);
	list_add(&val->link, &pvt->errors);
	mutex_unlock(&pvt->lock);

	return count;
}

static ssize_t inject_edac_show_count(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	struct inj_err *val;
	unsigned int count = 0;

	mutex_lock(&pvt->lock);
	list_for_each_entry(val, &pvt->errors, link) {
		if (val->type == ea->offset)
			count += val->count;
	}
	mutex_unlock(&pvt->lock);

	return sprintf(buf, "%u\n", count);
}

static ssize_t inject_edac_store_ulong(struct device *dev,
				      struct device_attribute *attr,
				      const char *data, size_t count)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	unsigned long *val = (unsigned long *) (((char *) pvt) + ea->offset);
	int ret;

	ret = kstrtoul(data, 10, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t inject_edac_show_ulong(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	unsigned long *val = (unsigned long *) (((char *) pvt) + ea->offset);

	return sprintf(buf, "%lu\n", *val);
}

static ssize_t inject_edac_store_int(struct device *dev,
				    struct device_attribute *attr,
				    const char *data, size_t count)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	int *val = (int *) (((char *) pvt) + ea->offset);
	int ret;

	ret = kstrtoint(data, 10, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t inject_edac_show_int(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	int *val = (int *) (((char *) pvt) + ea->offset);

	return sprintf(buf, "%d\n", *val);
}

static ssize_t inject_edac_store_str(struct device *dev,
				    struct device_attribute *attr,
				    const char *data, size_t count)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	char *val = (((char *) pvt) + ea->offset);
	char *aux;

	aux = strstrip((char *) data);
	strscpy(val, aux, EDAC_INJECT_MAX_MSG_SIZE);

	return count;
}

static ssize_t inject_edac_show_str(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct inject_edac_attribute *ea = to_inj_edac_attr(attr);
	struct inj_pvt *pvt = to_mci(dev)->pvt_info;
	char *val = (((char *) pvt) + ea->offset);

	return sprintf(buf, "%s\n", val);
}

#define DEVICE_INJECT_EDAC_COUNT(_name, _member, _type, _defstr)\
	struct inject_edac_attribute inject_edac_attr_##_name =\
		{ __ATTR(_name, 0600, inject_edac_show_count,\
			 inject_edac_store_count),\
		  _type, _defstr }

#define DEVICE_INJECT_EDAC_ULONG(_name, _member)\
	struct inject_edac_attribute inject_edac_attr_##_name =\
		{ __ATTR(_name, 0600, inject_edac_show_ulong,\
			 inject_edac_store_ulong),\
		  offsetof(struct inj_pvt, info._member) }

#define DEVICE_INJECT_EDAC_INT(_name, _member)\
	struct inject_edac_attribute inject_edac_attr_##_name =\
		{ __ATTR(_name, 0600, inject_edac_show_int,\
			 inject_edac_store_int),\
		  offsetof(struct inj_pvt, info._member) }

#define DEVICE_INJECT_EDAC_STR(_name, _member)\
	struct inject_edac_attribute inject_edac_attr_##_name =\
		{ __ATTR(_name, 0600, inject_edac_show_str,\
			 inject_edac_store_str),\
		  offsetof(struct inj_pvt, info._member) }

static DEVICE_INJECT_EDAC_COUNT(inject_ce, correctable_errors,
			       HW_EVENT_ERR_CORRECTED,
			       "injected correctable errors");
static DEVICE_INJECT_EDAC_COUNT(inject_ue, uncorrectable_errors,
			       HW_EVENT_ERR_UNCORRECTED,
			       "injected uncorrectable errors");
static DEVICE_INJECT_EDAC_COUNT(inject_de, deferrable_errors,
			       HW_EVENT_ERR_DEFERRED,
			       "injected deferrable errors");
static DEVICE_INJECT_EDAC_COUNT(inject_fe, fatal_errors,
			       HW_EVENT_ERR_FATAL,
			       "injected fatal errors");
static DEVICE_INJECT_EDAC_COUNT(inject_ie, informative_errors,
			       HW_EVENT_ERR_INFO,
			       "injected informative errors");
static DEVICE_INJECT_EDAC_ULONG(inject_pfn, page_frame_number);
static DEVICE_INJECT_EDAC_ULONG(inject_oip, offset_in_page);
static DEVICE_INJECT_EDAC_ULONG(inject_syndrome, syndrome);
static DEVICE_INJECT_EDAC_INT(inject_top, top_layer);
static DEVICE_INJECT_EDAC_INT(inject_mid, mid_layer);
static DEVICE_INJECT_EDAC_INT(inject_low, low_layer);
static DEVICE_INJECT_EDAC_STR(inject_msg, msg);
static DEVICE_INJECT_EDAC_STR(inject_other_detail, other_detail);

static struct attribute *edac_inj_attrs[] = {
	&inject_edac_attr_inject_ce.attr.attr,
	&inject_edac_attr_inject_ue.attr.attr,
	&inject_edac_attr_inject_de.attr.attr,
	&inject_edac_attr_inject_fe.attr.attr,
	&inject_edac_attr_inject_ie.attr.attr,
	&inject_edac_attr_inject_pfn.attr.attr,
	&inject_edac_attr_inject_oip.attr.attr,
	&inject_edac_attr_inject_syndrome.attr.attr,
	&inject_edac_attr_inject_top.attr.attr,
	&inject_edac_attr_inject_mid.attr.attr,
	&inject_edac_attr_inject_low.attr.attr,
	&inject_edac_attr_inject_msg.attr.attr,
	&inject_edac_attr_inject_other_detail.attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(edac_inj);

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

	rc = edac_mc_add_mc_with_groups(mci, edac_inj_groups);
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
