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
struct dentry* vtfs_lookup(struct inode*, struct dentry*, unsigned int);
int vtfs_iterate(struct file*, struct dir_context*);

struct file_operations vtfs_dir_ops = {
    .iterate_shared = vtfs_iterate,
};

int vtfs_iterate(struct file* flip, struct dir_context* ctx) {
  struct dentry* dentry = flip->f_path.dentry;
  struct inode* inode = dentry->d_inode;
  unsigned long offset = ctx->pos;
  ino_t ino = inode->i_ino;

  // for now we must keep fixed list of entries
  // standard solution involved iterating inside infinite loop with an upper condition,
  // which when not satisfied will throw kernel into panic
  struct {
    const char* name;
    unsigned char ftype;
    ino_t ino;
  } entries[] = {
      {       ".", DT_DIR,                              ino},
      {      "..", DT_DIR, dentry->d_parent->d_inode->i_ino},
      {"test.txt", DT_REG,                              101},
  };

  int total_entries = sizeof(entries) / sizeof(entries[0]);

  if (offset >= total_entries) {
    return 0;
  }

  for (; offset < total_entries; offset++) {
    if (!dir_emit(
            ctx,
            entries[offset].name,
            strlen(entries[offset].name),
            entries[offset].ino,
            entries[offset].ftype
        )) {
      return -ENOMEM;
    }
    ctx->pos++;
  }
  return 0;
}

struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
};

struct dentry* vtfs_lookup(
    struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag
) {
  return NULL;
}
struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
) {
  struct inode* inode = new_inode(sb);
  struct mnt_idmap* idmap = &nop_mnt_idmap;
  if (inode != NULL) {
    inode_init_owner(idmap, inode, dir, mode);
    inode->i_mode = mode;
  }
  inode->i_ino = i_ino;
  return inode;
}

int vtfs_fill_super(struct super_block* sb, void* data, int silent) {
  struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, 100);
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;
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
