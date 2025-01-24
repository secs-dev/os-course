#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

void vtfs_kill_sb(struct super_block*);
struct dentry* vtfs_mount(struct file_system_type*, int, const char*, void*);
int vtfs_fill_super(struct super_block*, void*, int);
struct inode* vtfs_get_inode(struct super_block*, const struct inode*, umode_t, int);

struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
) {
  struct inode* inode = new_inode(sb);
  if (inode != NULL) {
    // in linux kernel v6.8.0, inode_init_owner() call takes struct mnt_idmap* as first argument
    // instead of usual user_namespace*, and i do not have any way to access this structure this
    // here is a basic replacement of inode_init_owner() call
    inode->i_mode = mode;
    i_uid_write(inode, 0);
    i_gid_write(inode, 0);
    inode->__i_atime = inode->__i_mtime = inode->__i_ctime = current_time(inode);
  }

  inode->i_ino = i_ino;
  return inode;
}

int vtfs_fill_super(struct super_block* sb, void* data, int silent) {
  struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR, 1000);
  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    return -ENOMEM;
  }
  printk(KERN_INFO "return 0\n");
  return 0;
}

void vtfs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}

struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

struct dentry* vtfs_mount(
    struct file_system_type* fs_type, int flags, const char* token, void* data
) {
  struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (ret == NULL) {
    printk(KERN_ERR "Can't mount file system");
  } else {
    printk(KERN_INFO "Mounted successfully");
  }
  return ret;
}

static int __init vtfs_init(void) {
  register_filesystem(&vtfs_fs_type);
  LOG("VTFS joined the kernel\n");
  return 0;
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
  LOG("VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);
