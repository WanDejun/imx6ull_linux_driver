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
#include <linux/types.h>
#include <linux/atomic.h>

#define GPIOLED_CNT 1
#define GPIOLED_NAME "gpioLED"

typedef struct {
    dev_t devid;
    u32 major;
    u32 minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    u32 led_gpio;
    atomic_t atomic;
} gpioLED_dev_t;
gpioLED_dev_t gpioLED_dev = {.atomic = ATOMIC_INIT(0)};

static const struct file_operations gpioLED_dev_fops;

typedef enum { LED_OFF = 0, LED_ON = 1 } LED_status_t;

static void LED_switch(LED_status_t NewStatus) {
	if (NewStatus == LED_OFF) gpio_set_value(gpioLED_dev.led_gpio, 1);
	else gpio_set_value(gpioLED_dev.led_gpio, 0);
}

ssize_t gpioLED_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
	u32 val = gpio_get_value(gpioLED_dev.led_gpio);
	int ret;
	
	ret = copy_to_user(buf, val ? "0" : "1", 2); // 返回LED信息
	if (ret < 0) { // 异常处理
		printk("kernel read failed!\n");
		return ret;
	}
	return 2;
}

ssize_t gpioLED_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *lof) {
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

int gpioLED_open(struct inode *inode, struct file *filp) {
    filp->private_data = &gpioLED_dev;

    if (atomic_dec_and_test(&gpioLED_dev.atomic) <= 0) { // 占用锁
        atomic_inc(&gpioLED_dev.atomic); // 释放锁
        return -EBUSY; // 锁被占用
    }
    
    return 0;
}

int gpioLED_release(struct inode *inode, struct file *filp) {
    filp->private_data = NULL;

    atomic_inc(&gpioLED_dev.atomic); // 释放锁

    return 0;
} 


static int __init gpioLED_init(void) {
    int err;

    /* 
     * 初始化atomic
     */
    atomic_set(&gpioLED_dev.atomic, 1);
    /* 
     * 1 获取设备id 
     */
    gpioLED_dev.major = 0;
    if (gpioLED_dev.major != 0) { // 手动定义设备id
        gpioLED_dev.devid = MKDEV(gpioLED_dev.major, 0);
        err = register_chrdev_region(gpioLED_dev.devid, GPIOLED_CNT, GPIOLED_NAME);
    }
    else {
        err = alloc_chrdev_region(&gpioLED_dev.devid, 0, GPIOLED_CNT, GPIOLED_NAME);
    }

    if (err < 0) {
        printk("failed_regcdev\n");
        goto failed_regcdev;
    }
    gpioLED_dev.major = MAJOR(gpioLED_dev.devid);
    gpioLED_dev.minor = MINOR(gpioLED_dev.devid);


    /* 
     * 2 注册字符设备
     */
    gpioLED_dev.cdev.owner = THIS_MODULE;
    cdev_init(&gpioLED_dev.cdev, &gpioLED_dev_fops); // 初始化cdev
    err = cdev_add(&gpioLED_dev.cdev, gpioLED_dev.devid, GPIOLED_CNT);
    if (err < 0) {
        printk("failed_cdev\n");
        goto failed_cdev;
    }


    /* 
     * 3 注册类
     */
    gpioLED_dev.class = class_create(THIS_MODULE, GPIOLED_NAME); //初始化类
    err = IS_ERR(gpioLED_dev.class);
    if (err < 0) {
        printk("failed_class\n");
        goto failed_class;
    }

    gpioLED_dev.device = device_create(gpioLED_dev.class, NULL, gpioLED_dev.devid, NULL, GPIOLED_NAME); //初始化设备
    err = IS_ERR(gpioLED_dev.device);
    if (err < 0) {
        printk("failed_device\n");
        goto failed_device;
    }


    /** 
     * 4 获取设备树节点 
     */
    gpioLED_dev.nd = of_find_node_by_path("/alientek/gpioled1");
    if (gpioLED_dev.nd == NULL) {
        err = -EINVAL;
        goto fail_findnode;
    }

    gpioLED_dev.led_gpio = of_get_named_gpio(gpioLED_dev.nd, "led1-gpios", 0);
    if (gpioLED_dev.led_gpio < 0) {
        printk("can not find led gpio!\n");
        err = -EINVAL;
        goto failed_get_gpio;
    }
    printk("led_gpio:%d\n", gpioLED_dev.led_gpio);


    /*
     * 5 申请gpio使用权
     */
    err = gpio_request(gpioLED_dev.led_gpio, "led1-gpios");
    if (err) { // != 0 error
        printk("can not request led gpio!\n");
        goto failed_request_gpio;
    }


    /*
     * 6 使用gpio, 设置为输出
     */
    err = gpio_direction_output(gpioLED_dev.led_gpio, 1);
    if (err) {
        goto failed_gpio_dirction;
    }


    /*
     * 7 输出低电平, 开灯
     */
    gpio_set_value(gpioLED_dev.led_gpio, 0);

    return 0;



/**
 *@ funtion: 错误处理
 */
failed_gpio_dirction:
failed_request_gpio:
failed_get_gpio:
fail_findnode:
    device_destroy(gpioLED_dev.class, gpioLED_dev.devid);
failed_device:
    class_destroy(gpioLED_dev.class);
failed_class:
    cdev_del(&gpioLED_dev.cdev);
failed_cdev:
    unregister_chrdev_region(gpioLED_dev.devid, GPIOLED_CNT);
failed_regcdev:
    return err;
}

static void __exit gpioLED_exit(void) {
    /* 设置led关 */
    gpio_set_value(gpioLED_dev.led_gpio, 1);

    /* 释放led-gpio */
    gpio_free(gpioLED_dev.led_gpio);

    /*删除设备*/
    device_destroy(gpioLED_dev.class, gpioLED_dev.devid);

    /*删除类*/
    class_destroy(gpioLED_dev.class);

    /*删除字符设备*/
    cdev_del(&gpioLED_dev.cdev);
    
    /*注销设备号*/
    unregister_chrdev_region(gpioLED_dev.devid, GPIOLED_CNT);
}

static const struct file_operations gpioLED_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= gpioLED_read,
	.write		= gpioLED_write,
	.open		= gpioLED_open,
	.release	= gpioLED_release,
};

/*
 * 模块入口和出口
 */
module_init(gpioLED_init);
module_exit(gpioLED_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");