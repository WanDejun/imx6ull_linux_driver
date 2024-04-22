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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>

#define KEY_CNT 1
#define KEY_NAME "key0"

typedef struct {
    dev_t devid;
    u32 major;
    u32 minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    u32 key_gpio;
    u8 dev_status; // 等于1:设备被占用
    spinlock_t spinlock;
} key_dev_t;
key_dev_t key_dev;

static const struct file_operations key_dev_fops;

static int spinlock_init() {
    key_dev.dev_status = 0;
    spinlock_init(&key_dev.spinlock);
}

static int key_gpio_init(void) {
    int err;
    /** 
     * 1 获取设备树节点 
     */
    key_dev.nd = of_find_node_by_path("/alientek/key0");
    if (key_dev.nd == NULL) {
        err = -EINVAL;
        goto fail_findnode;
    }

    key_dev.key_gpio = of_get_named_gpio(key_dev.nd, "key0-gpios", 0);
    if (key_dev.key_gpio < 0) {
        printk("can not find key gpio!\n");
        err = -EINVAL;
        goto failed_get_gpio;
    }
    printk("key_gpio:%d\n", key_dev.key_gpio);


    /*
     * 2 申请gpio使用权
     */
    err = gpio_request(key_dev.key_gpio, "key0-gpios");
    if (err) { // != 0 error
        printk("can not request key gpio!\n");
        goto failed_request_gpio;
    }


    /*
     * 3 使用gpio, 设置为输出
     */
    err = gpio_direction_input(key_dev.key_gpio);
    if (err) {
        goto faikey_gpio_dirction;
    }

    return 0;


faikey_gpio_dirction:
    gpio_free(key_dev.key_gpio);
failed_request_gpio:
failed_get_gpio:
fail_findnode:
    return err;
}

ssize_t key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
	u32 val = gpio_get_value(key_dev.key_gpio);
	int ret;
	
	ret = copy_to_user(buf, val ? "0" : "1", 2); // 返回KEY信息
	if (ret < 0) { // 异常处理
		printk("kernel read failed!\n");
		return ret;
	}
	return 2;
}

int key_open(struct inode *inode, struct file *filp) {
    u8 flag;
    filp->private_data = &key_dev;

    spin_lock_irqsave(&key_dev.spinlock, flag);
    if (key_dev.dev_status == 1) {
        printk("dev is busy!\n");
        return -EBUSY;
    }
    key_dev.dev_status = 1;
    spin_unlock_irqrestore(&key_dev.spinlock, flag);

    return 0;
}

int key_release(struct inode *inode, struct file *filp) {
    filp->private_data = NULL;

    spin_lock_irqsave(&key_dev.spinlock, flag);
    key_dev.dev_status = 0;
    spin_unlock_irqrestore(&key_dev.spinlock, flag);

    return 0;
}


static int __init key_init(void) {
    int err;
    spinlock_init();
    /* 
     * 1 获取设备id 
     */
    key_dev.major = 0;
    if (key_dev.major != 0) { // 手动定义设备id
        key_dev.devid = MKDEV(key_dev.major, 0);
        err = register_chrdev_region(key_dev.devid, KEY_CNT, KEY_NAME);
    }
    else {
        err = alloc_chrdev_region(&key_dev.devid, 0, KEY_CNT, KEY_NAME);
    }

    if (err < 0) {
        printk("failed_regcdev\n");
        goto failed_regcdev;
    }
    key_dev.major = MAJOR(key_dev.devid);
    key_dev.minor = MINOR(key_dev.devid);


    /* 
     * 2 注册字符设备
     */
    key_dev.cdev.owner = THIS_MODULE;
    cdev_init(&key_dev.cdev, &key_dev_fops); // 初始化cdev
    err = cdev_add(&key_dev.cdev, key_dev.devid, KEY_CNT);
    if (err < 0) {
        printk("failed_cdev\n");
        goto failed_cdev;
    }


    /* 
     * 3 注册类
     */
    key_dev.class = class_create(THIS_MODULE, KEY_NAME); //初始化类
    err = IS_ERR(key_dev.class);
    if (err < 0) {
        printk("failed_class\n");
        goto failed_class;
    }

    key_dev.device = device_create(key_dev.class, NULL, key_dev.devid, NULL, KEY_NAME); //初始化设备
    err = IS_ERR(key_dev.device);
    if (err < 0) {
        printk("failed_device\n");
        goto failed_device;
    }

    /*
     * 4 初始化GPIO
     */
    err = key_gpio_init();
    if (err < 0) {
        goto failed_gpio;
    }
    

    return 0;



/**
 *@ funtion: 错误处理
 */
failed_gpio:
    device_destroy(key_dev.class, key_dev.devid);
failed_device:
    class_destroy(key_dev.class);
failed_class:
    cdev_del(&key_dev.cdev);
failed_cdev:
    unregister_chrdev_region(key_dev.devid, KEY_CNT);
failed_regcdev:
    return err;
}

static void __exit key_exit(void) {
    /* 释放led-gpio */
    gpio_free(key_dev.key_gpio);

    /*删除设备*/
    device_destroy(key_dev.class, key_dev.devid);

    /*删除类*/
    class_destroy(key_dev.class);

    /*删除字符设备*/
    cdev_del(&key_dev.cdev);
    
    /*注销设备号*/
    unregister_chrdev_region(key_dev.devid, KEY_CNT);
}

static const struct file_operations key_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= key_read,
	.open		= key_open,
	.release	= key_release,
};

/*
 * 模块入口和出口
 */
module_init(key_init);
module_exit(key_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");