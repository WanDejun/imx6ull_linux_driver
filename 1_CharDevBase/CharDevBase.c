#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define CHAR_DEV_BASE_MAJOR 	200 // 主设备号
#define CHAR_DEV_BASE_NAME 		"char_dev_base"

static const struct file_operations char_dev_base_fops;

char kernelData[128] = "";

int CharDevBase_open(struct inode *inode, struct file *file) {
	// printk(KERN_NOTICE"char dev base OPEN\n");

	return 0;
}

int CharDevBase_release(struct inode *inode, struct file *file) {
	// printk(KERN_NOTICE"char dev base RELEASE\n");

	return 0;
}

ssize_t CharDevBase_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	int ret = 0;

	ret = copy_to_user(buf, kernelData, count);
	if (ret == 0) {

	}
	else {
		printk(KERN_NOTICE"char dev base READ_ERROR!\n");
	}

	return 0;
}

ssize_t CharDevBase_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	int ret = 0;
	ret = copy_from_user(kernelData, buf, count);
	if (ret == 0) {
	}
	else {
		printk(KERN_NOTICE"char dev base WRITE_ERROR!\n");
	}

	return 0;
}

static int __init CharDevBase_init(void) {
	int ret;
	printk(KERN_NOTICE"char dev base INIT\n");

	ret = register_chrdev(CHAR_DEV_BASE_MAJOR, CHAR_DEV_BASE_NAME, &char_dev_base_fops);
	if (ret < 0) {
		printk(KERN_NOTICE"char dev base INIT FAILED!\n");
	}
	return 0;
}

static void __exit CharDevBase_exit(void){
	unregister_chrdev(CHAR_DEV_BASE_MAJOR, CHAR_DEV_BASE_NAME);
	printk(KERN_NOTICE"char dev base EXIT\n");
}

static const struct file_operations char_dev_base_fops = {
	.owner		= THIS_MODULE,
	.read		= CharDevBase_read,
	.write		= CharDevBase_write,
	.open		= CharDevBase_open,
	.release	= CharDevBase_release,
};

/*
 * 模块入口与出口
 */
module_init(CharDevBase_init);
module_exit(CharDevBase_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");