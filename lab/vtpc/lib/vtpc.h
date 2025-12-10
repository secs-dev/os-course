#ifndef VTPC_H
#define VTPC_H

#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>

/* Virtual Page Cache API (LRU implementation) */

/* Открытие файла с использованием LRU кэша */
int vtpc_open(const char *path, int flags, mode_t mode);

/* Закрытие файла и сброс кэшированных страниц */
int vtpc_close(int fd);

/* Чтение данных из файла через кэш */
ssize_t vtpc_read(int fd, void *buf, size_t count);

/* Запись данных в файл через кэш */
ssize_t vtpc_write(int fd, const void *buf, size_t count);

/* Перемещение файлового указателя */
off_t vtpc_lseek(int fd, off_t offset, int whence);

/* Синхронизация кэшированных данных с диском */
int vtpc_fsync(int fd);

/* Дополнительные функции для получения статистики */
void vtpc_get_stats(unsigned long *hits, unsigned long *misses, double *hit_rate);
int vtpc_get_cache_size(void);
int vtpc_get_cache_usage(void);

#endif /* VTPC_H */