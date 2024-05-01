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
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#define LED_CNT 1
#define LED_NAME "platform_LED"

struct platform_device led_platform_dev;

ssize_t LED_read(struct file*, char __user* usrBuf, size_t cnt, loff_t * ) {

}

ssize_t LED_write(struct file *, const char __user* usrBuf, size_t cnt, loff_t*) {

}

int LED_open(struct inode*, struct file *) {

}

int LED_release(struct inode *, struct file *) {

}


static int __init LED_init(void) {
	platform_device_register(&led_platform_dev);
	platform_device_unregister(&led_platform_dev)
	return 0;
}

static void __exit LED_exit(void) {

}

static const struct file_operations LED_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= LED_read,
	.write		= LED_write,
	.open		= LED_open,
	.release	= LED_release,
};

/*
 * 模块入口和出口
 */
module_init(LED_init);
module_exit(LED_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");