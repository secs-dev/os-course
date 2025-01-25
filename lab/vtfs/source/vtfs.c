#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/list.h>

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)
#define MAX_NAME 64
#define PAGESIZE 8
#define PGROUNDDOWN(a) (((a)) & ~(PAGESIZE - 1))

// Структура записи 'файла' виртуальной файловой системы
struct vtfs_entry {
  struct list_head  list_entry;
  char              vtfs_entry_name[MAX_NAME];
  struct dentry*    vtfs_entry_dentry;
  int               vtfs_inode_ino;
  umode_t           vtfs_inode_mode;
  size_t            vtfs_inode_size;
  size_t            vtfs_entry_count_of_pages;
  ino_t             vtfs_entry_parent_ino;
  struct list_head  vtfs_entry_data;
};

struct vtfs_entry_page{
  struct list_head  list_data;
  char              data[PAGESIZE];
};

// Важная информация:
int next_inode_number = 1000;
struct super_block* vtfs_sb;
struct list_head entries;

struct inode* vtfs_get_inode(
  struct super_block* sb, 
  const struct inode* dir, 
  umode_t mode, 
  int i_ino
);
//TODO Зачем этот вызов необходим
struct dentry* vtfs_lookup(
  struct inode* parent_inode,  // родительская нода
  struct dentry* child_dentry, // объект, к которому мы пытаемся получить доступ
  unsigned int flag            // неиспользуемое значение
){
  struct vtfs_entry*  current_entry;
  struct list_head*   position;
  int count_of_iterations = 0;

  list_for_each((position), &entries){
    current_entry = (struct vtfs_entry *) position;
    printk(KERN_INFO "vtfs_lookup: Итерация №%d\n", count_of_iterations++);
    printk(KERN_INFO "vtfs_lookup: ino = %d\n", current_entry->vtfs_inode_ino);
  }
  return NULL;
}

int vtfs_create(
  struct user_namespace* uname_space,
  struct inode* parent_inode,
  struct dentry* child_dentry,
  umode_t mode,
  bool b
){
  struct inode*      new_inode;
  struct vtfs_entry* new_vtfs_entry; 
  if(strlen(child_dentry->d_name.name) >= MAX_NAME){
    printk(KERN_ERR "Имя файла слишком большое\n");
    return -ENOMEM;
  }
  //TODO Пока что у файла нет содержимого
  if((new_vtfs_entry = kmalloc(sizeof(struct vtfs_entry), GFP_KERNEL)) == 0){
    printk(KERN_ERR "Не удалось выделить память под новую запись файла\n");
    return -ENOMEM;
  }

  //TODO == 0 такого не может быть 
  if((new_inode = vtfs_get_inode(
    vtfs_sb,
    parent_inode,
    mode | S_IRWXU | S_IRWXG | S_IRWXO,
    next_inode_number++
  )) == 0) {
    printk(KERN_ERR "Не удалось создать новый inode\n");
    kfree(new_vtfs_entry);
    return -ENOMEM;
  }
  // Осталось заполнить структуру, добавить новый файл в лист и сделать d_add
  list_add(&(new_vtfs_entry->list_entry), &entries);
  new_vtfs_entry->vtfs_entry_dentry = child_dentry;
  new_vtfs_entry->vtfs_inode_ino = next_inode_number - 1;
  strcpy(new_vtfs_entry->vtfs_entry_name, child_dentry->d_name.name);
  new_vtfs_entry->vtfs_entry_parent_ino = parent_inode->i_ino;
  new_vtfs_entry->vtfs_inode_size = 0;
  new_vtfs_entry->vtfs_entry_count_of_pages = 0;
  new_vtfs_entry->vtfs_inode_mode = mode | S_IRWXU | S_IRWXG | S_IRWXO;
  INIT_LIST_HEAD(&(new_vtfs_entry->vtfs_entry_data));

  d_add(child_dentry, new_inode);
  return 0;
}

int vtfs_unlink(
  struct inode* parent_inode,
  struct dentry* child_dentry
){
  struct vtfs_entry*  current_entry;
  struct list_head*   position;

  list_for_each((position), &entries){
    current_entry = (struct vtfs_entry*) position;
    if(current_entry->vtfs_inode_ino == child_dentry->d_inode->i_ino){
      printk(KERN_INFO "vtfs_unlink: Файл %s удалён\n", child_dentry->d_name.name);
      list_del(position);
      kfree(position);
      while(!list_empty(&(current_entry->vtfs_entry_data))){
        struct list_head* to_delete_page = current_entry->vtfs_entry_data.next;
        list_del(to_delete_page);
        kfree(to_delete_page);
      }
      return 0;
    }
  }
  printk(KERN_ERR "vtfs_unlink: Файл %s не найден\n", child_dentry->d_name.name);
  return -ENOENT;
}

int vtfs_mkdir(
  struct user_namespace*  uname_space,
  struct inode*           parent_inode,
  struct dentry*          child_dentry,
  umode_t mode
){
  if(vtfs_create(
    uname_space,
    parent_inode,
    child_dentry,
    mode | S_IFDIR,
    0
  ) != 0){
    printk("Не удалось создать директорию %s\n", child_dentry->d_name.name);
    return -ENOMEM;
  }
  printk("Директория %s создана\n", child_dentry->d_name.name);
  return 0;
}

int vtfs_rmdir(
  struct inode*   parent_inode,
  struct dentry*  child_dentry
){
  struct vtfs_entry*  current_entry;
  struct list_head*   position;
  struct vtfs_entry*  dir_position = 0;
  ino_t dir_ino = child_dentry->d_inode->i_ino;

  list_for_each((position), &entries){
    current_entry = (struct vtfs_entry*) position;
    if(current_entry->vtfs_entry_parent_ino == dir_ino){
      printk(KERN_ERR "vtfs_rmdir: директория %s не пуста\n", child_dentry->d_name.name);
      // TODO Код ошибки должен говорить, что директория не пуста
      return -EISDIR;
    }
    if(current_entry->vtfs_inode_ino == dir_ino){
      dir_position = current_entry;
    }
  }
  if(dir_position == 0){
    printk(KERN_ERR "vtfs_rmdir: директория %s не найдена\n", child_dentry->d_name.name);
    return -ENOENT;
  }
  list_del((struct list_head*) dir_position);
  kfree(dir_position);
  printk(KERN_ERR "vtfs_rmdir: директория %s удалена\n", child_dentry->d_name.name);
  return 0;
}

ssize_t vtfs_read(
  struct file* filp,
  char* buffer,
  size_t len,
  loff_t *offset
){
  struct vtfs_entry*      current_entry;
  struct list_head*       position;
  struct vtfs_entry_page* current_page;
  struct list_head*       current_page_position;
  int                     current_page_index = -1;
  ino_t                   entry_ino = filp->f_inode->i_ino;
  ssize_t bytes_read = 0;
  int start_pagenumber  = PGROUNDDOWN(*offset);
  int start_pageindex   = start_pagenumber / PAGESIZE;
  int end_pagenumber    = PGROUNDDOWN(*offset + len - 1);
  int end_pageindex     = end_pagenumber / PAGESIZE;
  list_for_each((position), &entries){
    // Ищем файл, откуда читаем
    current_entry = (struct vtfs_entry*) position;
    if(current_entry->vtfs_inode_ino == entry_ino){
      printk(KERN_INFO "Считывание из файла %s, %d байт\n", filp->f_path.dentry->d_name.name, len);
      len = len < current_entry->vtfs_inode_size ? len :current_entry->vtfs_inode_size;
      end_pagenumber    = PGROUNDDOWN(*offset + len - 1);
      end_pageindex     = end_pagenumber / PAGESIZE;
      if(*offset >= current_entry->vtfs_inode_size) return 0;
      if(list_empty(&(current_entry->vtfs_entry_data))) return 0;
      printk("read: %d | %d | %d | %d\n", start_pageindex, end_pageindex, *offset, len);
      
      list_for_each(current_page_position, &(current_entry->vtfs_entry_data)){
          current_page_index++;
          current_page = (struct vtfs_entry_page *)  current_page_position;
          if(current_page_index == start_pageindex){
            // Read one page
            bytes_read = len < PAGESIZE - (*offset) % PAGESIZE ? len : PAGESIZE - (*offset) % PAGESIZE;
            if(copy_to_user(
              buffer,
              current_page->data + (*offset) % PAGESIZE,
              bytes_read
            ) != 0){
              return 0;
              // return -EFAULT;
            }
            printk("File is read by %d bytes from offset %d. current file size = %d\n", bytes_read, *offset, current_entry->vtfs_inode_size);

            *offset += bytes_read;
            return bytes_read;
          }
      }
      
      return bytes_read;
    }
  }
  return -ENOENT;
}

ssize_t vtfs_write(
  struct file* filp,
  const char* buffer,
  size_t len,
  loff_t *offset
){
  struct vtfs_entry*      current_entry;
  struct list_head*       position;
  ino_t                   entry_ino = filp->f_inode->i_ino;
  ssize_t bytes_write = 0;
  struct vtfs_entry_page* new_page;
  // Для навигации по страницам:
  struct vtfs_entry_page* current_page;
  struct list_head*       current_page_position;
  // Для индексации страниц:
  int start_pagenumber  = PGROUNDDOWN(*offset);
  int start_pageindex   = start_pagenumber / PAGESIZE;
  int end_pagenumber    = PGROUNDDOWN(*offset + len - 1);
  int end_pageindex     = end_pagenumber / PAGESIZE;

  list_for_each((position), &entries){
    // Ищем файл, куда пишем
    current_entry = (struct vtfs_entry*) position;
    if(current_entry->vtfs_inode_ino == entry_ino){
      // We found a file
      printk("write: %d | %d | %d | %d\n", end_pageindex, end_pagenumber, *offset, len);
      current_entry->vtfs_inode_size = *offset + len;
      current_entry->vtfs_entry_dentry->d_inode->i_size = *offset + len;
      if(len == 0) return 0;
      if(list_empty(&(current_entry->vtfs_entry_data))){
        if((new_page = kmalloc(sizeof(struct vtfs_entry_page), GFP_KERNEL)) == 0){
          printk(KERN_ERR "Not enough memory to allocate the page\n");
          return len;
        }
        list_add_tail((struct list_head*)new_page, &(current_entry->vtfs_entry_data));
        current_entry->vtfs_entry_count_of_pages++;
        printk("allocate first page for dentry\n");
      }
      int current_page_index = -1;
      if(current_entry->vtfs_entry_count_of_pages <= end_pageindex){
        // Необходимо аллоцировать страцницы
        list_for_each(current_page_position, &(current_entry->vtfs_entry_data)){
          current_page_index++;
          current_page = (struct vtfs_entry_page *)  current_page_position;
          if(list_is_last(current_page_position, &(current_entry->vtfs_entry_data))){
            if(current_page_index < end_pageindex){
              if((new_page = kmalloc(sizeof(struct vtfs_entry_page), GFP_KERNEL)) == 0){
                printk(KERN_ERR "Not enough memory to allocate the page\n");
                return len;
              }
              list_add_tail((struct list_head*)new_page, &(current_entry->vtfs_entry_data));
              current_entry->vtfs_entry_count_of_pages++;
              printk("allocate page #%d for dentry\n", current_page_index + 1);
            }
          }
        }
      }
      current_page_index = -1;
      list_for_each(current_page_position, &(current_entry->vtfs_entry_data)){
          current_page_index++;
          current_page = (struct vtfs_entry_page *)  current_page_position;
          if (current_page_index == start_pageindex){
            bytes_write = len < PAGESIZE - (*offset) % PAGESIZE ? len : PAGESIZE - (*offset) % PAGESIZE;
            if(copy_from_user(
              current_page->data + (*offset) % PAGESIZE,
              buffer,
              bytes_write
            ) != 0){
              return 0;
              // return -EFAULT;
            }
            printk("File is written by %d bytes from offset %d. current file size = %d\n", bytes_write, *offset, current_entry->vtfs_inode_size);
            *offset += bytes_write;
            return bytes_write;
          } 
      }
      return len;
    }
  }
  return len;
}

int vtfs_iterate(struct file* file, struct dir_context* ctx) {
  struct dentry*  dentry = file->f_path.dentry;
  struct inode*   parent_inode = dentry->d_inode;
  ino_t           ino = parent_inode->i_ino;
  struct vtfs_entry*  current_entry;
  struct list_head*   position;
  int count_of_iterations = 0;
  unsigned type;
  printk("vtfs_iterate: ino = %ld\n", ino);

  if(!dir_emit_dots(file, ctx)){
    return 0;
  }

  if(ctx->pos >= 3) return ctx->pos;

  list_for_each((position), &entries){
    printk(KERN_INFO "vtfs_iterate: Итерация №%d, в директории: %s\n", count_of_iterations++, dentry->d_name.name);
    current_entry = (struct vtfs_entry * ) position;
    if(S_ISDIR(current_entry->vtfs_inode_mode)) type = DT_DIR;
    else if(S_ISREG(current_entry->vtfs_inode_mode)) type = DT_REG;
    else type = DT_UNKNOWN;
    if(current_entry->vtfs_entry_parent_ino == ino){
      if(!dir_emit(
        ctx,
        current_entry->vtfs_entry_name,
        strlen(current_entry->vtfs_entry_name),
        current_entry->vtfs_inode_ino,
        type
      )){
        //TODO Поставить нормальные выводы ошибок
        return -ENOMEM;
      }
      ctx->pos++;
    }
  }
  //TODO итого 4
  return ctx->pos;
}

struct inode_operations vtfs_inode_ops = {
  .lookup = vtfs_lookup,
  .create = vtfs_create,
  .unlink = vtfs_unlink,
  .mkdir  = vtfs_mkdir,
  .rmdir  = vtfs_rmdir
};

struct file_operations vtfs_dir_ops = {
  .iterate  = vtfs_iterate,
  .read     = vtfs_read,
  .write    = vtfs_write
};

void vtfs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "vtfs super block уничтожен. Unmount прошел успешно.\n");
}

struct inode* vtfs_get_inode(
  struct super_block* sb, 
  const struct inode* dir, 
  umode_t mode, 
  int i_ino
) {
  struct inode *inode = new_inode(sb);
  if (inode != NULL) {
    inode_init_owner(sb->s_user_ns, inode, dir, mode);
  }

  inode->i_ino = i_ino;
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;
  inode->i_mode = mode;
  inc_nlink(inode);
  return inode;
}

int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
  struct inode* inode;
  if((inode = vtfs_get_inode(sb, NULL, S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO, next_inode_number++)) == 0){
    printk(KERN_ERR "Не удалось создать ноду root\n");
    return -ENOMEM;
  }

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    printk(KERN_ERR "Не удалось создать root dentry\n");
    return -ENOMEM;
  }

  vtfs_sb = sb;
  INIT_LIST_HEAD(&entries);
  printk(KERN_INFO "vtfs fill super прошел успешно.\nИмя супер блока: %s\n", sb->s_root->d_name.name);
  return 0;
}

struct dentry* vtfs_mount(
  struct file_system_type* fs_type,
  int flags,
  const char* token,
  void* data
) {
  struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (ret == NULL) {
    printk(KERN_ERR "Не удалось монтировать файловую систему\n");
  } else {
    printk(KERN_INFO "Монтировка файловой системы прошла успешно\n");
  }
  return ret;
}

struct file_system_type vtfs_fs_type = {
  .name = "vtfs",
  .mount = vtfs_mount,
  .kill_sb = vtfs_kill_sb
};


static int __init vtfs_init(void) {
  LOG("VTFS joined the kernel\n");
  return register_filesystem(&vtfs_fs_type);
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
  LOG("VTFS left the kernel\n");
}


module_init(vtfs_init);
module_exit(vtfs_exit);
