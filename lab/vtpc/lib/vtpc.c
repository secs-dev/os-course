#include "vtpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/* Конфигурация кэша */
#define CACHE_SIZE 100           /* Количество страниц в кэше */
#define PAGE_SIZE 4096           /* Размер страницы (байт) */
#define HASH_TABLE_SIZE 101      /* Простое число для хэш-таблицы */

/* Структуры данных LRU кэша */
typedef struct CachePage {
    int fd;                     /* Дескриптор файла */
    off_t page_number;          /* Номер страницы (offset / PAGE_SIZE) */
    unsigned char data[PAGE_SIZE]; /* Данные страницы */
    int is_dirty;               /* Флаг "грязной" страницы (изменена) */
    int is_valid;               /* Флаг валидности страницы */
    time_t last_used;           /* Время последнего использования (для LRU) */
    
    /* Для двусвязного списка LRU */
    struct CachePage *prev;
    struct CachePage *next;
    
    /* Для разрешения коллизий в хэш-таблице */
    struct CachePage *hash_next;
} CachePage;

/* Хэш-таблица для быстрого поиска страниц */
typedef struct {
    CachePage **buckets;  // Массив указателей на цепочки коллизий
    int size;             // Размер таблицы
} HashTable;

/* Структура кэша */
typedef struct {
    CachePage *pages;           /* Массив страниц */
    CachePage *lru_head;        /* Голова списка (самая новая) */
    CachePage *lru_tail;        /* Хвост списка (самая старая) */
    HashTable hash_table;       /* Хэш-таблица для поиска */
    int capacity;               /* Вместимость кэша */
    int count;                  /* Текущее количество страниц */
    
    /* Статистика */
    unsigned long hits;         /* Попадания в кэш */
    unsigned long misses;       /* Промахи кэша */
    
    /* Блокировка для потокобезопасности */
    pthread_mutex_t lock;
} LRUCache;

/* Глобальный кэш (инициализируется при первом открытии файла) */
static LRUCache cache = {0};
static int cache_initialized = 0;

/* ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==================== */

/* Хэш-функция: преобразует (fd, номер_страницы) в индекс хэш-таблицы */
static unsigned int hash_function(int fd, off_t page_number) {
    return (unsigned int)((fd * 31 + page_number) % HASH_TABLE_SIZE);
}

/* Инициализация кэша (вызывается один раз при первом vtpc_open) */
static void cache_init(void) {
    if (cache_initialized) return;
    
    cache.capacity = CACHE_SIZE;
    cache.count = 0;
    cache.hits = 0;
    cache.misses = 0;
    cache.lru_head = NULL;
    cache.lru_tail = NULL;
    
    /* Выделяем память для массива страниц */
    cache.pages = (CachePage*)calloc(cache.capacity, sizeof(CachePage));
    if (!cache.pages) {
        perror("Failed to allocate cache pages");
        exit(EXIT_FAILURE);
    }
    
    /* Инициализация хэш-таблицы */
    cache.hash_table.size = HASH_TABLE_SIZE;
    cache.hash_table.buckets = (CachePage**)calloc(HASH_TABLE_SIZE, sizeof(CachePage*));
    if (!cache.hash_table.buckets) {
        perror("Failed to allocate hash table");
        free(cache.pages);
        exit(EXIT_FAILURE);
    }
    
    /* Инициализация мьютекса */
    pthread_mutex_init(&cache.lock, NULL);
    
    cache_initialized = 1;
    
    printf("[VTPC] LRU cache initialized: %d pages (%d KB)\n", 
           CACHE_SIZE, CACHE_SIZE * PAGE_SIZE / 1024);
}

/* Поиск страницы в кэше через хэш-таблицу */
static CachePage* cache_find(int fd, off_t page_number) {
    unsigned int bucket = hash_function(fd, page_number);
    CachePage *page = cache.hash_table.buckets[bucket];
    
    while (page) {
        if (page->is_valid && page->fd == fd && page->page_number == page_number) {
            return page;  // Страница найдена
        }
        page = page->hash_next;
    }
    
    return NULL;  // Страница не найдена
}

/* Обновление страницы в LRU списке (перемещение в начало) */
static void cache_touch(CachePage *page) {
    if (!page || page == cache.lru_head) return;
    
    /* Удаляем из текущей позиции в списке */
    if (page->prev) page->prev->next = page->next;
    if (page->next) page->next->prev = page->prev;
    
    /* Обновляем хвост, если нужно */
    if (page == cache.lru_tail) {
        cache.lru_tail = page->prev;
    }
    
    /* Вставляем в начало списка */
    page->prev = NULL;
    page->next = cache.lru_head;
    
    if (cache.lru_head) {
        cache.lru_head->prev = page;
    }
    
    cache.lru_head = page;
    
    if (!cache.lru_tail) {
        cache.lru_tail = page;
    }
    
    page->last_used = time(NULL);  // Обновляем время доступа
}

/* Получение кандидата на вытеснение (самая старая страница или невалидная) */
static CachePage* cache_get_victim(void) {
    /* Сначала ищем невалидные (свободные) страницы */
    for (int i = 0; i < cache.capacity; i++) {
        if (!cache.pages[i].is_valid) {
            return &cache.pages[i];
        }
    }
    
    /* Если все страницы валидны, берём самую старую (хвост LRU списка) */
    return cache.lru_tail;
}

/* Удаление страницы из кэша (при вытеснении) */
static void cache_remove(CachePage *page) {
    if (!page || !page->is_valid) return;
    
    /* Если страница "грязная", выводим предупреждение */
    if (page->is_dirty) {
        printf("[VTPC] Warning: evicting dirty page (fd=%d, page=%ld)\n", 
               page->fd, (long)page->page_number);
    }
    
    /* Удаляем из хэш-таблицы */
    unsigned int bucket = hash_function(page->fd, page->page_number);
    CachePage **pp = &cache.hash_table.buckets[bucket];
    
    while (*pp) {
        if (*pp == page) {
            *pp = page->hash_next;
            break;
        }
        pp = &(*pp)->hash_next;
    }
    
    /* Удаляем из LRU списка */
    if (page->prev) page->prev->next = page->next;
    if (page->next) page->next->prev = page->prev;
    
    if (page == cache.lru_head) cache.lru_head = page->next;
    if (page == cache.lru_tail) cache.lru_tail = page->prev;
    
    /* Очищаем страницу */
    page->is_valid = 0;
    page->fd = -1;
    page->is_dirty = 0;
    page->hash_next = NULL;
    page->prev = NULL;
    page->next = NULL;
    
    cache.count--;
}

/* Добавление страницы в кэш (с вытеснением при необходимости) */
static CachePage* cache_put(int fd, off_t page_number, const unsigned char *data, int is_dirty) {
    pthread_mutex_lock(&cache.lock);  // Входим в критическую секцию
    
    /* Проверяем, не существует ли страница уже в кэше */
    CachePage *page = cache_find(fd, page_number);
    
    if (page) {
        /* Страница уже есть - обновляем данные */
        memcpy(page->data, data, PAGE_SIZE);
        page->is_dirty = is_dirty;
        page->last_used = time(NULL);
        cache_touch(page);  // Обновляем позицию в LRU
        cache.hits++;       // Увеличиваем счётчик попаданий
    } else {
        /* Нужно добавить новую страницу */
        if (cache.count >= cache.capacity) {
            /* Кэш полон - вытесняем самую старую страницу */
            CachePage *victim = cache_get_victim();
            if (victim) {
                cache_remove(victim);
            }
        }
        
        /* Ищем свободную страницу в массиве */
        for (int i = 0; i < cache.capacity; i++) {
            if (!cache.pages[i].is_valid) {
                page = &cache.pages[i];
                break;
            }
        }
        
        if (!page) {
            /* Не должно происходить, но на всякий случай */
            pthread_mutex_unlock(&cache.lock);
            return NULL;
        }
        
        /* Заполняем страницу данными */
        page->fd = fd;
        page->page_number = page_number;
        memcpy(page->data, data, PAGE_SIZE);
        page->is_dirty = is_dirty;
        page->is_valid = 1;
        page->last_used = time(NULL);
        
        /* Добавляем в хэш-таблицу */
        unsigned int bucket = hash_function(fd, page_number);
        page->hash_next = cache.hash_table.buckets[bucket];
        cache.hash_table.buckets[bucket] = page;
        
        /* Добавляем в начало LRU списка */
        page->prev = NULL;
        page->next = cache.lru_head;
        
        if (cache.lru_head) {
            cache.lru_head->prev = page;
        }
        
        cache.lru_head = page;
        
        if (!cache.lru_tail) {
            cache.lru_tail = page;
        }
        
        cache.count++;
        cache.misses++;  // Увеличиваем счётчик промахов
    }
    
    pthread_mutex_unlock(&cache.lock);
    return page;
}

/* Чтение страницы из кэша или с диска */
static int cache_read_page(int fd, off_t offset, unsigned char *buffer) {
    off_t page_number = offset / PAGE_SIZE;
    off_t page_offset = page_number * PAGE_SIZE;
    
    pthread_mutex_lock(&cache.lock);
    CachePage *page = cache_find(fd, page_number);
    
    if (page) {
        /* Страница в кэше - копируем данные */
        memcpy(buffer, page->data, PAGE_SIZE);
        cache_touch(page);  /* Обновляем LRU */
        cache.hits++;       /* Попадание в кэш */
        pthread_mutex_unlock(&cache.lock);
        return 1;  /* Cache hit */
    }
    
    pthread_mutex_unlock(&cache.lock);
    
    /* Определяем физический размер файла на данный момент */
    off_t current_size = lseek(fd, 0, SEEK_END);
    off_t saved_pos = lseek(fd, 0, SEEK_CUR); /* Сохраняем текущую позицию */
    
    /* Вычисляем, сколько байтов можно прочитать для этой страницы */
    size_t bytes_to_read = PAGE_SIZE;
    if (page_offset >= current_size) {
        /* За пределами файла - заполняем нулями */
        memset(buffer, 0, PAGE_SIZE);
    } else if (page_offset + PAGE_SIZE > current_size) {
        /* Частично за пределами файла */
        bytes_to_read = current_size - page_offset;
        ssize_t bytes_read = pread(fd, buffer, bytes_to_read, page_offset);
        if (bytes_read < 0) {
            lseek(fd, saved_pos, SEEK_SET); /* Восстанавливаем позицию */
            return -1;  /* Ошибка чтения */
        }
        /* Дополняем нулями остаток страницы */
        if (bytes_read < PAGE_SIZE) {
            memset(buffer + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
    } else {
        /* Полностью внутри файла */
        ssize_t bytes_read = pread(fd, buffer, PAGE_SIZE, page_offset);
        if (bytes_read < 0) {
            lseek(fd, saved_pos, SEEK_SET); /* Восстанавливаем позицию */
            return -1;  /* Ошибка чтения */
        }
        if (bytes_read < PAGE_SIZE) {
            memset(buffer + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
    }
    
    lseek(fd, saved_pos, SEEK_SET); /* Восстанавливаем позицию */
    
    /* Сохраняем прочитанную страницу в кэш */
    cache_put(fd, page_number, buffer, 0);  /* is_dirty = 0 */
    
    return 0;  /* Cache miss */
}
/* ==================== ТАБЛИЦА ОТКРЫТЫХ ФАЙЛОВ ==================== */

/* Структура для отслеживания открытых файлов */
typedef struct {
    int is_used;      /* Флаг использования */
    int real_fd;      /* Реальный дескриптор файла (от open) */
    off_t position;   /* Текущая позиция в файле (как у lseek) */
    off_t logical_size; /* Логический размер файла (с учетом незаписанных данных) */
    char path[256];   /* Путь к файлу */
} FileEntry;

#define MAX_OPEN_FILES 1024
static FileEntry open_files[MAX_OPEN_FILES] = {0};

/* Получение реального дескриптора по внутреннему */
static int get_real_fd(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].is_used) {
        errno = EBADF;  // Bad file descriptor
        return -1;
    }
    return open_files[fd].real_fd;
}

/* Инициализация записи о файле */
static void init_file_entry(int fd, int real_fd, const char *path) {
    if (fd >= 0 && fd < MAX_OPEN_FILES) {
        open_files[fd].is_used = 1;
        open_files[fd].real_fd = real_fd;
        open_files[fd].position = 0;
        
        /* СОХРАНЯЕМ текущую позицию */
        off_t old_pos = lseek(real_fd, 0, SEEK_CUR);
        
        /* Получаем текущий физический размер файла */
        off_t current_size = lseek(real_fd, 0, SEEK_END);
        open_files[fd].logical_size = (current_size > 0) ? current_size : 0;
        
        /* ВОССТАНАВЛИВАЕМ позицию */
        lseek(real_fd, old_pos, SEEK_SET);
        
        if (path) {
            strncpy(open_files[fd].path, path, sizeof(open_files[fd].path) - 1);
            open_files[fd].path[sizeof(open_files[fd].path) - 1] = '\0';
        }
    }
}

/* ==================== ОСНОВНЫЕ API ФУНКЦИИ ==================== */

/* Открытие файла с использованием LRU кэша */
int vtpc_open(const char *path, int flags, mode_t mode) {
    if (!cache_initialized) {
        cache_init();  // Инициализируем кэш при первом вызове
    }
    
    /* Открываем файл с переданными флагами и режимом */
    int real_fd = open(path, flags, mode);
    if (real_fd < 0) {
        return -1;
    }
    
    /* Ищем свободный слот в таблице файлов */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].is_used) {
            init_file_entry(i, real_fd, path);
            return i;  /* Возвращаем внутренний дескриптор */
        }
    }
    
    close(real_fd);
    errno = EMFILE; /* Too many open files */
    return -1;
}

/* Закрытие файла и сброс кэша */
int vtpc_close(int fd) {
    int real_fd = get_real_fd(fd);
    if (real_fd < 0) {
        return -1;
    }
    
    /* Сбрасываем все "грязные" страницы этого файла на диск */
    pthread_mutex_lock(&cache.lock);
    
    for (int i = 0; i < cache.capacity; i++) {
        CachePage *page = &cache.pages[i];
        if (page->is_valid && page->fd == real_fd && page->is_dirty) {
            /* Записываем страницу на диск */
            ssize_t written = pwrite(real_fd, page->data, PAGE_SIZE, 
                                    page->page_number * PAGE_SIZE);
            if (written == PAGE_SIZE) {
                page->is_dirty = 0;
            }
        }
    }
    
    pthread_mutex_unlock(&cache.lock);
    
    /* Закрываем реальный файл и освобождаем запись */
    close(real_fd);
    open_files[fd].is_used = 0;
    
    return 0;
}

/* Чтение из файла через кэш */
ssize_t vtpc_read(int fd, void *buf, size_t count) {
    int real_fd = get_real_fd(fd);
    if (real_fd < 0) {
        return -1;
    }
    
    FileEntry *file = &open_files[fd];
    off_t current_pos = file->position;
    unsigned char *buffer = (unsigned char *)buf;
    ssize_t total_read = 0;
    
    /* Используем логический размер файла */
    off_t file_size = file->logical_size;
    
    printf("[DEBUG] vtpc_read: fd=%d, pos=%ld, count=%zu, logical_size=%ld\n", 
           fd, (long)current_pos, count, (long)file_size);
    
    if (current_pos >= file_size) {
        printf("[DEBUG] EOF: current_pos=%ld >= file_size=%ld\n", 
               (long)current_pos, (long)file_size);
        return 0;  /* Конец файла */
    }
    
    /* Ограничиваем чтение доступным размером файла */
    if (count > file_size - current_pos) {
        printf("[DEBUG] Reducing count: %zu -> %zu\n", count, file_size - current_pos);
        count = file_size - current_pos;
    }
    
    /* Читаем постранично */
    unsigned char page_buffer[PAGE_SIZE];
    
    while (count > 0) {
        off_t page_number = current_pos / PAGE_SIZE;
        size_t offset_in_page = current_pos % PAGE_SIZE;
        size_t bytes_in_page = PAGE_SIZE - offset_in_page;
        size_t bytes_to_read = (count < bytes_in_page) ? count : bytes_in_page;
        
        printf("[DEBUG] Reading: page=%ld, offset_in_page=%zu, bytes_to_read=%zu\n",
               (long)page_number, offset_in_page, bytes_to_read);
        
        /* Читаем страницу (из кэша или с диска) */
        int cache_result = cache_read_page(real_fd, current_pos, page_buffer);
        if (cache_result < 0) {
            /* Ошибка чтения */
            printf("[DEBUG] cache_read_page error: %d\n", cache_result);
            if (total_read == 0) return -1;
            break;
        }
        
        /* Копируем нужную часть страницы в буфер пользователя */
        memcpy(buffer, page_buffer + offset_in_page, bytes_to_read);
        
        /* Обновляем счётчики */
        buffer += bytes_to_read;
        current_pos += bytes_to_read;
        total_read += bytes_to_read;
        count -= bytes_to_read;
        
        printf("[DEBUG] Progress: total_read=%ld, remaining=%zu\n",
               (long)total_read, count);
    }
    
    file->position = current_pos;
    printf("[DEBUG] vtpc_read completed: total_read=%ld\n", (long)total_read);
    return total_read;
}

/* Запись в файл через кэш */
ssize_t vtpc_write(int fd, const void *buf, size_t count) {
    int real_fd = get_real_fd(fd);
    if (real_fd < 0) {
        return -1;
    }
    
    FileEntry *file = &open_files[fd];
    off_t current_pos = file->position;
    const unsigned char *buffer = (const unsigned char *)buf;
    ssize_t total_written = 0;
    
    /* Выровненный буфер для работы со страницами */
    unsigned char page_buffer[PAGE_SIZE];
    
    /* Пишем постранично */
    while (count > 0) {
        off_t page_number = current_pos / PAGE_SIZE;
        off_t page_offset = page_number * PAGE_SIZE;
        size_t offset_in_page = current_pos % PAGE_SIZE;
        size_t bytes_in_page = PAGE_SIZE - offset_in_page;
        size_t bytes_to_write = (count < bytes_in_page) ? count : bytes_in_page;
        
        /* Читаем текущую страницу (если нужно) */
        int cache_result = cache_read_page(real_fd, current_pos, page_buffer);
        if (cache_result < 0) {
            /* Ошибка чтения */
            if (total_written == 0) return -1;
            break;
        }
        
        /* Модифицируем страницу новыми данными */
        memcpy(page_buffer + offset_in_page, buffer, bytes_to_write);
        
        /* ЗАПИСЫВАЕМ ТОЛЬКО НУЖНЫЕ БАЙТЫ НА ДИСК (без выравнивания) */
        ssize_t written = pwrite(real_fd, buffer, bytes_to_write, current_pos);
        if (written != bytes_to_write) {
            if (total_written == 0) return -1;
            break;
        }
        
        /* Сохраняем страницу в кэш как чистую (так как уже записали на диск) */
        cache_put(real_fd, page_number, page_buffer, 0); /* is_dirty = 0 */
        
        /* Обновляем счётчики */
        buffer += bytes_to_write;
        current_pos += bytes_to_write;
        total_written += bytes_to_write;
        count -= bytes_to_write;
    }
    
    /* Обновляем логический размер файла */
    if (current_pos > file->logical_size) {
        file->logical_size = current_pos;
    }
    
    file->position = current_pos;
    return total_written;
}

/* Перемещение указателя в файле */
off_t vtpc_lseek(int fd, off_t offset, int whence) {
    int real_fd = get_real_fd(fd);
    if (real_fd < 0) {
        return -1;
    }
    
    FileEntry *file = &open_files[fd];
    off_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case SEEK_END:
            {
                /* Определяем размер файла */
                off_t old_pos = lseek(real_fd, 0, SEEK_CUR);
                off_t file_size = lseek(real_fd, 0, SEEK_END);
                lseek(real_fd, old_pos, SEEK_SET);
                new_pos = file_size + offset;
            }
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    
    if (new_pos < 0) {
        errno = EINVAL;
        return -1;
    }
    
    file->position = new_pos;
    return new_pos;
}

/* Синхронизация данных кэша с диском */
int vtpc_fsync(int fd) {
    int real_fd = get_real_fd(fd);
    if (real_fd < 0) {
        return -1;
    }
    
    pthread_mutex_lock(&cache.lock);
    
    int flushed = 0;
    
    /* Сбрасываем все "грязные" страницы этого файла на диск */
    for (int i = 0; i < cache.capacity; i++) {
        CachePage *page = &cache.pages[i];
        if (page->is_valid && page->fd == real_fd && page->is_dirty) {
            /* Записываем всю страницу на диск */
            ssize_t written = pwrite(real_fd, page->data, PAGE_SIZE, 
                                    page->page_number * PAGE_SIZE);
            if (written == PAGE_SIZE) {
                page->is_dirty = 0;
                flushed++;
            }
        }
    }
    
    pthread_mutex_unlock(&cache.lock);
    
    /* Вызываем системный fsync для файла */
    if (fsync(real_fd) < 0) {
        return -1;
    }
    
    printf("[VTPC] fsync: flushed %d dirty pages for fd=%d\n", flushed, real_fd);
    return 0;
}

/* ==================== ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ ==================== */

/* Получение статистики кэша */
void vtpc_get_stats(unsigned long *hits, unsigned long *misses, double *hit_rate) {
    pthread_mutex_lock(&cache.lock);
    
    if (hits) *hits = cache.hits;
    if (misses) *misses = cache.misses;
    
    if (hit_rate) {
        unsigned long total = cache.hits + cache.misses;
        *hit_rate = (total > 0) ? (double)cache.hits / total * 100.0 : 0.0;
    }
    
    pthread_mutex_unlock(&cache.lock);
}

/* Получение размера кэша */
int vtpc_get_cache_size(void) {
    return cache.capacity;
}

/* Получение текущего использования кэша */
int vtpc_get_cache_usage(void) {
    pthread_mutex_lock(&cache.lock);
    int usage = cache.count;
    pthread_mutex_unlock(&cache.lock);
    return usage;
}