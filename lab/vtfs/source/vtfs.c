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


// Структура записи 'файла' виртуальной файловой системы
struct vtfs_entry {
  struct list_head  list_entry;
  char              vtfs_entry_name[MAX_NAME];
  struct dentry*    vtfs_entry_dentry;
  int               vtfs_inode_ino;
  umode_t           vtfs_inode_mode;
  size_t            vtfs_inode_size;
  ino_t             vtfs_entry_parent_ino;
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
  new_vtfs_entry->vtfs_inode_mode = mode | S_IRWXU | S_IRWXG | S_IRWXO;

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
  return 0;
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
  .iterate = vtfs_iterate,
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
