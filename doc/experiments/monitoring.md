# Утилиты мониторинга Linux для экспериментов над ОС

> Общая методичка курса «Операционные системы»: какие утилиты снимают какие
> метрики и как читать их вывод. Применима к любому лабораторному заданию —
> подставляйте вместо `<program>` конкретную исследуемую программу.

Мониторинг в экспериментах используется в двух разных ролях, которые важно
не путать:

1. **Характеризация** — снять паспорт системы и понять принципиальное
   поведение исследуемой программы до планирования серии измерений. Здесь
   допустимы более «тяжёлые» инструменты (`perf stat`, постоянный опрос
   `vmstat`).
2. **Финальные измерения** — метрика должна собираться максимально
   ненавязчиво (см. `statistics.md`, раздел про интрузивность), чтобы сам
   факт мониторинга не исказил измеряемое время.

## Паспорт системы (снимается один раз в начале работы)

| Команда | Что даёт |
|---|---|
| `uname -a` | ядро, архитектура |
| `lscpu` | модель CPU, число ядер/потоков, частоты, флаги (avx, sse и т.д.) |
| `lscpu -e` | топология: logical CPU ↔ physical core ↔ socket — полезно для `taskset` и SMT |
| `cat /proc/cpuinfo \| grep -i cache` | размеры кэшей L1/L2/L3 |
| `free -h` | объём RAM, использование, размер page cache/buffers |
| `sudo lshw -class disk -class memory -short` | модель диска/памяти, тип шины |
| `sudo smartctl -a /dev/sdX` | тип накопителя (SSD/HDD/NVMe), состояние |
| `cat /sys/block/sdX/queue/rotational` | `0` = не вращающийся (SSD/NVMe), `1` = HDD |
| `nproc` | число доступных логических CPU |
| `numactl --hardware` | NUMA-топология, если применимо к эксперименту |

## Мониторинг во время (или сразу после) запуска

### `/usr/bin/time -v <program> ...`

Простой и точный источник посекундной метрики конкретного процесса.
Используйте именно `/usr/bin/time`, а не bash-builtin `time` — у builtin
нет флага `-v` и меньше метрик.

Даёт одним запуском: wall clock, user time, system time, Maximum RSS,
voluntary/involuntary context switches, minor/major page faults, файловые
операции ввода-вывода (если ядро их считает для процесса).

### `perf stat -e <events> <program> ...`

Более гибкий инструмент, аппаратные и программные счётчики:

```bash
perf stat -e task-clock,context-switches,cpu-migrations,page-faults,\
cache-references,cache-misses,cycles,instructions \
  taskset -c 2 <program> ...
```

- `context-switches` — сколько раз планировщик снимал процесс с CPU;
- `cpu-migrations` — сколько раз процесс переезжал на другое ядро (в норме
  ~0 при корректном `taskset`);
- `cache-misses`/`cache-references` — промахи кэша CPU;
- `page-faults` — суммарно minor+major page faults, ключевая метрика для
  экспериментов, связанных с `mmap` и виртуальной памятью;
- для более специфичных экспериментов по памяти пригодятся счётчики TLB:
  `dTLB-load-misses`, `dTLB-store-misses` (список доступных событий —
  `perf list`).

`perf stat` добавляет собственные накладные расходы — не используйте его как
источник финальной метрики wall time в большой серии измерений, только для
характеризации.

### Общесистемные наблюдатели (в соседнем терминале, параллельно запуску)

```bash
vmstat 1        # свободная память, буферы/кэш, swap, %us/%sy/%id CPU, блочный IO (bi/bo)
mpstat -P ALL 1 # загрузка каждого логического CPU отдельно — проверка отсутствия миграций
pidstat -urd 1  # по конкретному PID: %CPU, память, диск (пакет sysstat)
iostat -x 1     # загрузка устройств хранения: %util, await (задержка), r/s, rkB/s
```

Как читать `iostat -x`:
- `%util` близко к 100% — диск является узким местом;
- `await` — средняя задержка запроса в мс;
- `r/s`, `rkB/s` — количество и объём операций чтения в секунду.

### Генерация фоновой нагрузки — `stress-ng`

Полезна для демонстрации того, как выглядит «грязное» измерение с
конкуренцией за ресурсы, прежде чем вы научитесь её убирать (см.
`environment.md`):

```bash
stress-ng --cpu 2 --io 1 --vm 1 --vm-bytes 256M --timeout 30s
```

- `--cpu N` — N воркеров, нагружающих CPU;
- `--io N` — N воркеров, вызывающих `sync()`;
- `--vm N --vm-bytes SIZE` — воркеры, нагружающие подсистему памяти.

С `stress-ng` в фоне обычно растут `involuntary context switches` и
`cpu-migrations`, увеличивается разброс wall time — наглядная иллюстрация
того, зачем нужна изоляция окружения.

## Сводная таблица: метрика → инструмент

| Метрика | Основной источник | Альтернатива/уточнение |
|---|---|---|
| Wall time | `/usr/bin/time -v` | `perf stat` (task-clock), встроенный таймер программы, шелловский `time` |
| User/Kernel time | `/usr/bin/time -v` | `perf stat -e task-clock`, `/proc/[pid]/stat` |
| %CPU в моменте | `pidstat -u 1`, `top` | `mpstat -P ALL 1` |
| Context switches | `/usr/bin/time -v` | `perf stat -e context-switches` |
| CPU migrations | `perf stat -e cpu-migrations` | — |
| Page faults (minor/major) | `/usr/bin/time -v` | `perf stat -e page-faults`, `/proc/[pid]/stat` (`minflt`, `majflt`) |
| RAM/RSS | `/usr/bin/time -v` (Maximum RSS) | `pidstat -r 1`, `/proc/[pid]/status` (`VmRSS`) |
| Disk IO / задержка | `iostat -x 1` | `pidstat -d 1` |
| Кэш CPU (cache misses) | `perf stat -e cache-misses,cache-references` | — |
| TLB misses | `perf stat -e dTLB-load-misses,dTLB-store-misses` | актуально для заданий по виртуальной памяти |
| Состояние page cache | `free -h` | `cat /proc/meminfo` (`Cached`, `Buffers`) |

## Замечание об интрузивности инструментов

Любой инструмент мониторинга — сам по себе процесс (или модифицирует
поведение вашего процесса, как `strace`), и он потребляет CPU/память,
создаёт дополнительные системные вызовы или прерывания. Проверяйте это
явно: прогоните одну и ту же конфигурацию под «тяжёлым» инструментом
(`perf stat`, `strace -c`) и без него, сравните wall time. Если разница
заметна на фоне доверительного интервала — используйте для финальной серии
более лёгкий способ измерения (подробнее — `statistics.md`).
