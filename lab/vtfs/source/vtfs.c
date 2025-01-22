#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>  // Для kmalloc и kfree
#include <linux/stat.h>  // Для S_IFDIR и других флагов

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

// Прототипы функций
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

// structs
struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
};

struct file_operations vtfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = vtfs_iterate,
};

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
    struct file_system_type* fs_type, int flags, const char* token, void* data
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
int vtfs_fill_super(struct super_block* sb, void* data, int silent) {
  struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR, 1000);

  inode->i_mode = S_IFDIR | S_IRWXG | S_IRWXU | S_IRWXO;  // Директория с правами drwxrwxrwx
  inode->i_op = &vtfs_inode_ops;                 // Операции для inode
  inode->i_fop = &vtfs_dir_ops;                  // Операции для директории

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    return -ENOMEM;
  }

  printk(KERN_INFO "return 0\n");
  return 0;
}

// Функция создания inode
struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
) {
  struct inode* inode = new_inode(sb);
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

// PART 3 СТРУКТУРА ДЛЯ ПРОСМОТРА СОЗДАННОГО /mnt/vt. Однако он все еще недоступен для просмотра
// содержимого
struct dentry* vtfs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag) {
    const char* name = child_dentry->d_name.name;
    struct inode* inode = NULL;

    if (!strcmp(name, "test.txt")) {
        inode = vtfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 101);
    } else if (!strcmp(name, "dir")) {
        inode = vtfs_get_inode(parent_inode->i_sb, NULL, S_IFDIR, 102);
    }

    if (inode) {
        d_add(child_dentry, inode);
    } else {
        d_add(child_dentry, NULL);
    }

    return NULL;
}


// PART 3 Эта функция вызывается только для директорий и выводит список объектов в ней
int vtfs_iterate(struct file* filp, struct dir_context* ctx) {
    struct dentry* dentry = filp->f_path.dentry;
    struct inode* inode = dentry->d_inode;
    unsigned long offset = ctx->pos;  // Текущая позиция в каталоге
    ino_t ino = inode->i_ino;

    printk(KERN_INFO "vtfs iterate was called with ino: %lu, offset: %lu\n",
       (unsigned long)ino, offset);

    // Проверяем, что мы работаем с правильным inode
    // if (ino != 1000) {
    //     return -ENOENT;  // Код ошибки: объект не найден
    // }

    // Генерация содержимого каталога
    if (offset == 0) {
        if (!dir_emit(ctx, ".", 1, ino, DT_DIR)) {
            return 0;  // Прерывание, если ядро не может добавить запись
        }
        ctx->pos++;  // Увеличиваем позицию для перехода к следующему объекту
    }

    if (offset == 1) {
        if (!dir_emit(ctx, "..", 2, dentry->d_parent->d_inode->i_ino, DT_DIR)) {
            return 0;
        }
        ctx->pos++;
    }

    if (offset == 2) {
        if (!dir_emit(ctx, "test.txt", 8, 101, DT_REG)) {
            return 0;
        }
        ctx->pos++;
    }

    return 0;  // Возвращаем 0 после завершения перечисления объектов
}
