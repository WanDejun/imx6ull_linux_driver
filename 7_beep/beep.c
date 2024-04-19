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

#define BEEP_CNT 1
#define BEEP_NAME "beep"

typedef struct {
    dev_t devid;
    u32 major;
    u32 minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    u32 beep_gpio;
} beep_dev_t;
beep_dev_t beep_dev;

static const struct file_operations beep_dev_fops;

typedef enum { BEEP_OFF = 0, BEEP_ON = 1 } LED_status_t;

static void BEEP_switch(LED_status_t NewStatus) {
	if (NewStatus == BEEP_OFF) gpio_set_value(beep_dev.beep_gpio, 1);
	else gpio_set_value(beep_dev.beep_gpio, 0);
}

ssize_t beep_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
	u32 val = gpio_get_value(beep_dev.beep_gpio);
	int ret;
	
	ret = copy_to_user(buf, val ? "0" : "1", 2); // 返回BEEP信息
	if (ret < 0) { // 异常处理
		printk("kernel read failed!\n");
		return ret;
	}
	return 2;
}

ssize_t beep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *lof) {
    u8 kernal_buf[8];
	int ret;

	ret = copy_from_user(kernal_buf, buf, cnt); // 读取输入
	if (ret < 0) { // 异常处理
		printk("kernal write failed!\n");
		return ret;
	}

	BEEP_switch((kernal_buf[0] - '0' == 0) ? BEEP_OFF : BEEP_ON); // 修改LED状态

    return 0;
}

int beep_open(struct inode *inode, struct file *filp) {
    filp->private_data = &beep_dev;

    return 0;
}

int beep_release(struct inode *inode, struct file *filp) {
    filp->private_data = NULL;

    return 0;
}


static int __init beep_init(void) {
    int err;

    /* 
     * 1 获取设备id 
     */
    beep_dev.major = 0;
    if (beep_dev.major != 0) { // 手动定义设备id
        beep_dev.devid = MKDEV(beep_dev.major, 0);
        err = register_chrdev_region(beep_dev.devid, BEEP_CNT, BEEP_NAME);
    }
    else {
        err = alloc_chrdev_region(&beep_dev.devid, 0, BEEP_CNT, BEEP_NAME);
    }

    if (err < 0) {
        printk("failed_regcdev\n");
        goto failed_regcdev;
    }
    beep_dev.major = MAJOR(beep_dev.devid);
    beep_dev.minor = MINOR(beep_dev.devid);


    /* 
     * 2 注册字符设备
     */
    beep_dev.cdev.owner = THIS_MODULE;
    cdev_init(&beep_dev.cdev, &beep_dev_fops); // 初始化cdev
    err = cdev_add(&beep_dev.cdev, beep_dev.devid, BEEP_CNT);
    if (err < 0) {
        printk("failed_cdev\n");
        goto failed_cdev;
    }


    /* 
     * 3 注册类
     */
    beep_dev.class = class_create(THIS_MODULE, BEEP_NAME); //初始化类
    err = IS_ERR(beep_dev.class);
    if (err < 0) {
        printk("failed_class\n");
        goto failed_class;
    }

    beep_dev.device = device_create(beep_dev.class, NULL, beep_dev.devid, NULL, BEEP_NAME); //初始化设备
    err = IS_ERR(beep_dev.device);
    if (err < 0) {
        printk("failed_device\n");
        goto failed_device;
    }


    /** 
     * 4 获取设备树节点 
     */
    beep_dev.nd = of_find_node_by_path("/alientek/beep");
    if (beep_dev.nd == NULL) {
        err = -EINVAL;
        goto fail_findnode;
    }

    beep_dev.beep_gpio = of_get_named_gpio(beep_dev.nd, "beep-gpios", 0);
    if (beep_dev.beep_gpio < 0) {
        printk("can not find beep gpio!\n");
        err = -EINVAL;
        goto failed_get_gpio;
    }
    printk("beep_gpio:%d\n", beep_dev.beep_gpio);


    /*
     * 5 申请gpio使用权
     */
    err = gpio_request(beep_dev.beep_gpio, "beep");
    if (err) { // != 0 error
        printk("can not request beep gpio!\n");
        goto failed_request_gpio;
    }


    /*
     * 6 使用gpio, 设置为输出
     */
    err = gpio_direction_output(beep_dev.beep_gpio, 1);
    if (err) {
        goto faibeep_gpio_dirction;
    }


    /*
     * 7 输出低电平, 开灯
     */
    gpio_set_value(beep_dev.beep_gpio, 0);

    return 0;



/**
 *@ funtion: 错误处理
 */
faibeep_gpio_dirction:
failed_request_gpio:
failed_get_gpio:
fail_findnode:
    device_destroy(beep_dev.class, beep_dev.devid);
failed_device:
    class_destroy(beep_dev.class);
failed_class:
    cdev_del(&beep_dev.cdev);
failed_cdev:
    unregister_chrdev_region(beep_dev.devid, BEEP_CNT);
failed_regcdev:
    return err;
}

static void __exit beep_exit(void) {
    /* 设置led关 */
    gpio_set_value(beep_dev.beep_gpio, 1);

    /* 释放led-gpio */
    gpio_free(beep_dev.beep_gpio);

    /*删除设备*/
    device_destroy(beep_dev.class, beep_dev.devid);

    /*删除类*/
    class_destroy(beep_dev.class);

    /*删除字符设备*/
    cdev_del(&beep_dev.cdev);
    
    /*注销设备号*/
    unregister_chrdev_region(beep_dev.devid, BEEP_CNT);
}

static const struct file_operations beep_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= beep_read,
	.write		= beep_write,
	.open		= beep_open,
	.release	= beep_release,
};

/*
 * 模块入口和出口
 */
module_init(beep_init);
module_exit(beep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");