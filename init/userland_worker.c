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

static inline void set_selinux(int value)
{
	pr_info("Setting selinux state: %d", value);

	enforcing_set(extern_state, value);
	if (value)
		avc_ss_reset(extern_state->avc, 0);
	selnl_notify_setenforce(value);
	selinux_status_update_setenforce(extern_state, value);
	if (!value)
		call_lsm_notifier(LSM_POLICY_CHANGE, NULL);
}

static void encrypted_work(void)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "pixel.oslo.allowed_override");
	strcpy(argv[2], "1");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Props set succesfully! Soli is unlocked!");
	else
		pr_err("Couldn't set Soli props! %d", ret);

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "persist.vendor.radio.multisim_swtich_support");
	strcpy(argv[2], "true");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Props set succesfully! Multisim is unlocked!");
	else
		pr_err("Couldn't set multisim props! %d", ret);

	free_memory(argv, INITIAL_SIZE);
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

static void userland_worker(struct work_struct *work)
{
	struct proc_dir_entry *userland_dir;
	bool is_enforcing = false;

	pr_info("%s worker...\n",__func__);

	if (extern_state)
		is_enforcing = enforcing_enabled(extern_state);
	if (is_enforcing)
		set_selinux(0);

	encrypted_work();
	decrypted_work();

	userland_dir = proc_mkdir_data("userland", 0777, NULL, NULL);
	if (userland_dir == NULL)
		pr_err("Couldn't create proc dir!");
	else
		pr_info("Proc dir created successfully!");

	if (is_enforcing)
		set_selinux(1);
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
