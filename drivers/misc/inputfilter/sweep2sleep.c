/*
* Copyright by flar2 (c) 2016-2018
* Copyright by Pal Zoltan Illes (c) 2019
* Licensed under GPL-v2 or above.
*/
#include <linux/module.h>
#include <linux/kernel.h>    
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>

#ifdef CONFIG_UCI
#include <linux/uci/uci.h>
#endif

#define DRIVER_AUTHOR "flar2 (asegaert at gmail.com)"
#define DRIVER_DESCRIPTION "sweep2sleep driver"
#define DRIVER_VERSION "4.0"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

//sweep2sleep
#define S2S_PWRKEY_DUR         60
#if 1
// 3040x1440
static int S2S_Y_MAX = 3040;
static int S2S_X_MAX = 1440;
static int S2S_X_LEFT_CORNER_END = 150;
static int S2S_X_RIGHT_CORNER_START = 1290; // 1440-150
#else
// 2280x1080
#define S2S_Y_MAX             	2280
#define S2S_X_LEFT_CORNER_END 90
#define S2S_X_RIGHT_CORNER_START 950
#endif

#define SWEEP_RIGHT		0x01
#define SWEEP_LEFT		0x02
#define VIB_STRENGTH		20

#define X_DIFF_THRESHOLD_0 70 // 200
#define X_DIFF_THRESHOLD_1 70 // 180

// 1=sweep right, 2=sweep left, 3=both
static int s2s_switch = 0;
static int s2s_filter_mode = 0; // 0 input filter NO, 1 YES RIGHT HANDED MODE, 2 YES LEFT HANDED MODE
static int s2s_doubletap_mode = 0; // 0 - off, 1 - powerOff, 2 - signal thru UCI
static int s2s_height = 130;
static int s2s_doubletap_height = 70; // where doubletap Y coordinates are registered
static int s2s_height_above = 20;
static int s2s_width = 70;
static int s2s_from_corner = 0;
static int touch_x = 0, touch_y = 0, firstx = 0;
static bool touch_x_called = false, touch_y_called = false, touch_down_called = false;
static bool scr_on_touch = false, barrier[2] = {false, false};
static bool exec_count = true;
static struct input_dev * sweep2sleep_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *s2s_input_wq;
static struct work_struct s2s_input_work;
//extern void set_vibrate(int value);
extern void set_vibrate_2(int value, int boost_power);
static int vib_strength = VIB_STRENGTH;
static bool first_event = false;
static bool setup_done = false;

static bool filter_coords_status = false;

extern char* init_get_saved_command_line(void);

#ifdef CONFIG_UCI
static int get_s2s_switch(void) {
	return uci_get_user_property_int_mm("sweep2sleep_mode", s2s_switch, 0, 3);
}
static int get_s2s_filter_mode(void) {
	return uci_get_user_property_int_mm("sweep2sleep_filter_mode", s2s_filter_mode, 0, 2);
}
static int get_s2s_doubletap_mode(void) {
	return uci_get_user_property_int_mm("sweep2sleep_doubletap_mode", s2s_doubletap_mode, 0, 2);
}
static int get_s2s_height(void) {
	return uci_get_user_property_int_mm("sweep2sleep_height", s2s_height, 50, 350);
}
static int get_s2s_doubletap_height(void) {
	return uci_get_user_property_int_mm("sweep2sleep_doubletap_height", s2s_doubletap_height, 50, 350);
}
static int get_s2s_height_above(void) {
	return uci_get_user_property_int_mm("sweep2sleep_height_above", s2s_height_above, 0, 150);
}
static int get_s2s_width(void) {
	return uci_get_user_property_int_mm("sweep2sleep_width", s2s_width, 0, 150);
}
static int get_s2s_width_cutoff(void) {
	return uci_get_user_property_int_mm("sweep2sleep_width_cutoff", 60, 0, 80);
}
static int get_s2s_corner_width(void) {
	return uci_get_user_property_int_mm("sweep2sleep_corner_width", 150, 100, 250);
}
static int get_s2s_from_corner(void) {
	return uci_get_user_property_int_mm("sweep2sleep_from_corner", s2s_from_corner, 0, 1);
}
static int get_s2s_continuous_vib(void) {
	return uci_get_user_property_int_mm("sweep2sleep_continuous_vib", 0, 0, 1);
}
static int get_s2s_y_limit(void) {
	return S2S_Y_MAX - get_s2s_height();
}
static int get_s2s_y_limit_doubletap(void) {
	return S2S_Y_MAX - get_s2s_doubletap_height();
}
static int get_s2s_y_above(void) {
	return S2S_Y_MAX - get_s2s_height_above();
}
#endif

// device specifics
static void s2s_setup_values() {
	if ( strstr(init_get_saved_command_line(),"hardware=coral") ) {
		// CORAL 3040x1440
		pr_info("%s hw coral\n",__func__);
		// leave original values
	} else {
		pr_info("%s hw flame\n",__func__);
		// FLAME 2280x1080
		S2S_Y_MAX = 2280;
		S2S_X_MAX = 1080;
		S2S_X_LEFT_CORNER_END = 100;
		S2S_X_RIGHT_CORNER_START = 1080-100;
	}
}


/* PowerKey work func */
static void sweep2sleep_presspwr(struct work_struct * sweep2sleep_presspwr_work) {

	if (!mutex_trylock(&pwrkeyworklock))
                return;
	input_event(sweep2sleep_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(sweep2sleep_pwrdev, EV_SYN, 0, 0);
	msleep(S2S_PWRKEY_DUR);
	input_event(sweep2sleep_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(sweep2sleep_pwrdev, EV_SYN, 0, 0);
	msleep(S2S_PWRKEY_DUR);
        mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(sweep2sleep_presspwr_work, sweep2sleep_presspwr);

static int vib_power = 50;
static void sweep2sleep_vib(struct work_struct * sweep2sleep_vib_work) {
	set_vibrate_2(vib_strength-10,vib_power);
	return;
}
static DECLARE_WORK(sweep2sleep_vib_work, sweep2sleep_vib);

/* PowerKey trigger */
static void sweep2sleep_pwrtrigger(void) {
	set_vibrate_2(vib_strength,100);
	schedule_work(&sweep2sleep_presspwr_work);
        return;
}

/* reset on finger release */
static void sweep2sleep_reset(void) {
	exec_count = true;
	barrier[0] = false;
	barrier[1] = false;
	firstx = 0;
	first_event = false;
	scr_on_touch = false;
	filter_coords_status = false;
}


/* Sweep2sleep main function */
static void detect_sweep2sleep(int x, int y, bool st)
{
        int prevx = 0, nextx = 0;
#ifdef CONFIG_UCI
	int s2s_y_limit = get_s2s_y_limit();
	int s2s_y_above = get_s2s_y_above();
#else
	int s2s_y_limit = S2S_Y_MAX - s2s_height;
	int s2s_y_above = S2S_Y_MAX - s2s_height_above;
#endif
	int x_threshold_0 = X_DIFF_THRESHOLD_0 + get_s2s_width();
	int x_threshold_1 = X_DIFF_THRESHOLD_1 + get_s2s_width();
        bool single_touch = st;

	if (!setup_done) {
		setup_done = true;
		s2s_setup_values();
	}

	if (firstx == 0) {
		firstx = x;
		first_event = true;
	}

	if (get_s2s_switch() > 3)
		s2s_switch = 3;

	//left->right
	if (single_touch && ((firstx < (S2S_X_RIGHT_CORNER_START-40) && firstx < (S2S_X_MAX/2) && !get_s2s_from_corner()) || ((firstx > get_s2s_width_cutoff()) && firstx < get_s2s_corner_width())) && (get_s2s_switch() & SWEEP_RIGHT)) {
		scr_on_touch=true;
		prevx = firstx;
		nextx = prevx + x_threshold_1;
		if ((barrier[0] == true) ||
		   ((x > prevx) &&
		    (x < nextx) &&
		    (y > s2s_y_limit && y < s2s_y_above))) {
			if (first_event || get_s2s_continuous_vib()) { // signal gesture start with vib, or continuously
				if (exec_count) {
					if (barrier[1] == true) { vib_power = 50; } else { vib_power = 1; }
					schedule_work(&sweep2sleep_vib_work);
				}
				first_event = false;
			}
			prevx = nextx;
			nextx += x_threshold_0;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x > prevx) &&
			    (x < nextx) &&
			    (y > s2s_y_limit && y < s2s_y_above))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x > prevx) &&
				    (y > s2s_y_limit && y < s2s_y_above)) {
					if (x > (nextx + x_threshold_1)) {
						if (exec_count) {
							sweep2sleep_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	//right->left
	} else if (((firstx >= (S2S_X_LEFT_CORNER_END-40) && firstx > (S2S_X_MAX/2) && !get_s2s_from_corner()) || (firstx >= S2S_X_MAX - get_s2s_corner_width() && (firstx < S2S_X_MAX - get_s2s_width_cutoff()))) && (get_s2s_switch() & SWEEP_LEFT)) {
		scr_on_touch=true;
		prevx = firstx;
		nextx = prevx - x_threshold_1;
		if ((barrier[0] == true) ||
		   ((x < prevx) &&
		    (x > nextx) &&
		    (y > s2s_y_limit && y < s2s_y_above))) {
			if (first_event || get_s2s_continuous_vib()) { // signal gesture start with vib, or continuously
				if (exec_count) {
					if (barrier[1] == true) { vib_power = 50; } else { vib_power = 1; }
					schedule_work(&sweep2sleep_vib_work);
				}
				first_event = false;
			}
			prevx = nextx;
			nextx -= x_threshold_0;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x < prevx) &&
			    (x > nextx) &&
			    (y > s2s_y_limit && y < s2s_y_above))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x < prevx) &&
				    (y > s2s_y_limit && y < s2s_y_above)) {
					if (x < (nextx - x_threshold_1)) {
						if (exec_count) {
							sweep2sleep_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	}
}


static void s2s_input_callback(struct work_struct *unused) {

	detect_sweep2sleep(touch_x, touch_y, true);

	return;
}

static int last_tap_coord_x = 0;
static int last_tap_coord_y = 0;
static unsigned long last_tap_jiffies = 0;

static bool s2s_input_filter(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
	bool first_touch_detection = false;

	if (type == EV_KEY && code == BTN_TOUCH && value == 1) {
		touch_down_called = true;
		touch_x_called = false;
		touch_y_called = false;
		sweep2sleep_reset();
		return false;
	}

	if (type == EV_KEY && code == BTN_TOUCH && value == 0) {
		touch_down_called = false;
		touch_x_called = false;
		touch_y_called = false;
		sweep2sleep_reset();
		return false;
	}

	if (code == ABS_MT_SLOT) {
		touch_x_called = false;
		touch_y_called = false;
		sweep2sleep_reset();
		return false;
	}

	if (code == ABS_MT_TRACKING_ID && value == -1) {
		touch_down_called = false;
		touch_x_called = false;
		touch_y_called = false;
		sweep2sleep_reset();
		return false;
	}

	if (code == ABS_MT_POSITION_X && touch_down_called) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y && touch_down_called) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called && touch_y_called && touch_down_called) {
		int s2s_y_limit = get_s2s_y_limit();
		int s2s_y_above = get_s2s_y_above();
		touch_x_called = false;
		touch_y_called = false;
		if (touch_y > s2s_y_above || touch_y < s2s_y_limit || (touch_x < get_s2s_width_cutoff()) || (touch_x > S2S_X_MAX - get_s2s_width_cutoff()) || (get_s2s_filter_mode() == 1 && (touch_x < (S2S_X_MAX * 0.60))) || (get_s2s_filter_mode() == 2 && (touch_x > (S2S_X_MAX * 0.40)))) { // TODO left to right mode?
			touch_down_called = false;
			sweep2sleep_reset();
		} else {
			// in touch area...
			if (get_s2s_filter_mode()>0 && !filter_coords_status) { // filtered input mode, and first touch point registered without lifting finger...
				first_touch_detection = true; // this is the firt touch so far without lifting finger...
				if (touch_y > get_s2s_y_limit_doubletap()) // only check doubletaps if Y is at the right part
				if (get_s2s_doubletap_mode()>0) {
					unsigned int last_tap_time_diff = jiffies - last_tap_jiffies;
					int delta_x = last_tap_coord_x - touch_x;
					int delta_y = last_tap_coord_y - touch_y;
					//pr_info("%d doubletap check, Time: %u X: %d Y: %d\n",last_tap_time_diff,delta_x,delta_y);
					if (last_tap_time_diff < 150) { // previous first touch time and coordinate comparision to detect double tap...
						if (delta_x < 60 && delta_x > -60 && delta_y < 60 && delta_y > -60) {
							touch_down_called = false;
							sweep2sleep_reset();
							if (get_s2s_doubletap_mode()==1) { // power button mode
								sweep2sleep_pwrtrigger();
							} else { // mode 2
								vib_power = 60;
								schedule_work(&sweep2sleep_vib_work);
								write_uci_out("fp_touch");
							}
							last_tap_coord_x = 0;
							last_tap_coord_y = 0;
							last_tap_jiffies = 0;
							return false; // break out here, don't filter
						}
					}
					last_tap_coord_x = touch_x;
					last_tap_coord_y = touch_y;
					last_tap_jiffies = jiffies;
				}
			}
			// in touch area, set filter status True...
			filter_coords_status = true;

			queue_work_on(0, s2s_input_wq, &s2s_input_work);
		}
	}
	// filter if filter mode active and in sweep touch area, and...
	// ...this is not right the first touch detection and so the Y coordinate 
	//    which should NOT be filtered, or it will cause touch positioning issues...
	return get_s2s_switch() && get_s2s_filter_mode() && filter_coords_status && !first_touch_detection; 
}

static void s2s_input_event(struct input_handle *handle, unsigned int type,
                                unsigned int code, int value) {
}


static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "synaptics,s3320")) {
		return 0;
	} else
	if (strstr(dev->name, "fts")) {
		return 0;
	} else
	if (strstr(dev->name, "sec_touchscreen")) {
		return 0;
	} else {
		return 1;
	}
}

static int s2s_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "s2s";

	error = input_register_handle(handle);

	error = input_open_device(handle);

	return 0;

}

static void s2s_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id s2s_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler s2s_input_handler = {
	.filter         = s2s_input_filter,
	.event		= s2s_input_event,
	.connect	= s2s_input_connect,
	.disconnect	= s2s_input_disconnect,
	.name		= "s2s_inputreq",
	.id_table	= s2s_ids,
};

static ssize_t sweep2sleep_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", s2s_switch);
}

static ssize_t sweep2sleep_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return ret;

	if (input < 0 || input > 3)
		input = 0;

	s2s_switch = input;
	
	return count;
}

static DEVICE_ATTR(sweep2sleep, (S_IWUSR|S_IRUGO),
	sweep2sleep_show, sweep2sleep_dump);

static ssize_t vib_strength_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", vib_strength);
}

static ssize_t vib_strength_dump(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return ret;

	if (input < 0 || input > 90)
		input = 20;

	vib_strength = input;
	
	return count;
}

static DEVICE_ATTR(vib_strength, (S_IWUSR|S_IRUGO),
	vib_strength_show, vib_strength_dump);

static struct kobject *sweep2sleep_kobj;

static int __init sweep2sleep_init(void)
{
	int rc = 0;

	sweep2sleep_pwrdev = input_allocate_device();
	if (!sweep2sleep_pwrdev) {
		pr_err("Failed to allocate sweep2sleep_pwrdev\n");
		goto err_alloc_dev;
	}

	input_set_capability(sweep2sleep_pwrdev, EV_KEY, KEY_POWER);

	sweep2sleep_pwrdev->name = "s2s_pwrkey";
	sweep2sleep_pwrdev->phys = "s2s_pwrkey/input0";

	rc = input_register_device(sweep2sleep_pwrdev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

	s2s_input_wq = create_workqueue("s2s_iwq");
	if (!s2s_input_wq) {
		pr_err("%s: Failed to create workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&s2s_input_work, s2s_input_callback);

	rc = input_register_handler(&s2s_input_handler);
	if (rc)
		pr_err("%s: Failed to register s2s_input_handler\n", __func__);

	sweep2sleep_kobj = kobject_create_and_add("sweep2sleep", NULL) ;
	if (sweep2sleep_kobj == NULL) {
		pr_warn("%s: sweep2sleep_kobj failed\n", __func__);
	}

	rc = sysfs_create_file(sweep2sleep_kobj, &dev_attr_sweep2sleep.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for sweep2sleep\n", __func__);

	rc = sysfs_create_file(sweep2sleep_kobj, &dev_attr_vib_strength.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for vib_strength\n", __func__);

err_input_dev:
	input_free_device(sweep2sleep_pwrdev);

err_alloc_dev:
	pr_info("%s done\n", __func__);
	return 0;
}

static void __exit sweep2sleep_exit(void)
{
	kobject_del(sweep2sleep_kobj);
	input_unregister_handler(&s2s_input_handler);
	destroy_workqueue(s2s_input_wq);
	input_unregister_device(sweep2sleep_pwrdev);
	input_free_device(sweep2sleep_pwrdev);

	return;
}

module_init(sweep2sleep_init);
module_exit(sweep2sleep_exit);
