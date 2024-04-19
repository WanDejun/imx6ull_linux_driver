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

#define DTSLED_NAME "dtsLED" // 设备名

static const struct file_operations dtsLED_dev_fops; // 驱动接口结构体

typedef enum { LED_OFF = 0, LED_ON = 1 } LED_status_t;

typedef struct {
	u64* IMX6U_CCM_CCGR1;
	u64* IMX6U_SW_MUX_GPIO1_IO03;
	u64* IMX6U_SW_PAD_GPIO1_IO03;
	u64* IMX6U_GPIO1_DR;
	u64* IMX6U_GPIO1_GDIR;
} dtsLED_reg_t; // 寄存器表

typedef struct {
	dev_t 	devid; /*设备id */
	int 	major; /* 主设备号 */
	int 	minor; /* 次设备号 */
	struct cdev cdev; // 字符设备结构体
	struct class *class; // 类结构体
	struct device *device; // 设备结构体，与类结构体用于mdev注册设备节点
	struct device_node *nd; // 寄存器结构体
	u32 reg_cnt; // 设备树寄存器属性数量
	dtsLED_reg_t reg; //
} dtsLED_dev_t;

dtsLED_dev_t dtsLED_dev;

/**
 * @function: set LED status
 * @param: LED_status_t NewStatus
 * @return: void
 * @author: WanDejun
 */
static void LED_switch(LED_status_t NewStatus) {
	u32 val;
	
	val = readl(dtsLED_dev.reg.IMX6U_GPIO1_DR);
	if (NewStatus == LED_OFF) val |= (1 << 3);
	else val &= ~(1 << 3);
	writel(val, dtsLED_dev.reg.IMX6U_GPIO1_DR);
}

static int __init dtsLED_init(void) {
	int err = 0; // 状态返回值
	u32 val;
	char *str;
	u32 *reg_arr; // 寄存器数组
	
	/* 
	 * 注册设备号 
	 */
	dtsLED_dev.major = 0;
	if (dtsLED_dev.major != 0) { // 手动分配设备号
		dtsLED_dev.devid = MKDEV(dtsLED_dev.major, 0); // 主次设备号
		err = register_chrdev_region(dtsLED_dev.devid, 1, DTSLED_NAME); // 注册设备号
	}
	else { // 由操作系统分配设备号
		err = alloc_chrdev_region(&dtsLED_dev.devid, 0, 1, DTSLED_NAME);
		dtsLED_dev.major = MAJOR(dtsLED_dev.devid); //读取主设备号
		dtsLED_dev.minor = MINOR(dtsLED_dev.devid); //读取次设备号
	}

	if (err < 0) goto faild_devid; // 异常处理


	/* 
	 * 添加字符设备
	 */
	dtsLED_dev.cdev.owner = THIS_MODULE; // 设置字符设备所有模块为本模块
	cdev_init(&dtsLED_dev.cdev, &dtsLED_dev_fops); // 初始化字符设备
	err = cdev_add(&dtsLED_dev.cdev, dtsLED_dev.devid, 1); // 添加字符设备

	if (err < 0) {// 异常处理
		goto faild_cdev;
	}

	/* 
	 * 创建类 
	 */
	dtsLED_dev.class = class_create(THIS_MODULE, DTSLED_NAME);
	err = IS_ERR(dtsLED_dev.class); // 判断异常
	if (err < 0) { // 异常处理
		goto faild_class;
	}

	/*
	 * 创建类设备
	 */
	dtsLED_dev.device = device_create(dtsLED_dev.class, NULL, dtsLED_dev.devid, NULL, DTSLED_NAME);
	err = IS_ERR(dtsLED_dev.device); // 判断异常
	if (err < 0) { // 异常处理
		goto faild_device;
	}


	/*
	 * 读取设备树信息
	 */
	/* 查找节点 */
	dtsLED_dev.nd = of_find_node_by_path("/alientek/led1"); // 通过绝对路径查找节点
	if (dtsLED_dev.nd == NULL) { // 异常处理
		err = -EINVAL;
		goto fail_findnd;
	}

	/* 获取状态信息 */
	err = of_property_read_string(dtsLED_dev.nd, "status", (const char**)&str); // 读取status字符串
	if (err < 0) { // 异常处理
		goto fail_rs;
	}
	else {
		printk("status=%s\n", str); // 输出日志
	}

	err = of_property_read_string(dtsLED_dev.nd, "compatible", (const char**)&str); // 读取compatible字符串
	if (err < 0) {// 异常处理
		goto fail_rs;
	}
	else {
		printk("compatible=%s\n", str); // 输出日志
	}

	/* 获取reg长度 */
	dtsLED_dev.reg_cnt = of_property_count_elems_of_size(dtsLED_dev.nd, "reg", sizeof(u32)); // 读取集合长度
	if (dtsLED_dev.reg_cnt < 0) { // 异常处理
		err = dtsLED_dev.reg_cnt;
		goto fail_read_size;
	}
	else {
		printk("size of reg = %d\n", dtsLED_dev.reg_cnt); // 输出日志
	}

	/* 分配reg数组 */
	if ((dtsLED_dev.reg_arr = kmalloc(dtsLED_dev.reg_cnt * sizeof(u32), GFP_KERNEL)) == NULL) goto fail_kmalloc; // 动态分配堆区内存
	
	/* 读取reg */
	err = of_property_read_u32_array(dtsLED_dev.nd, "reg", dtsLED_dev.reg_arr, dtsLED_dev.reg_cnt); // 读取u32数组
	if (err < 0) { // 异常处理
		goto fail_read_reg_arr;
	}
	else { // 输出日志
		int i;
		printk("reg = [ "); 
		for (i = 0; i < dtsLED_dev.reg_cnt; i++) {
			printk("%u ", dtsLED_dev.reg_arr[i]);
		}
		printk("]\n");
	}


	/* 
	 * 内存映射 
	 */
	dtsLED_dev.reg.IMX6U_CCM_CCGR1 = 			ioremap(dtsLED_dev.reg_arr[0], dtsLED_dev.reg_arr[1]);
	dtsLED_dev.reg.IMX6U_SW_MUX_GPIO1_IO03 = 	ioremap(dtsLED_dev.reg_arr[2], dtsLED_dev.reg_arr[3]);
	dtsLED_dev.reg.IMX6U_SW_PAD_GPIO1_IO03 = 	ioremap(dtsLED_dev.reg_arr[4], dtsLED_dev.reg_arr[5]);
	dtsLED_dev.reg.IMX6U_GPIO1_DR = 			ioremap(dtsLED_dev.reg_arr[6], dtsLED_dev.reg_arr[7]);
	dtsLED_dev.reg.IMX6U_GPIO1_GDIR = 			ioremap(dtsLED_dev.reg_arr[8], dtsLED_dev.reg_arr[9]);

	/*
	 * 初始化IO
	 */
	val = readl(dtsLED_dev.reg.IMX6U_CCM_CCGR1); // 开启时钟
	val |= (3 << 26); 
	writel(val, dtsLED_dev.reg.IMX6U_CCM_CCGR1);

	writel(0x5, dtsLED_dev.reg.IMX6U_SW_MUX_GPIO1_IO03); // 初始化IO属性
	writel(0x10b0, dtsLED_dev.reg.IMX6U_SW_PAD_GPIO1_IO03);

	val = readl(dtsLED_dev.reg.IMX6U_GPIO1_GDIR); // 初始化GPIO
	val |= 1 << 3;
	writel(val, dtsLED_dev.reg.IMX6U_GPIO1_GDIR);

	LED_switch(LED_OFF); // 默认关灯 


	/*
	 * 释放内存
	 */
	kfree(reg_arr);

	return 0;


/*
 * 错误处理
 */
fail_read_reg_arr:
	kfree(reg_arr);
fail_kmalloc:
fail_read_size:
fail_rs:
fail_findnd:
	device_destroy(dtsLED_dev.class, dtsLED_dev.devid);
faild_device:
	class_destroy(dtsLED_dev.class);
faild_class:
	cdev_del(&dtsLED_dev.cdev);
faild_cdev:
	unregister_chrdev_region(dtsLED_dev.devid, 1);
faild_devid:
	return err;
}

static void __exit dtsLED_exit(void) {
	/* 关灯 */
	LED_switch(LED_OFF);

	/* 删除设备*/
	device_destroy(dtsLED_dev.class, dtsLED_dev.devid);

	/* 删除类 */
	class_destroy(dtsLED_dev.class);

	/* 注销字符设备 */
	cdev_del(&dtsLED_dev.cdev);

	/* 注销设备号 */
	unregister_chrdev_region(dtsLED_dev.devid, 1);

	return;
}

ssize_t dtsLED_read(struct file *filp, char __user * buf, size_t cnt, loff_t *ppos) {
	dtsLED_dev_t *dev = (dtsLED_dev_t*)filp->private_data;
	u32 val = readl(dev->reg.IMX6U_GPIO1_DR); // 读取LED信息
	int ret;
	
	ret = copy_to_user(buf, (val & (1 << 3)) ? "0" : "1", 2); // 返回LED信息
	if (ret < 0) { // 异常处理
		printk("kernel read failed!\n");
		return ret;
	}
	return 2;
}

ssize_t dtsLED_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *ppos) {
	dtsLED_dev_t *dev = (dtsLED_dev_t*)filp->private_data;

	u8 kernal_buf[8];
	int ret;

	ret = copy_from_user(kernal_buf, buf, cnt); // 读取输入
	if (ret < 0) { // 异常处理
		printk("kernal write failed!\n");
		return ret;
	}

	LED_switch((kernal_buf[0] - '0' == 0) ? LED_OFF : LED_ON); // 修改LED状态

	return 0;
}

int dtsLED_open(struct inode *inode, struct file *filp) {
	filp->private_data = &dtsLED_dev;
	return 0;
}

int dtsLED_release(struct inode *inode, struct file *filp) {
	dtsLED_dev_t *dev = (dtsLED_dev_t*)filp->private_data;
	
	return 0;
}

static const struct file_operations dtsLED_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= dtsLED_read,
	.write		= dtsLED_write,
	.open		= dtsLED_open,
	.release	= dtsLED_release,
};

/*
 * 模块入口和出口
 */
module_init(dtsLED_init);
module_exit(dtsLED_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");