#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define LED_MAJOR 200
#define LED_NAME "LED"

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

typedef enum {
	LED_OFF = 0,
	LED_ON = 1,
} LED_STATE;

void LED_switch(LED_STATE state) {
	u32 val = 0;
	val = readl(IMX6U_GPIO1_DR);
	if (state == LED_ON) val &= ~(1 << 3);
	else  val |= (1 << 3);
	writel(val, IMX6U_GPIO1_DR);
}

ssize_t LED_read(struct file *file, char __user *buf, size_t count, loff_t *filp) {
	u32 val = 0;
	u8 ret = 0;
	val = readl(IMX6U_GPIO1_DR) & (1 << 3);
	ret = copy_to_user(buf, (val & (1 << 3)) ? "0" : "1", 2);
	if (ret < 0) {
		printk("kernel read failed!\n");
	}
	
	return 0;
}

ssize_t LED_write(struct file* file, const char __user *buf, size_t count, loff_t *filp) {
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

int LED_open(struct inode *inode, struct file *file) {

	return 0;
}

int LED_release(struct inode *inode, struct file *file) {

	return 0;
}

static int __init LED_init(void) {
	int res, val;

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

	/*注册设备*/
	res = register_chrdev(LED_MAJOR, LED_NAME, &LED_dev_fops);
	if (res < 0) {
		printk("register chardev failed!\n");
		return -EIO;
	}

	printk("LED_INIT\n");
	return 0;
}

static void __exit LED_exit(void) {
	LED_switch(LED_OFF);

	/* 注销MMU地址映射 */
	iounmap(IMX6U_CCM_CCGR1);
	iounmap(IMX6U_SW_MUX_GPIO1_IO03);
	iounmap(IMX6U_SW_PAD_GPIO1_IO03);
	iounmap(IMX6U_GPIO1_DR);
	iounmap(IMX6U_GPIO1_GDIR);

	/* 注销设备 */
	unregister_chrdev(LED_MAJOR, LED_NAME);
	printk("LED_EXIT\n");
	return;
}

static const struct file_operations LED_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= LED_read,
	.write		= LED_write,
	.open		= LED_open,
	.release	= LED_release,
};

/*
 * 模块入口与出口
 */
module_init(LED_init);
module_exit(LED_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");