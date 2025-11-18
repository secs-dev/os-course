#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

#define MODULE_NAME "vtkm"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

static int __init vtkm_init(void) {
  LOG("VTKM joined the kernel\n");
  return 0;
}

static void __exit vtkm_exit(void) {
  LOG("VTKM left the kernel\n");
}

module_init(vtkm_init);
module_exit(vtkm_exit);
