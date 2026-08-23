#!/usr/bin/env python3
"""
graphgen.py — генератор бинарного файла с графом для экспериментов
над подсистемой page cache (проверка паттернов последовательного /
случайного чтения при обходе графа, а также записи в вершины).

ФОРМАТ ФАЙЛА
============

Файл состоит из фиксированного заголовка (HEADER_SIZE = 64 байта) и
N записей вершин ФИКСИРОВАННОГО размера, идущих подряд. Благодаря
фиксированному размеру записи адрес вершины по её индексу вычисляется
за O(1):

    offset(i) = HEADER_SIZE + i * record_size

Никаких указателей/оглавлений читать не нужно — это и есть требование
"вершины должны легко индексироваться через offset в файле".

Заголовок (little-endian, БЕЗ выравнивания, чтобы формат был одинаков
на любой платформе/архитектуре):

    magic            8s   b'GCACHEG1'
    version          I    номер версии формата
    node_count       Q    количество вершин
    record_size      I    размер записи вершины, байт
    fan_out          I    максимальная исходящая степень вершины
    page_size        I    размер страницы, использованный при генерации
    back_prob_permille I  P(обратный переход) * 1000
    seed             Q    зерно генератора (для воспроизводимости)
    root_index       Q    индекс стартовой вершины обхода
    min_step_nodes   Q    гарантированный минимальный шаг (в узлах)
                          между вершинами для "случайных" переходов
    flags            I    битовые флаги (bit0: 1 = граф ациклический)

Итого 64 байта.

Запись вершины (record_size = 16 + 8*fan_out байт):

    value       q   int64  — полезная нагрузка вершины (изменяемая полем записи)
    degree      I   uint32 — фактическое число исходящих рёбер (<= fan_out)
    reserved    I   uint32 — зарезервировано/выравнивание
    children[fan_out]  Q   uint64 — индексы дочерних вершин;
                          неиспользуемые слоты = SENTINEL (0xFFFFFFFFFFFFFFFF)

КЛЮЧЕВЫЕ ИДЕИ ГЕНЕРАЦИИ
=======================

1. Гарантия отсутствия зацикливания.
   Строится случайная перестановка индексов узлов `topo_order`. Любое
   ребро создаётся только от узла с МЕНЬШЕЙ позицией в topo_order к узлу
   с БОЛЬШЕЙ позицией. Это делает граф ациклическим (DAG) по построению
   — не нужно ни visited-set, ни проверок во время обхода, чтобы избежать
   бесконечного цикла. Каждая не-корневая вершина получает ровно одного
   родителя из уже обработанных — граф гарантированно связный и весь
   достижим из корня (root_index), т.е. обход не "провалится" в короткий
   тупик раньше времени.

   При fan_out=1 эта же схема гарантированно вырождается ровно в связный
   список (Hamiltonian path) со случайным порядком вершин — при out-degree
   <= 1 дерево физически не может ветвиться, значит "родословная" — это
   единственная простая цепочка. Т.е. линейный граф из ТЗ является
   частным случаем общей модели графов.

2. Управление паттерном ввода-вывода (--backprob).
   Родитель для очередной "рождающейся" вершины выбирается СРЕДИ ВСЕХ уже
   рождённых вершин, у которых ещё есть свободная исходящая ёмкость
   (fan_out). Выбор — точный и равномерно случайный по нужную сторону
   индекса (offset), обеспечивается деревом Фенвика (структура
   order-statistics), а не эвристическим "окном" — это важно, потому что
   при fan_out=1 суммарная свободная ёмкость едва превышает необходимое
   число рёбер, и наивный локальный поиск кандидата почти всегда не
   оставляет свободы выбора направления. С вероятностью backprob цель
   получает МЕНЬШИЙ offset, чем источник ("обратный" переход), иначе —
   БОЛЬШИЙ ("вперёд" = вклад в последовательное чтение). backprob=0 —
   преимущественно вперёд по возрастанию offset, backprob=1 —
   преимущественно назад, 0.5 — смешанный паттерн.

   Нюанс: для fan_out=1 (цепочка) путь обязан посетить КАЖДУЮ вершину
   ровно один раз. Если всегда прыгать вперёд, часть меньших offset'ов
   неизбежно "отстаёт" и остаётся непосещённой — под конец обхода их
   всё равно придётся подобрать обратными переходами. Поэтому на
   практике при backprob=0/1 итоговая доля перекоса выходит на уровне
   ~85-95%, а не строго 100% — это ожидаемое свойство гамильтонова
   пути, а не ошибка. Фактически достигнутое соотношение печатается по
   завершении генерации.

3. Гарантия кэш-промаха для "случайных" переходов (--page-size,
   --min-step-pages).
   Так как несколько записей вершины помещаются в одну страницу (обычно
   4 КиБ), переход в СОСЕДНИЙ по индексу узел может попасть в уже
   прочитанную/закэшированную страницу и НЕ вызвать промах. Поэтому среди
   кандидатов нужного направления генератор в первую очередь ищет такого,
   чья дистанция от источника (в узлах) не меньше:

       min_step_nodes = ceil(page_size * min_step_pages / record_size)

   т.е. переход гарантированно пересекает границу как минимум
   `min_step_pages` страниц. Если подходящего по дистанции кандидата нет
   (бывает под конец генерации, когда свободных вершин мало), требование
   по дистанции мягко ослабляется — итоговая доля "коротких" переходов
   печатается в статистике по завершении генерации.

4. Ветвление (--fanout) и глубина.
   `fan_out` ограничивает исходящую степень (сколько раз узел может быть
   выбран родителем). Поскольку каждая не-корневая вершина получает
   ровно одного родителя, при fan_out=1 получается путь длиной N-1, а при
   fan_out>1 — дерево с ветвлением и глубиной, зависящей от того, кого
   выбирает в родители алгоритм (в среднем получается развесистое дерево
   логарифмической-линейной глубины; сильно ветвистые/сильно вытянутые
   графы настраиваются через отношение fan_out к размеру графа).

5. Значение в вершине (--value-mode).
   Каждая вершина хранит int64 payload, который экспериментатор может
   менять в режиме записи (просто перезаписав 8 байт по смещению
   offset(i)). Это не влияет на структуру рёбер.

Автор использует собственный детерминированный PRNG (SplitMix64) вместо
random.Random, чтобы при необходимости результат было легко
воспроизвести (побитно) в утилите на другом языке — алгоритм
общеизвестный и тривиально переносимый.
"""

import argparse
import math
import struct
import sys

MAGIC = b"GCACHEG1"
VERSION = 1
HEADER_FMT = "<8sIQIIIIQQQI"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 64, HEADER_SIZE
SENTINEL = (1 << 64) - 1
MASK64 = (1 << 64) - 1


# --------------------------------------------------------------------------
# Детерминированный, платформонезависимый PRNG (SplitMix64)
# --------------------------------------------------------------------------
class SplitMix64:
    def __init__(self, seed: int):
        self.state = seed & MASK64

    def next_u64(self) -> int:
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        z = z ^ (z >> 31)
        return z

    def random(self) -> float:
        """Равномерное float в [0, 1)."""
        return (self.next_u64() >> 11) * (1.0 / (1 << 53))

    def randint(self, lo: int, hi: int) -> int:
        """Равномерное целое в [lo, hi] включительно."""
        span = hi - lo + 1
        return lo + (self.next_u64() % span)

    def choice(self, seq):
        return seq[self.randint(0, len(seq) - 1)]

    def shuffle(self, lst: list) -> None:
        """Fisher-Yates на собственном генераторе — воспроизводимо всюду."""
        for i in range(len(lst) - 1, 0, -1):
            j = self.randint(0, i)
            lst[i], lst[j] = lst[j], lst[i]


class Fenwick:
    """
    Дерево Фенвика (BIT) поверх пространства индексов узлов [0, n).
    Хранит "оставшуюся ёмкость" (сколько ещё исходящих рёбер может создать
    узел) и позволяет за O(log n):
      - изменить ёмкость узла (update)
      - посчитать суммарную ёмкость на отрезке [lo, hi] (range_sum)
      - найти РАВНОМЕРНО СЛУЧАЙНЫЙ доступный узел на произвольном отрезке
        (через выбор случайного ранга k и find_kth)

    Это нужно, чтобы --backprob и гарантия минимальной дистанции перехода
    (для кэш-промаха) соблюдались ТОЧНО и эффективно, даже когда узлов
    миллионы, а свободной "ёмкости" мало (например, при fan_out=1, когда
    почти каждый слот на счету и наивный поиск окном не оставляет свободы
    выбора).
    """

    def __init__(self, n: int):
        self.n = n
        self.tree = [0] * (n + 1)

    def update(self, i: int, delta: int) -> None:
        i += 1
        while i <= self.n:
            self.tree[i] += delta
            i += i & (-i)

    def prefix_sum(self, i: int) -> int:
        """Сумма на [0, i] включительно (0-индексация)."""
        if i < 0:
            return 0
        if i >= self.n:
            i = self.n - 1
        i += 1
        s = 0
        while i > 0:
            s += self.tree[i]
            i -= i & (-i)
        return s

    def range_sum(self, lo: int, hi: int) -> int:
        if lo > hi:
            return 0
        lo = max(lo, 0)
        hi = min(hi, self.n - 1)
        if lo > hi:
            return 0
        return self.prefix_sum(hi) - self.prefix_sum(lo - 1)

    def find_kth(self, k: int) -> int:
        """0-индексированный индекс узла, на котором накопленная сумма
        впервые достигает k (k нумеруется с 1). Требует k <= total()."""
        pos = 0
        rem = k
        p = 1
        while p * 2 <= self.n:
            p *= 2
        while p > 0:
            if pos + p <= self.n and self.tree[pos + p] < rem:
                pos += p
                rem -= self.tree[pos]
            p //= 2
        return pos

    def random_in_range(self, lo: int, hi: int, rng: "SplitMix64"):
        """Равномерно случайный доступный (capacity>0) индекс в [lo, hi],
        либо None, если таких нет."""
        total = self.range_sum(lo, hi)
        if total <= 0:
            return None
        base = self.prefix_sum(lo - 1)
        k = base + rng.randint(1, total)
        return self.find_kth(k)


def parse_size(text: str) -> int:
    """Разбирает '64M', '2G', '1024', '500K' и т.п. в число байт."""
    text = text.strip().upper()
    mult = 1
    suffixes = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3, "T": 1024 ** 4, "B": 1}
    if text and text[-1] in suffixes:
        mult = suffixes[text[-1]]
        text = text[:-1]
    return int(float(text) * mult)


# --------------------------------------------------------------------------
# Основная генерация
# --------------------------------------------------------------------------
def build_graph(args):
    record_size = 16 + 8 * args.fanout
    avail = args.size_bytes - HEADER_SIZE
    if avail < record_size * args.min_nodes:
        raise SystemExit(
            f"Файл слишком мал: доступно {avail} байт под вершины, "
            f"а нужно минимум {record_size * args.min_nodes} "
            f"({args.min_nodes} вершин по {record_size} байт). "
            f"Увеличьте --size или уменьшите --fanout."
        )
    node_count = avail // record_size

    min_step_nodes = max(1, math.ceil(args.page_size * args.min_step_pages / record_size))
    if min_step_nodes >= node_count:
        print(
            f"[предупреждение] min_step_nodes ({min_step_nodes}) >= node_count "
            f"({node_count}); гарантия кэш-промаха ослаблена — граф слишком мал "
            f"относительно page_size.",
            file=sys.stderr,
        )
        min_step_nodes = max(1, node_count // 4)

    rng = SplitMix64(args.seed)
    children = [[] for _ in range(node_count)]
    stats = {"forward": 0, "backward": 0, "short_step": 0}

    def pick_neighbour(pool, cur, want_backward):
        """Ищет в `pool` (объект Fenwick с "доступными" id) идеальный по
        направлению и дистанции id, при необходимости смягчая требования.
        Возвращает (id, дистанция_соблюдена)."""
        if want_backward:
            near_lo, near_hi = 0, cur - min_step_nodes
            far_lo, far_hi = 0, cur - 1
        else:
            near_lo, near_hi = cur + min_step_nodes, node_count - 1
            far_lo, far_hi = cur + 1, node_count - 1

        picked = pool.random_in_range(near_lo, near_hi, rng)
        if picked is not None:
            return picked, True
        picked = pool.random_in_range(far_lo, far_hi, rng)
        if picked is not None:
            return picked, False
        picked = pool.random_in_range(0, node_count - 1, rng)
        return picked, False

    if args.fanout == 1:
        # ВАЖНЫЙ ЧАСТНЫЙ СЛУЧАЙ: при fan_out=1 суммарная "ёмкость" родителей
        # (по одному слоту на узел) равна числу узлов, а нужно ровно N-1
        # рёбер — свободного "запаса" почти нет. Если строить граф как DAG
        # по мере "рождения" узлов (общий алгоритм ниже), на каждом шаге
        # окажется ровно один доступный кандидат в родители (тот, что был
        # рождён последним), и никакой выбор направления станет невозможен
        # — backprob не будет иметь физического смысла. Поэтому для чистого
        # связного списка используется прямой алгоритм самоизбегающего
        # обхода: строим путь как перестановку всех узлов, каждый следующий
        # выбирается СРЕДИ ВСЕХ ЕЩЁ НЕ ПОСЕЩЁННЫХ узлов — на протяжении
        # почти всего построения в запасе остаётся большой пул кандидатов,
        # так что backprob и min_step_nodes соблюдаются точно.
        unvisited = Fenwick(node_count)
        for i in range(node_count):
            unvisited.update(i, 1)
        start = rng.randint(0, node_count - 1)
        unvisited.update(start, -1)
        cur = start
        topo_order = [start]  # порядок посещения = валидный топологический порядок (все рёбра идут "вперёд" по нему)
        for _ in range(node_count - 1):
            want_backward = rng.random() < args.backprob
            nxt, dist_ok = pick_neighbour(unvisited, cur, want_backward)
            unvisited.update(nxt, -1)
            children[cur].append(nxt)
            if nxt > cur:
                stats["forward"] += 1
            else:
                stats["backward"] += 1
            if not dist_ok:
                stats["short_step"] += 1
            cur = nxt
            topo_order.append(nxt)
        root = start
    else:
        # Общий случай (fan_out > 1): строим DAG по мере "рождения" узлов в
        # случайном порядке topo_order — любое ребро идёт от узла с меньшей
        # позицией в topo_order к узлу с большей, это гарантирует отсутствие
        # циклов. Каждый новый узел получает ровно одного родителя из уже
        # рождённых — граф гарантированно связный, весь достижим из корня.
        # При fan_out>1 общая ёмкость растёт быстрее, чем расходуется
        # (прирост fan_out-1 за шаг), так что уже через несколько шагов
        # появляется большой запас кандидатов и backprob соблюдается точно.
        topo_order = list(range(node_count))
        rng.shuffle(topo_order)

        out_degree = [0] * node_count
        capacity = Fenwick(node_count)
        capacity.update(topo_order[0], args.fanout)

        for k in range(1, node_count):
            v = topo_order[k]
            want_backward = rng.random() < args.backprob
            # для родителя направление обратное тому, что для потомка:
            # "backward"-ребро (parent -> v, offset(v) < offset(parent))
            # означает, что относительно v нужно искать родителя С БОЛЬШИМ id.
            parent, dist_ok = pick_neighbour(capacity, v, want_backward=not want_backward)

            capacity.update(parent, -1)
            children[parent].append(v)
            out_degree[parent] += 1

            if v > parent:
                stats["forward"] += 1
            else:
                stats["backward"] += 1
            if not dist_ok:
                stats["short_step"] += 1

            capacity.update(v, args.fanout)  # v рождается и сам становится доступным родителем

        root = topo_order[0]

    # Значения вершин (payload для режима записи)
    values = [0] * node_count
    if args.value_mode == "index":
        values = list(range(node_count))
    elif args.value_mode == "random":
        for i in range(node_count):
            values[i] = rng.randint(args.value_min, args.value_max)
    # 'zero' — оставляем нули

    return {
        "node_count": node_count,
        "record_size": record_size,
        "root": root,
        "min_step_nodes": min_step_nodes,
        "children": children,
        "values": values,
        "stats": stats,
        "topo_order": topo_order,
    }


def write_file(path, args, g):
    node_count = g["node_count"]
    record_size = g["record_size"]

    flags = 0
    flags |= 1  # bit0: граф ациклический — гарантировано построением

    header = struct.pack(
        HEADER_FMT,
        MAGIC,
        VERSION,
        node_count,
        record_size,
        args.fanout,
        args.page_size,
        int(round(args.backprob * 1000)),
        args.seed & MASK64,
        g["root"],
        g["min_step_nodes"],
        flags,
    )

    node_struct = struct.Struct("<qII" + "Q" * args.fanout)

    with open(path, "wb") as f:
        f.write(header)
        children = g["children"]
        values = g["values"]
        for i in range(node_count):
            ch = children[i]
            degree = len(ch)
            padded = ch + [SENTINEL] * (args.fanout - degree)
            f.write(node_struct.pack(values[i], degree, 0, *padded))

    return HEADER_SIZE + node_count * record_size


def verify_graph(g):
    """Проверка связности (достижимости из корня) и отсутствия циклов."""
    node_count = g["node_count"]
    children = g["children"]
    root = g["root"]

    # BFS от корня
    from collections import deque

    visited = [False] * node_count
    depth = [-1] * node_count
    visited[root] = True
    depth[root] = 0
    q = deque([root])
    max_depth = 0
    while q:
        u = q.popleft()
        for v in children[u]:
            if not visited[v]:
                visited[v] = True
                depth[v] = depth[u] + 1
                max_depth = max(max_depth, depth[v])
                q.append(v)

    reached = sum(visited)

    # Проверка ацикличности через порядок topo_order (по построению это
    # гарантировано, но проверим явно на всякий случай)
    pos = [0] * node_count
    for idx, node in enumerate(g["topo_order"]):
        pos[node] = idx
    is_dag = True
    for u in range(node_count):
        for v in children[u]:
            if pos[v] <= pos[u]:
                is_dag = False
                break
        if not is_dag:
            break

    return {
        "reached": reached,
        "coverage": reached / node_count,
        "max_depth": max_depth,
        "is_dag": is_dag,
    }


def main():
    ap = argparse.ArgumentParser(
        description="Генератор бинарного файла графа для тестов page cache."
    )
    ap.add_argument("-o", "--output", default="graph.bin", help="путь к выходному файлу")
    ap.add_argument("-s", "--size", required=True, help="целевой размер файла, напр. 64M, 2G, 1000000")
    ap.add_argument(
        "-f", "--fanout", type=int, default=1,
        help="макс. число исходящих рёбер на вершину (по умолчанию 1 = линейный список)",
    )
    ap.add_argument(
        "-b", "--backprob", type=float, default=0.5,
        help="вероятность 'обратного' перехода (к меньшему offset), 0..1. "
             "0 = чисто последовательно вперёд, 1 = чисто назад",
    )
    ap.add_argument("--seed", type=int, required=True, help="зерно генератора (для воспроизводимости)")
    ap.add_argument("--page-size", type=int, default=4096, help="размер страницы ОС/ФС, байт")
    ap.add_argument(
        "--min-step-pages", type=float, default=2.0,
        help="мин. число страниц, которое должен пересекать 'случайный' переход "
             "(гарантия кэш-промаха)",
    )
    ap.add_argument(
        "--topology", choices=["chain", "graph"], default=None,
        help="удобный пресет: chain принудительно задаёт fanout=1 (простой связный "
             "список / Hamiltonian path со случайным порядком узлов); "
             "graph использует заданный --fanout как есть",
    )
    ap.add_argument("--min-nodes", type=int, default=64, help="минимально допустимое число вершин")
    ap.add_argument(
        "--value-mode", choices=["zero", "index", "random"], default="random",
        help="как инициализировать payload-значение вершины",
    )
    ap.add_argument("--value-min", type=int, default=-(2**31))
    ap.add_argument("--value-max", type=int, default=2**31 - 1)
    ap.add_argument("--verify", action="store_true", help="после генерации проверить связность и ацикличность")

    args = ap.parse_args()

    if args.topology == "chain":
        if args.fanout != 1:
            print("[инфо] --topology chain: принудительно fanout=1", file=sys.stderr)
        args.fanout = 1

    if args.fanout < 1:
        raise SystemExit("--fanout должен быть >= 1")
    if not (0.0 <= args.backprob <= 1.0):
        raise SystemExit("--backprob должен быть в диапазоне [0, 1]")

    args.size_bytes = parse_size(args.size)

    g = build_graph(args)
    actual_size = write_file(args.output, args, g)

    total_edges = g["stats"]["forward"] + g["stats"]["backward"]
    print(f"Файл записан: {args.output}")
    print(f"  размер файла:        {actual_size} байт (запрошено: {args.size_bytes})")
    print(f"  число вершин:        {g['node_count']}")
    print(f"  размер записи:       {g['record_size']} байт")
    print(f"  fan_out:             {args.fanout}")
    print(f"  page_size:           {args.page_size}")
    print(f"  min_step_nodes:      {g['min_step_nodes']} (~{g['min_step_nodes']*g['record_size']} байт)")
    print(f"  root_index:          {g['root']}  (offset={HEADER_SIZE + g['root']*g['record_size']})")
    print(f"  рёбер всего:         {total_edges}")
    if total_edges:
        print(
            f"  доля вперёд/назад:   {g['stats']['forward']/total_edges:.3f} / "
            f"{g['stats']['backward']/total_edges:.3f} (запрошено backprob={args.backprob})"
        )
        print(f"  доля 'коротких' переходов (< min_step_nodes): {g['stats']['short_step']/total_edges:.3f}")

    if args.verify:
        v = verify_graph(g)
        print("Проверка:")
        print(f"  достижимо из корня:  {v['reached']}/{g['node_count']} ({v['coverage']*100:.2f}%)")
        print(f"  макс. глубина BFS:   {v['max_depth']}")
        print(f"  граф ацикличен:      {v['is_dag']}")
        if v["coverage"] < 0.999:
            print("  [ВНИМАНИЕ] не все вершины достижимы из корня!", file=sys.stderr)


if __name__ == "__main__":
    main()