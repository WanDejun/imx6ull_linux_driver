#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define NEW_LED_MAJOR 		200
#define NEW_LED_NAME 		"NEW_LED"
#define NEW_LED_DEV_CNT 	1

/* 物理寄存器定义 */
#define CCM_CCGR1_BASE 				(0x020C406C)
#define SW_MUX_GPIO1_IO03_BASE		(0x020E0068)
#define SW_PAD_GPIO1_IO03_BASE		(0x020E02F4)
#define GPIO1_DR_BASE				(0x0209C000)
#define GPIO1_GDIR_BASE				(0x0209C004)

/* MMU映射地址指针 */
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *IMX6U_SW_MUX_GPIO1_IO03;
static void __iomem *IMX6U_SW_PAD_GPIO1_IO03;
static void __iomem *IMX6U_GPIO1_DR;
static void __iomem *IMX6U_GPIO1_GDIR;

static const struct file_operations LED_dev_fops;

typedef struct {
	struct cdev 	cdev;
	dev_t			devid; //设备号
	struct class* 	class;
	struct device*  device;
	int 			major; //主设备号
	int 			minor; //次设备号
} New_LED_Dev_t;

New_LED_Dev_t New_LED_Dev;

typedef enum {
	LED_OFF = 0,
	LED_ON = 1,
} LED_STATE;

static void LED_switch(LED_STATE state) {
	u32 val = 0;
	val = readl(IMX6U_GPIO1_DR);
	if (state == LED_ON) val &= ~(1 << 3);
	else  val |= (1 << 3);
	writel(val, IMX6U_GPIO1_DR);
}
static void imxiounmap(void) {
	iounmap(IMX6U_CCM_CCGR1);
	iounmap(IMX6U_SW_MUX_GPIO1_IO03);
	iounmap(IMX6U_SW_PAD_GPIO1_IO03);
	iounmap(IMX6U_GPIO1_DR);
	iounmap(IMX6U_GPIO1_GDIR);
}

ssize_t New_LED_read(struct file *file, char __user *buf, size_t count, loff_t *filp) {
	u32 val = 0;
	u8 ret = 0;
	val = readl(IMX6U_GPIO1_DR) & (1 << 3);
	ret = copy_to_user(buf, (val & (1 << 3)) ? "0" : "1", 2);
	if (ret < 0) {
		printk("kernel read failed!\n");
	}
	
	return 0;
}

ssize_t New_LED_write(struct file *file, const char __user *buf, size_t count, loff_t *filp) {
	int val;
	u8 detaBuf[1];

	val = copy_from_user(detaBuf, buf, count);

	if (val < 0) { // error
		printk("kernal write filed!\n"); // print kernel error log
		return -EFAULT;
	}

	if (*(detaBuf) - '0' != LED_OFF) { // led on
		LED_switch(LED_ON);
	}
	else { // led off
		LED_switch(LED_OFF);
	}
	return 0;
}

int New_LED_open(struct inode *inode, struct file *file) {
	file->private_data = &New_LED_Dev;

	return 0;
}

int New_LED_release(struct inode *inode, struct file *file) {
	New_LED_Dev_t *dev = (New_LED_Dev_t*)file->private_data; 

	return 0;
}

static int __init New_LED_init(void) {
	int ret = 0, val;
	int err;

/* 1.初始化LED IO */
	/* MMU地址映射 */
	IMX6U_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE, 4);
	IMX6U_SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
	IMX6U_SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
	IMX6U_GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
	IMX6U_GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE, 4);

	/* 开启时钟 */
	val = readl(IMX6U_CCM_CCGR1);
	val |= (3 << 26); 
	writel(val, IMX6U_CCM_CCGR1);

	writel(0x5, IMX6U_SW_MUX_GPIO1_IO03);
	writel(0x10b0, IMX6U_SW_PAD_GPIO1_IO03);

	val = readl(IMX6U_GPIO1_GDIR);
	val |= 1 << 3;
	writel(val, IMX6U_GPIO1_GDIR);

	LED_switch(LED_OFF);

/* 2.获取设备号 */
	New_LED_Dev.major = 0; // 从系统自动申请设备号
	// 从操作系统获取或在下面这段代码之前手动分配, 并初始化dev_t, cdev
	if (New_LED_Dev.major) { // 给定主设备号
		New_LED_Dev.devid = MKDEV(New_LED_Dev.major, 0);
		ret = register_chrdev_region(New_LED_Dev.devid, NEW_LED_DEV_CNT, NEW_LED_NAME);
	}
	else { // 没有给定主设备号
		ret = alloc_chrdev_region(&New_LED_Dev.devid, 0, NEW_LED_DEV_CNT, NEW_LED_NAME);
		New_LED_Dev.major = MAJOR(New_LED_Dev.devid);
		New_LED_Dev.minor = MINOR(New_LED_Dev.devid);
	}

	if (ret < 0) {
		goto major_error;
	}

	printk("new char dev major = %d, minor = %d\n", New_LED_Dev.major, New_LED_Dev.minor);


/* 3.注册字符设备 */
	New_LED_Dev.cdev.owner = THIS_MODULE;
	cdev_init(&New_LED_Dev.cdev, &LED_dev_fops);
	ret = cdev_add(&New_LED_Dev.cdev, New_LED_Dev.devid, NEW_LED_DEV_CNT);

	if (ret < 0) 
		goto cdev_error;


/* 4. 自动创建/dev节点 */
	/* 创建类 */
	New_LED_Dev.class = class_create(THIS_MODULE, NEW_LED_NAME);
	err = PTR_ERR(New_LED_Dev.class);
	if (IS_ERR(New_LED_Dev.class))
		goto class_error;

	/* 创建设备 */
	New_LED_Dev.device = device_create(New_LED_Dev.class, NULL, New_LED_Dev.devid, NULL, NEW_LED_NAME);
	err = PTR_ERR(New_LED_Dev.device);
	if (IS_ERR(New_LED_Dev.device))
		goto device_error;

	printk("LED_INIT\n");
	return 0;

major_error:
	LED_switch(LED_OFF);
	imxiounmap();
	printk("new char dev chrdev_region error!\n");
	return -1;

cdev_error:
	unregister_chrdev_region(New_LED_Dev.devid, NEW_LED_DEV_CNT);
	LED_switch(LED_OFF);
	imxiounmap();
	printk("register chardev failed!\n");
	return -EIO;

device_error:
	class_destroy(New_LED_Dev.class);
class_error:
	cdev_del(&New_LED_Dev.cdev);
	unregister_chrdev_region(New_LED_Dev.devid, NEW_LED_DEV_CNT);
	LED_switch(LED_OFF);
	imxiounmap();
	return err;
}

static void __exit New_LED_exit(void) {
	/* 摧毁设备 */
	device_destroy(New_LED_Dev.class, New_LED_Dev.devid);

	/* 摧毁设备类 */
	class_destroy(New_LED_Dev.class);

	/* 注销设备 */
	cdev_del(&New_LED_Dev.cdev);

	/* 注销设备号 */
	unregister_chrdev_region(New_LED_Dev.devid, NEW_LED_DEV_CNT);

	/* 关闭LED */
	LED_switch(LED_OFF);

	/* 注销MMU地址映射 */
	imxiounmap();


	printk("LED_EXIT\n");
}

static const struct file_operations LED_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= New_LED_read,
	.write		= New_LED_write,
	.open		= New_LED_open,
	.release	= New_LED_release,
};

/*
 * 模块入口与出口
 */
module_init(New_LED_init);
module_exit(New_LED_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");
