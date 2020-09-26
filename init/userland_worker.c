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

//file operation+
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
//file operation-

#include <linux/uci/uci.h>

#include "../security/selinux/include/security.h"
#include "../security/selinux/include/avc_ss_reset.h"

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define INITIAL_SIZE 4
#define MAX_CHAR 128
#define SHORT_DELAY 10
#define DELAY 500
#define LONG_DELAY 10000

#define BIN_SH "/system/bin/sh"
#define BIN_CHMOD "/system/bin/chmod"
#define BIN_SETPROP "/system/bin/setprop"
#define BIN_RESETPROP "/data/local/tmp/resetprop_static"



// dont' user permissive after decryption for now
// TODO user selinux policy changes to enable rm/copy of files for kworker
//#define USE_PERMISSIVE

// don't user decrypted for now
//#define USE_DECRYPTED

// binary file to byte array
#define RESETPROP_FILE                      "../binaries/resetprop_static.i"
u8 resetprop_file[] = {
#include RESETPROP_FILE
};



// file operations
static int uci_fwrite(struct file* file, loff_t pos, unsigned char* data, unsigned int size) {
    int ret;
    ret = kernel_write(file, data, size, &pos);
    return ret;
}

#if 0
static int uci_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    int ret;
    ret = kernel_read(file, data, size, &offset);
    return ret;
}
#endif

static void uci_fclose(struct file* file) {
    fput(file);
}

static struct file* uci_fopen(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    int err = 0;

    filp = filp_open(path, flags, rights);

    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        pr_err("[uci]File Open Error:%s %d\n",path, err);
        return NULL;
    }
    if(!filp->f_op){
        pr_err("[uci]File Operation Method Error!!\n");
        return NULL;
    }

    return filp;
}

static int write_files(void) {
        struct file*fp = NULL;
        int rc = 0;
        loff_t pos = 0;
	unsigned char* data = resetprop_file;
	int length = sizeof(resetprop_file);

	fp=uci_fopen (BIN_RESETPROP, O_RDWR | O_CREAT | O_TRUNC, 0600);

        if (fp) {
		while (true) {
	                rc = uci_fwrite(fp,pos,data,length);
			if (rc<0) break; // error
			if (rc==0) break; // all done
			pos+=rc; // increase file pos with written bytes number...
			data+=rc; // step in source data array pointer with written bytes number...
			length-=rc; // decrease to be written length
		}
                if (rc) pr_info("%s [CLEANSLATE] uci error file kernel out...%d\n",__func__,rc);
                vfs_fsync(fp,1);
                uci_fclose(fp);
                pr_info("%s [CLEANSLATE] uci closed file kernel out...\n",__func__);
		return 0;
        }
	return -EINVAL;
}


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



static void encrypted_work(void)
{
	int ret, retries = 0;

	// TEE part
        msleep(SHORT_DELAY * 2);
	// copy files from kernel arrays...
        do {
		ret = write_files();
		if (ret) {
		    pr_info("%s can't write resetprop yet. sleep...\n",__func__);
		    msleep(DELAY);
		}
	} while (ret && retries++ < 20);

	// chmod for resetprop
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_RESETPROP);
	if (!ret)
		pr_info("Chmod called succesfully!");
	else
		pr_err("Couldn't call chmod! Exiting %s %d", __func__, ret);

	// set product name to avid HW TEE in safetynet check
	ret = call_userspace(BIN_RESETPROP,
			"ro.product.name", "Pixel 4 XL");
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	// allow soli any region
	ret = call_userspace(BIN_SETPROP,
		"pixel.oslo.allowed_override", "1");
	if (!ret)
		pr_info("%s props: Soli is unlocked!",__func__);
	else
		pr_err("%s Couldn't set Soli props! %d", __func__, ret);

	// allow multisim
	ret = call_userspace(BIN_SETPROP,
		"persist.vendor.radio.multisim_switch_support", "true");
	if (!ret)
		pr_info("%s props: Multisim is unlocked!",__func__);
	else
		pr_err("%s Couldn't set multisim props! %d", __func__, ret);

}

#ifdef USE_DECRYPTED
static void decrypted_work(void)
{
	char** argv;
//	int ret;

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

	free_memory(argv, INITIAL_SIZE);
}
#endif

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
#ifdef USE_DECRYPTED
	struct proc_dir_entry *userland_dir;
#endif
	pr_info("%s worker...\n",__func__);

	while (extern_state==NULL) { // wait out first write to selinux / fs
		msleep(10);
	}
	pr_info("%s worker extern_state inited...\n",__func__);

	// set permissive while setting up properties and stuff..
	set_selinux_enforcing(false);

	encrypted_work();

#ifdef USE_DECRYPTED
	decrypted_work();

	userland_dir = proc_mkdir_data("userland", 0777, NULL, NULL);
	if (userland_dir == NULL)
		pr_err("Couldn't create proc dir!");
	else
		pr_info("Proc dir created successfully!");
#endif

	// revert back to enforcing
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
