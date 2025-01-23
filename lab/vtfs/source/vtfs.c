#include "vtfs.h"

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
  struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR, ROOT_INODE);

  inode->i_mode = S_IFDIR | S_IRWXG | S_IRWXU | S_IRWXO;  // Директория с правами drwxrwxrwx
  inode->i_op = &vtfs_inode_ops;                          // Операции для inode
  inode->i_fop = &vtfs_dir_ops;  // Операции для директории

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
  if (!inode) {
    return NULL;
  }

  inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
  inode->i_ino = i_ino;

  // Если это каталог, инициализируем список
  if (S_ISDIR(mode)) {
    struct vtfs_inode_info* info = kmalloc(sizeof(struct vtfs_inode_info), GFP_KERNEL);
    if (!info) {
      iput(inode);
      return NULL;
    }
    INIT_LIST_HEAD(&info->children);
    inode->i_private = info;
  } else {
    inode->i_private = NULL;  // Для файлов
  }

  inode->i_op = &vtfs_inode_ops;  // Обязательно указываем операции
  inode->i_fop = S_ISDIR(mode) ? &vtfs_dir_ops : NULL;

  return inode;
}

// Функция отмонтирования файловой системы
void vtfs_kill_sb(struct super_block* sb) {
  struct inode* inode;

  kill_litter_super(sb);  // Освобождаем суперблок
  printk(KERN_INFO "VTFS: super block destroyed\n");
}

// PART 3 СТРУКТУРА ДЛЯ ПРОСМОТРА СОЗДАННОГО /mnt/vt.
struct dentry* vtfs_lookup(
    struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag
) {
  const char* name = child_dentry->d_name.name;
  struct vtfs_inode_info* info = parent_inode->i_private;

  // Проверяем, есть ли файл в списке
  struct vtfs_file* file;
  list_for_each_entry(file, &info->children, list) {
    if (!strcmp(file->name, name)) {
      d_add(child_dentry, file->inode);
      return NULL;
    }
  }

  // Если не найдено
  d_add(child_dentry, NULL);
  return NULL;
}

int vtfs_iterate(struct file* filp, struct dir_context* ctx) {
  struct dentry* dentry = filp->f_path.dentry;
  struct inode* inode = dentry->d_inode;
  struct vtfs_inode_info* info = inode->i_private;
  unsigned long offset = ctx->pos;

  if (!info) {
    return -ENOENT;
  }

  // Генерируем стандартные записи ".", ".."
  if (offset == 0) {
    if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR)) {
      return 0;
    }
    ctx->pos++;
  }

  if (offset == 1) {
    if (!dir_emit(ctx, "..", 2, dentry->d_parent->d_inode->i_ino, DT_DIR)) {
      return 0;
    }
    ctx->pos++;
  }

  // Перечисляем дочерние файлы
  struct vtfs_file* file;
  int count = 1;  // "." и ".." занимают первые два слота
  list_for_each_entry(file, &info->children, list) {
    if (count++ < offset) {
      continue;
    }

    if (!dir_emit(
            ctx,
            file->name,
            strlen(file->name),
            file->inode->i_ino,
            S_ISDIR(file->inode->i_mode) ? DT_DIR : DT_REG
        )) {
      return 0;
    }
    ctx->pos++;
  }

  return 0;
}

// ===================================
// Часть 5. Создание и удаление файлов
// ===================================

int vtfs_create(
    struct mnt_idmap* idmap,
    struct inode* parent_inode,
    struct dentry* child_dentry,
    umode_t mode,
    bool b
) {
  const char* name = child_dentry->d_name.name;

  // Создаем новый inode
  struct inode* inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, mode, get_next_ino());
  if (!inode) {
    return -ENOMEM;
  }
  inode->i_mode = S_IFDIR | S_IRWXG | S_IRWXU | S_IRWXO;
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = S_ISDIR(mode) ? &vtfs_dir_ops : NULL;

  // Добавляем новый файл в список дочерних файлов родительского каталога
  struct vtfs_inode_info* info = parent_inode->i_private;
  if (info) {
    struct vtfs_file* file = kmalloc(sizeof(struct vtfs_file), GFP_KERNEL);
    if (!file) {
      iput(inode);
      return -ENOMEM;
    }
    strncpy(file->name, name, 256);
    file->inode = inode;
    list_add(&file->list, &info->children);
  }

  // Связываем dentry с inode
  d_add(child_dentry, inode);

  printk(KERN_INFO "File '%s' was created successfully\n", name);
  return 0;
}

int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry) {
  const char* name = child_dentry->d_name.name;
  struct vtfs_inode_info* info = parent_inode->i_private;
  struct vtfs_file *file, *tmp;

  list_for_each_entry_safe(file, tmp, &info->children, list) {
    if (!strcmp(file->name, name)) {
      list_del(&file->list);  // Удаляем из списка
      iput(file->inode);      // Уменьшаем счетчик ссылок inode
      kfree(file);            // Освобождаем структуру
      printk(KERN_INFO "File '%s' deleted successfully\n", name);
      return 0;
    }
  }

  return -ENOENT;  // Файл не найден
}
