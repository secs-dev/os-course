#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>  // Для kmalloc и kfree
#include <linux/stat.h>  // Для S_IFDIR и других флагов

#define MODULE_NAME "vtfs"
#define ROOT_INODE 100
#define TEST_FILE_INODE 101
#define NEW_FILE_INODE 102
#define DIR_INODE 200

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lubitelkvokk");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

// =================
// Прототипы функций
// =================
struct dentry* vtfs_mount(
    struct file_system_type* fs_type, int flags, const char* token, void* data
);

int vtfs_fill_super(struct super_block* sb, void* data, int silent);

void vtfs_kill_sb(struct super_block* sb);

struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
);

struct dentry* vtfs_lookup(
    struct inode* parent_inode,  // родительская нода
    struct dentry* child_dentry,  // объект, к которому мы пытаемся получить доступ
    unsigned int flag  // неиспользуемое значение
);

int vtfs_iterate(struct file* filp, struct dir_context* ctx);

int vtfs_create(
    struct mnt_idmap* idmap,
    struct inode* parent_inode,
    struct dentry* child_dentry,
    umode_t mode,
    bool b
);

int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry);
int vtfs_mkdir(
    struct mnt_idmap* idmap, struct inode* parent_inode, struct dentry* child_dentry, umode_t mode
);

int vtfs_rmdir(struct inode*, struct dentry*);

ssize_t vtfs_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset);
ssize_t vtfs_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset);

int vtfs_link(
  struct dentry *old_dentry, 
  struct inode *parent_dir, 
  struct dentry *new_dentry
);
// =========
// Структуры
// =========
struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .unlink = vtfs_unlink,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
    .link = vtfs_link,
};

struct file_operations vtfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = vtfs_iterate,
};

struct file_operations vtfs_file_ops = {
    .open = simple_open,            // стандартная реализация
    .llseek = generic_file_llseek,  // -||-
    .read = vtfs_read,
    .write = vtfs_write,
};

// Описание файловой системы
struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
    .owner = THIS_MODULE,
};

struct vtfs_file {
  char name[256];         // Имя файла
  struct inode* inode;    // Указатель на inode
  struct list_head list;  // Связанный список
};

// Добавить это поле в inode для хранения списка дочерних файлов
struct vtfs_inode_info {
  struct list_head children;  // Список дочерних файлов
};

struct vtfs_file_content {
  char* data;   // Указатель на содержимое файла
  size_t size;  // Размер данных
};
