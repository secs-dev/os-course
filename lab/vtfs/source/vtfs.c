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

  if (S_ISDIR(mode)) {
    struct vtfs_inode_info* info = kmalloc(sizeof(struct vtfs_inode_info), GFP_KERNEL);
    if (!info) {
      return NULL;
    }
    INIT_LIST_HEAD(&info->children);
    inode->i_private = info;
  } else {
    inode->i_private = NULL;
  }

  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = S_ISDIR(mode) ? &vtfs_dir_ops : &vtfs_file_ops;

  return inode;
}

// Функция отмонтирования файловой системы
void vtfs_kill_sb(struct super_block* sb) {
  // struct inode* inode;

  // kill_litter_super(sb);  // Освобождаем суперблок
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
  printk(KERN_INFO "vtfs_create: name = %s, mode = 0%o\n", name, mode);

  struct inode* inode =
      vtfs_get_inode(parent_inode->i_sb, parent_inode, mode | S_IFREG, get_next_ino());
  if (!inode) {
    return -ENOMEM;
  }

  // Привязываем содержимое файла
  struct vtfs_file_content* content = kmalloc(sizeof(struct vtfs_file_content), GFP_KERNEL);
  if (!content) {
    return -ENOMEM;
  }
  content->data = NULL;
  content->size = 0;
  inode->i_private = content;

  // Добавляем файл в список
  struct vtfs_inode_info* info = parent_inode->i_private;
  if (info) {
    struct vtfs_file* file = kmalloc(sizeof(struct vtfs_file), GFP_KERNEL);
    if (!file) {
      kfree(content);
      return -ENOMEM;
    }
    strncpy(file->name, name, 256);
    file->inode = inode;
    list_add(&file->list, &info->children);
  }

  d_add(child_dentry, inode);
  printk(KERN_INFO "File '%s' created successfully\n", name);
  return 0;
}

int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry) {
  const char* name = child_dentry->d_name.name;
  struct vtfs_inode_info* info = parent_inode->i_private;
  struct vtfs_file *file, *tmp;

  list_for_each_entry_safe(file, tmp, &info->children, list) {
    if (!strcmp(file->name, name)) {
      // Удаляем содержимое файла, если оно есть
      if (file->inode && file->inode->i_private) {
        struct vtfs_file_content* content = file->inode->i_private;
        kfree(content->data);  // Освобождаем данные
        kfree(content);        // Освобождаем структуру
      }

      list_del(&file->list);  // Удаляем из списка
      kfree(file);            // Освобождаем структуру
      printk(KERN_INFO "File '%s' deleted successfully\n", name);
      return 0;
    }
  }

  return -ENOENT;  // Файл не найден
}

// =====
// MKDIR
// =====
int vtfs_mkdir(
    struct mnt_idmap* idmap, struct inode* parent_inode, struct dentry* child_dentry, umode_t mode
) {
  const char* name = child_dentry->d_name.name;
  printk(KERN_INFO "vtfs_mkdir: name = %s, mode = 0%o\n", name, mode);

  struct inode* inode =
      vtfs_get_inode(parent_inode->i_sb, parent_inode, mode | S_IFDIR, get_next_ino());
  if (!inode) {
    return -ENOMEM;
  }

  struct vtfs_inode_info* info = parent_inode->i_private;
  if (info) {
    struct vtfs_file* dir = kmalloc(sizeof(struct vtfs_file), GFP_KERNEL);
    if (!dir) {
      return -ENOMEM;
    }
    strncpy(dir->name, name, 256);
    dir->inode = inode;
    list_add(&dir->list, &info->children);
  }

  d_add(child_dentry, inode);
  printk(KERN_INFO "Directory '%s' created successfully\n", name);
  return 0;
}

// ======
// RMDIR
// ======
int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry) {
  const char* name = child_dentry->d_name.name;  // Получаем имя удаляемой директории
  struct vtfs_inode_info* info = parent_inode->i_private;
  struct vtfs_file *dir, *tmp;

  // Проверяем, существует ли директория в списке дочерних элементов
  list_for_each_entry_safe(dir, tmp, &info->children, list) {
    if (!strcmp(dir->name, name)) {
      // Проверяем, пуста ли директория
      struct vtfs_inode_info* dir_info = dir->inode->i_private;
      if (!list_empty(&dir_info->children)) {
        printk(KERN_ERR "Directory '%s' is not empty\n", name);
        return -ENOTEMPTY;
      }

      // Удаляем директорию из списка
      list_del(&dir->list);
      kfree(dir);  // Освобождаем память
      printk(KERN_INFO "Directory '%s' deleted successfully\n", name);
      return 0;
    }
  }

  printk(KERN_ERR "Directory '%s' not found\n", name);
  return -ENOENT;  // Директория не найдена
}

// =========================
// Реализация функции чтения
// =========================
ssize_t vtfs_read(struct file* filp, char __user* buffer, size_t len, loff_t* offset) {
  printk(KERN_INFO "vtfs_read: called with len=%zu, offset=%lld\n", len, *offset);
  struct vtfs_file_content* content = filp->private_data;

  if (!content || *offset > content->size) {
    printk(KERN_ERR "vtfs_read: offset too much for file size\n");
    return 0;  // Конец файла или нет данных
  }

  size_t available = content->size - *offset;
  printk(KERN_ERR "vtfs_read: available founded\n");
  size_t to_read = min(len, available);
  printk(KERN_ERR "vtfs_read: min founded\n");

  if (copy_to_user(buffer, content->data + *offset, to_read)) {
    printk(KERN_ERR "vtfs_read: copy_to_user failed\n");
    return -EFAULT;
  }

  *offset += to_read;
  printk(KERN_INFO "vtfs_read: read %zu bytes, new offset=%lld\n", to_read, *offset);
  return to_read;
}

ssize_t vtfs_write(struct file* filp, const char __user* buffer, size_t len, loff_t* offset) {
  printk(KERN_INFO "vtfs_write: called with len=%zu, offset=%lld\n", len, *offset);
  struct vtfs_file_content* content = filp->private_data;

  if (!content) {
    content = kmalloc(sizeof(struct vtfs_file_content), GFP_KERNEL);
    if (!content) {
      return -ENOMEM;  // Ошибка выделения памяти
    }
    content->data = NULL;
    content->size = 0;
    filp->private_data = content;
    printk(KERN_INFO "vtfs_write: private_data initialized\n");
  }

  // Расширяем буфер, если необходимо
  if (*offset + len > content->size) {
    char* new_data = krealloc(content->data, *offset + len, GFP_KERNEL);
    if (!new_data) {
      return -ENOMEM;  // Ошибка выделения памяти
    }
    content->data = new_data;
    content->size = *offset + len;
  }

  if (copy_from_user(content->data + *offset, buffer, len)) {
    return -EFAULT;  // Ошибка копирования из user-space
  }

  *offset += len;
  printk(KERN_INFO "vtfs_write: wrote %zu bytes, new size=%zu\n", len, content->size);
  return len;
}
