#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/slab.h> // Для kmalloc и kfree
#include <linux/stat.h> // Для S_IFDIR и других флагов

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

// Прототипы функций
struct dentry* vtfs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data);
int vtfs_fill_super(struct super_block *sb, void *data, int silent);
void vtfs_kill_sb(struct super_block* sb);
struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);

// Описание файловой системы
struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
    .owner = THIS_MODULE,
};

// Инициализация модуля
static int __init vtfs_init(void) {
    int ret = register_filesystem(&vtfs_fs_type);
    if (ret != 0) {
        LOG("Failed to register filesystem\n");
        return ret;
    }
    LOG("VTFS joined the kernel\n");
    return 0;
}

// Очистка модуля
static void __exit vtfs_exit(void) {
    int ret = unregister_filesystem(&vtfs_fs_type);
    if (ret != 0) {
        LOG("Failed to unregister filesystem\n");
    } else {
        LOG("VTFS left the kernel\n");
    }
}

module_init(vtfs_init);
module_exit(vtfs_exit);

// Функция монтирования файловой системы
struct dentry* vtfs_mount(
    struct file_system_type* fs_type,
    int flags,
    const char* token,
    void* data
) {
    struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
    if (IS_ERR(ret)) {
        LOG("Can't mount file system\n");
    } else {
        LOG("Mounted successfully\n");
    }
    return ret;
}

// Заполнение super_block
int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR, 1000);

    sb->s_root = d_make_root(inode);
    if (sb->s_root == NULL) {
        return -ENOMEM;
    }

    printk(KERN_INFO "return 0\n");
    return 0;
}

// Функция создания inode
struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino) {
    struct inode *inode = new_inode(sb);
    if (inode != NULL) {
        // Используем nop_mnt_idmap для первого аргумента
        inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    }
    inode->i_ino = i_ino;
    return inode;
}



// Функция отмонтирования файловой системы
void vtfs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}
