#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>

#define BK_LEVEL_CNT 8
/*
backlight {
	compatible = "pwm-backlight";
	pwms = <&pwm1 0 5000000>;
	brightness-levels = <0 4 8 16 32 64 128 255>;
	default-brightness-level = <6>;
	status = "okay";
};
*/

static int __init dtsof_init(void) {
	int err = 0, size;
	struct device_node *nd;
	struct property *comppor;
	char *str;
	u32 *u32_arr;
	if ((u32_arr = kmalloc(BK_LEVEL_CNT * sizeof(u32), GFP_KERNEL)) == NULL) goto fail_kmalloc; // 动态分配堆区内存

/* 1. 找到backlight设备节点，通过路径查找 */
	nd = of_find_node_by_path("/backlight");
	if (nd == NULL) {
		err = -EINVAL;
		goto fail_findnd;
	}

/* 2.获取属性 */
	/* compatible */
	comppor = of_find_property(nd, "compatible", NULL);
	if (comppor == NULL) { // error
		err = -EINVAL;
		goto fail_findpro;
	}
	else { // get property success
		printk("compatible = %s\n", (char*)comppor->value);
	}

	/* status */
	err = of_property_read_string(nd, "status", (const char**)&str);
	if (err < 0) {
		goto fail_rs;
	}
	else {
		printk("status = %s\n", str);
	}

	/* brightness-levels长度 */
	size = of_property_count_elems_of_size(nd, "brightness-levels", sizeof(u32));
	if (size < 0) {
		err = size;
		goto fail_read_size;
	}
	else {
		printk("size of brightness-levels = %d\n", size);
	}
	
	/* brightness-levels */
	err = of_property_read_u32_array(nd, "brightness-levels", u32_arr, BK_LEVEL_CNT);
	if (err < 0) {
		goto fail_read_u32_arr;
	}
	else {
		int i;
		printk("brightness-levels = [ ");
		for (i = 0; i < BK_LEVEL_CNT; i++) {
			printk("%u ", u32_arr[i]);
		}
		printk("]\n");
	}
	
	kfree(u32_arr); // 释放内存

	return 0;

fail_kmalloc:
	return -EAGAIN;

fail_read_u32_arr:
fail_read_size:
fail_rs:
fail_findpro:
fail_findnd:
	kfree(u32_arr);
	return err;
}

static void __exit dtsof_exit(void) {
	return;
}


/*
 * 模块入口和出口
 */
module_init(dtsof_init);
module_exit(dtsof_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");