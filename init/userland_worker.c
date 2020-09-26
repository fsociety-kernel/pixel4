// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>.
 */

#define pr_fmt(fmt) "userland_worker: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/userland.h>

#include <linux/uci/uci.h>

#include "../security/selinux/include/security.h"
#include "../security/selinux/include/avc_ss_reset.h"

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define INITIAL_SIZE 4
#define MAX_CHAR 128
#define DELAY 500

static struct delayed_work userland_work;

static const struct file_operations proc_file_fops = {
	.owner = THIS_MODULE,
};

static void free_memory(char** argv, int size)
{
	int i;

	for (i = 0; i < size; i++)
		kfree(argv[i]);
	kfree(argv);
}

static char** alloc_memory(int size)
{
	char** argv;
	int i;

	argv = kmalloc(size * sizeof(char*), GFP_KERNEL);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return NULL;
	}

	for (i = 0; i < size; i++) {
		argv[i] = kmalloc(MAX_CHAR * sizeof(char), GFP_KERNEL);
		if (!argv[i]) {
			pr_err("Couldn't allocate memory!");
			kfree(argv);
			return NULL;
		}
	}

	return argv;
}

static int use_userspace(char** argv)
{
	static char* envp[] = {
		"SHELL=/bin/sh",
		"HOME=/",
		"USER=shell",
		"TERM=xterm-256color",
		"PATH=/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:/system_ext/bin:/system/bin:/system/xbin:/odm/bin:/vendor/bin:/vendor/xbin",
		"DISPLAY=:0",
		NULL
	};

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}
static int call_userspace(char *binary, char *param0, char *param1) {
	char** argv;
	int ret;
	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return -ENOMEM;
	}
	strcpy(argv[0], binary);
	strcpy(argv[1], param0);
	strcpy(argv[2], param1);
	argv[3] = NULL;
	ret = use_userspace(argv);
	free_memory(argv, INITIAL_SIZE);
	return ret;
}

static inline void set_selinux(int value)
{
	pr_info("%s Setting selinux state: %d", __func__, value);

	enforcing_set(get_extern_state(), value);
	if (value)
		avc_ss_reset(get_extern_state()->avc, 0);
	selnl_notify_setenforce(value);
	selinux_status_update_setenforce(get_extern_state(), value);
	if (!value)
		call_lsm_notifier(LSM_POLICY_CHANGE, NULL);
}

static bool on_boot_selinux_mode_read = false;
static bool on_boot_selinux_mode = false;
DEFINE_MUTEX(enforce_mutex);

static void set_selinux_enforcing(bool enforcing) {
	bool is_enforcing = false;
	mutex_lock(&enforce_mutex);
	while (get_extern_state()==NULL) {
		msleep(10);
	}

	is_enforcing = enforcing_enabled(get_extern_state());

	if (!on_boot_selinux_mode_read) {
		on_boot_selinux_mode_read = true;
		on_boot_selinux_mode = is_enforcing;
	}

	// nothing to do?
	if (enforcing == is_enforcing) goto exit;

	// change to permissive?
	if (is_enforcing && !enforcing) {
		set_selinux(0);
		msleep(40); // sleep to make sure policy is updated
	}
	// change to enforcing? only if on-boot it was enforcing
	if (!is_enforcing && enforcing && on_boot_selinux_mode)
		set_selinux(1);
exit:
	mutex_unlock(&enforce_mutex);
}

#define BIN_SH "/system/bin/sh"
#define BIN_SETPROP "/system/bin/setprop"
#define BIN_RESETPROP "/data/local/tmp/resetprop_static"

static void encrypted_work(void)
{
	int ret;

	ret = call_userspace(BIN_SETPROP,
		"pixel.oslo.allowed_override", "1");
	if (!ret)
		pr_info("%s props: Soli is unlocked!",__func__);
	else
		pr_err("%s Couldn't set Soli props! %d", __func__, ret);

	ret = call_userspace(BIN_SETPROP,
		"persist.vendor.radio.multisim_switch_support", "true");
	if (!ret)
		pr_info("%s props: Multisim is unlocked!",__func__);
	else
		pr_err("%s Couldn't set multisim props! %d", __func__, ret);

	ret = call_userspace(BIN_RESETPROP,
		"ro.product.name", "Pixel 4 XL");
	if (!ret)
		pr_info("%s props: product.name resetprops set succesfully!",__func__);
	else
		pr_err("%s Couldn't set product.name props! %d", __func__, ret);
}

static void decrypted_work(void)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	if (!is_decrypted) {
		pr_info("Waiting for fs decryption!");
		while (!is_decrypted)
			msleep(1000);
		msleep(10000);
		pr_info("Fs decrypted!");
	}

	// Wait for RCU grace period to end for the files to sync
	rcu_barrier();
	msleep(100);

#if 0
	strcpy(argv[0], "/system/bin/cp");
	strcpy(argv[1], "/storage/emulated/0/resetprop_static");
	strcpy(argv[2], "/data/local/tmp/resetprop_static");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret) {
		pr_info("Copy called succesfully!");

		strcpy(argv[0], "/system/bin/chmod");
		strcpy(argv[1], "755");
		strcpy(argv[2], "/data/local/tmp/resetprop_static");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret) {
			pr_info("Chmod called succesfully!");
		} else {
			pr_err("Couldn't call chmod! Exiting %s %d", __func__, ret);
		}
	} else {
		pr_err("Couldn't copy file! %s %d", __func__, ret);
	}
#endif

	free_memory(argv, INITIAL_SIZE);
}

#define USE_PERMISSIVE

static void setup_kadaway(bool on) {
	int ret;
#ifdef USE_PERMISSIVE
	set_selinux_enforcing(false);
#endif
	if (!on) {
		ret = call_userspace(BIN_SH,
			"-c", "/system/bin/rm /data/local/tmp/hosts_k");
                if (!ret)
                        pr_info("%s userland: rm hosts file",__func__);
                else
                        pr_err("%s userland: COULDN'T rm hosts file",__func__);
	} else{
		ret = call_userspace(BIN_SH,
			"-c", "/system/bin/cp /storage/emulated/0/hosts_k /data/local/tmp/hosts_k");
                if (!ret)
                        pr_info("%s userland: cp hosts file",__func__);
                else
                        pr_err("%s userland: COULDN'T copy hosts file",__func__);
	}
#ifdef USE_PERMISSIVE
	set_selinux_enforcing(true);
#endif
}

static bool kadaway = true;
static void uci_user_listener(void) {
	bool new_kadaway = !!uci_get_user_property_int_mm("kadaway", kadaway, 0, 1);
	if (new_kadaway!=kadaway) {
		pr_info("%s kadaway %u\n",__func__,new_kadaway);
		kadaway = new_kadaway;
		setup_kadaway(kadaway);
	}
}


static void userland_worker(struct work_struct *work)
{
	struct proc_dir_entry *userland_dir;
	pr_info("%s worker...\n",__func__);
#if 1
	while (extern_state==NULL) { // wait out first write to selinux / fs
		msleep(10);
	}
	msleep(3000);
	pr_info("%s worker extern_state inited...\n",__func__);
#endif

	set_selinux_enforcing(false);

	encrypted_work();
	decrypted_work();

	userland_dir = proc_mkdir_data("userland", 0777, NULL, NULL);
	if (userland_dir == NULL)
		pr_err("Couldn't create proc dir!");
	else
		pr_info("Proc dir created successfully!");

	set_selinux_enforcing(true);

	uci_add_user_listener(uci_user_listener);

}

static int __init userland_worker_entry(void)
{
	INIT_DELAYED_WORK(&userland_work, userland_worker);
	pr_info("%s boot init\n",__func__);
	queue_delayed_work(system_power_efficient_wq,
			&userland_work, DELAY);


	return 0;
}

module_init(userland_worker_entry);
