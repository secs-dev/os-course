# BPF. XDP

В данной ЛР мы познакомимся с [eBPF](https://ebpf.io/what-is-ebpf/) на примере [XDP](https://docs.ebpf.io/linux/program-type/BPF_PROG_TYPE_XDP/).

Мы начнем с тривиального XDP-фильтра, загрузим его и выполним в ядре, а после разработаем и опробуем простой ограничитель RPS. Поднимемся от "голого" С и [системного вызова bpf](https://man7.org/linux/man-pages/man2/bpf.2.html) к [libbpf](https://docs.kernel.org/bpf/libbpf/libbpf_overview.html).

## Подготовка

На Ubuntu должны быть установлены следующие пакеты:

```bash
sudo apt install clang llvm gcc
```

Убедитесь, что `clang` поддерживает `bpf` в качестве цели:

```bash
clang --print-targets | grep bpf
```

Для компиляции нужны будут системные заголовки:

```bash
ls /usr/include/$(gcc -print-multiarch)
```

## Первый XDP-фильтр

Рассмотрим следующий XDP-фильтр в [pass.bpf.c](./pass.bpf.c):

```c
#include <linux/bpf.h>

__attribute__((section("xdp"), used))
enum xdp_action pass(struct xdp_md *ctx) {
  (void)ctx;
  return XDP_PASS;
}
```

Он получает на вход [struct xdp_md](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L6662-L6664), а возвращает [enum xdp_action](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L6651C1-L6657).

Скомпилируем его:

```bash
clang -target bpf -I/usr/include/$(gcc -print-multiarch) -c pass.bpf.c -o pass.bpf.o
```

Посмотрим на получившийся байткод:

```bash
llvm-objdump --disassemble pass.bpf.o
# Disassembly of section xdp:
#0000000000000000 <pass>:
#       0:	7b 1a f8 ff 00 00 00 00	*(u64 *)(r10 - 0x8) = r1
#       1:	b7 00 00 00 02 00 00 00	r0 = 0x2
#       2:	95 00 00 00 00 00 00 00	exit
```

Соберите код со влюченными оптимизациями. Посмотрите на получившийся байткод.

Видно, что BPF-программа занимает пару десятков байт, но получившийся после компиляции объектный файл гораздо больше.

```bash
stat -c %s pass.bpf.o
```

Какую еще информацию он содержит (подсказка: изучите `llvm-readelf`), зачем она требуется?

BPF-инструкция занимает 8 байт и представлена структурой [struct bpf_insn](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L80-L86).

BPF-программу, сперва необходимо загрузить в ядро в качестве ресурса при помощи команды [BPF_PROG_LOAD](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L261-L274).

Команда является первым аргументом системного вызова [bpf](https://man7.org/linux/man-pages/man2/bpf.2.html). Далее следует [union bpf_attr](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L1527C1-L1527C15). Нам нужна [альтернатива для BPF_PROG_LOAD](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L1608-L1672).

Получить ровно необходимый массив байт из объектного файла можно следующей командой:

```bash
llvm-objcopy --dump-section xdp=/dev/stdout pass.bpf.o | hexdump -C
```

Теперь можно загрузить программу примерно так:

```c
const char license[] = "GPL";

union bpf_attr attr = {};
attr.prog_type = BPF_PROG_TYPE_XDP;
attr.insn_cnt = count;
attr.insns = (__u64)insns;
attr.license = (__u64)license;

int fd = syscall(SYS_bpf, BPF_PROG_LOAD, &attr, sizeof(union bpf_attr));
```

Протестировать можно при помощи команды `BPF_PROG_TEST_RUN`.

Реализуйте и запустите программу, которая загружает заданный фильтр из файла и тестирует его. Решение разместите в файле `load_xdp.c`.

## Создание виртуального сетевого интерфейса

Создайте network namespace с собственной сетевой таблицей:

```bash
sudo ip netns add xdp-ns
```

Создайте соединенную пару виртуальных Ethernet-интерфейсов:

```bash
sudo ip link add xdp-host type veth peer name xdp-peer
```

Переместите второй конец veth в `xdp-ns`:

```bash
sudo ip link set xdp-peer netns xdp-ns
```

Установите адреса:

```bash
sudo ip           addr add 10.200.1.1/24 dev xdp-host
sudo ip -n xdp-ns addr add 10.200.1.2/24 dev xdp-peer
sudo ip -n xdp-ns addr add 10.200.1.3/24 dev xdp-peer
```

Поднимите концы и loopback:

```bash
sudo ip           link set xdp-host up
sudo ip -n xdp-ns link set xdp-peer up
sudo ip -n xdp-ns link set lo       up
```

## Подключение XDP-фильтра

Пришло время подключить XDP-фильтра к сетевому интерфейсу. Это делается командой `BPF_LINK_CREATE`. Она требует fd на BPF-программу и индекс интерфейса (его можно получить из имени при помощи `if_nametoindex`).

Добработайте `load_xdp.c` так, чтобы программа принимала имя сетевого интерфейса и подключала к нему заданный xdp-фильтр. Запустите ее.

```bash
sudo ./load_xdp pass.bpf.bin xdp-host &
```

Найдите свой XDP-фильтр в списке загруженных BPF-программ:

```bash
sudo bpftool prog show
# 57: xdp  tag 57cd311f2e27366b  gpl
# 	loaded_at 2026-08-29T16:14:59+0000  uid 0
# 	xlated 16B  jited 23B  memlock 4096B
```

Посмотрите xlated инструкции:

```bash
sudo bpftool prog dump xlated tag 57cd311f2e27366b
#   0: (b7) r0 = 1
#   1: (95) exit
```

Посмотрите на JIT-скомпилированный код:

```bash
sudo bpftool prog dump jited tag 57cd311f2e27366b
```

Проанализируйте увиденное.

## Блокировка IPv4 кадров

Теперь усложним пример. Будем читать ethernet заголовок и блокировать IPv4 пакеты.

Попробуйте следующую реализацию:

```c
#include <linux/bpf.h>

struct eth {
  unsigned char dst[6];
  unsigned char src[6];
  unsigned short type;
} __attribute__((packed));

__attribute__((section("xdp"), used))
enum xdp_action bad(struct xdp_md *ctx) {
  void *data = (void *)(long)ctx->data;
  struct eth *eth = data;

  if (eth->type == __builtin_bswap16(0x0800)) {
    return XDP_DROP;
  }

  return XDP_PASS;
}
```

Вы должны получить `Permission denied`. За что?.. А вы уверены, что не вышли за пределы буфера `ctx`? Вот и [eBPF verifier](https://kernel-internals.org/bpf/bpf-verifier/) не уверен.

Чтобы прочитать диагностику от него, передайте `log_buf` в `BPF_PROG_LOAD`:

```c
attr.log_buf = (__u64)log;
attr.log_size = sizeof(log);
attr.log_level = 1;
```

Теперь получите пояснение:

```txt
0: R1=ctx() R10=fp0
0: (7b) *(u64 *)(r10 -16) = r1        ; R1=ctx() R10=fp0 fp-16_w=ctx()
1: (79) r1 = *(u64 *)(r10 -16)        ; R1_w=ctx() R10=fp0 fp-16_w=ctx()
2: (61) r1 = *(u32 *)(r1 +0)          ; R1_w=pkt(r=0)
3: (7b) *(u64 *)(r10 -24) = r1        ; R1_w=pkt(r=0) R10=fp0 fp-24_w=pkt(r=0)
4: (79) r1 = *(u64 *)(r10 -24)        ; R1_w=pkt(r=0) R10=fp0 fp-24_w=pkt(r=0)
5: (7b) *(u64 *)(r10 -32) = r1        ; R1_w=pkt(r=0) R10=fp0 fp-32_w=pkt(r=0)
6: (79) r1 = *(u64 *)(r10 -32)        ; R1_w=pkt(r=0) R10=fp0 fp-32_w=pkt(r=0)
7: (71) r2 = *(u8 *)(r1 +12)
invalid access to packet, off=12 size=1, R1(id=0,off=12,r=0)
R1 offset is outside of the packet
processed 8 insns (limit 1000000) max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0
```

Все понятно.

Можно сопоставлять диагностику со строками изначальной программы, но это за пределами данного туториала.

Исправьте программу так, чтобы verifier доказал ее валидность.

Попробуйте создать фильтр, работа которого не завершается. Что скажет verifier?

## Блокировка по IP

Реализуйте XDP-фильтр, не пропускающий пакеты от IP `10.200.1.2`.

Подсказка:

```c
struct eth {
  __u8 dst[6];
  __u8 src[6];
  __u16 type;
} __attribute__((packed));

struct ipv4 {
  __u8 version_ihl;
  __u8 tos;
  __u16 length;
  __u16 id;
  __u16 fragment;
  __u8 ttl;
  __u8 protocol;
  __u16 checksum;
  __u32 src;
  __u32 dst;
} __attribute__((packed));
```

Продемонстрируйте работу с и без фильтра:

```bash
sudo ip netns exec xdp-ns ping -I 10.200.1.2 -c 3 10.200.1.1
sudo ip netns exec xdp-ns ping -I 10.200.1.3 -c 3 10.200.1.1
```

## Ограничитель RPS по IP

Теперь реализуем более полезную программу в `rpc.bpf.c`. Фильтр должен дропать пакеты по IP адресу, если их частота превышает 5 пакетов в секунду. Для этого ему понадобится некоторые состояние. Также фильтр должен сообщать в userspace статистику по обработке пакетов по каждому IP-адресу.

Внутренее состояние фильтра и канал связи (разделяемую память) для общения с userspace можно реализовать при помощи [BPF Maps](https://docs.ebpf.io/linux/concepts/maps/). Скорее всего, вы сделаете их глобальными переменными.

Исследуйте байткод нового фильтра. Обратите внимание на места обращений к maps. Это должны быть широкие `ldimm64` инструкции.

Из объектного файла мы достанем только байткод. В нем будут неразрешенные релокации. Посмореть их можно следующим образом:

```bash
llvm-readelf -r rps.bpf.o
# Relocation section '.relxdp' at offset 0x710 contains 7 entries:
#     Offset             Info             Type               Symbol's Value  Symbol's Name
# 0000000000000188  0000001700000001 R_BPF_64_64            0000000000000000 .data
# 00000000000001a0  0000001900000001 R_BPF_64_64            0000000000000000 rates
# 00000000000001d0  0000001700000001 R_BPF_64_64            0000000000000000 .data
# 0000000000000240  0000001700000001 R_BPF_64_64            0000000000000000 .data
# 0000000000000258  0000001900000001 R_BPF_64_64            0000000000000000 rates
# 0000000000000378  0000001700000001 R_BPF_64_64            0000000000000000 .data
# 0000000000000390  0000001a00000001 R_BPF_64_64            0000000000000014 stats
```

Запись означает, что в интрукции по заданному "Offset" неопределена ссылка на "Symbol Name". Разрешить эти ссылки должен будет наш загрузчик XDP-фильтров.

Новый загрузчик должен сперва создать maps при помощи команды `BPF_MAP_CREATE`, возвращающей fd на map. Теперь необходимо пропатчить неразрешенные `ldimm64`, установив в них `src_reg` в [BPF_PSEUDO_MAP_FD](https://github.com/torvalds/linux/blob/cf72cbb39da84b6f02f90c07f33b102fc10b16f0/include/uapi/linux/bpf.h#L1342-L1354) и установив `imm` в `<fd>`.

Допустимо, если ваш загрузчик получит смещения в argv. Будет плюсом, если вы автоматизируете этот процесс.

Какие еще `src_reg` в `ldimm64` бывают? Зачем они? Как они обрабатываются в общепризнанных инструментах для работы с BPF?

Каким образом релокации разрешаются в компиляторах не под BPF? В чем разница между статическими и динамическими библиотеками?

Реализуйте также инструмент для чтения статистики RPS-ограничителя. Его можно встроить в загрузчик.

Проверьте работоспособность командами:

```bash
sudo ip netns exec xdp-ns ping -I 10.200.1.2 -f -c 10000 10.200.1.1
sudo ip netns exec xdp-ns ping -I 10.200.1.3 -f -c 10000 10.200.1.1
```

## Миграция на libbpf

Перепишите RPS-ограничитель на `libbpf`.
