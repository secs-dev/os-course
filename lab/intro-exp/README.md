# Introductionary Experiment


Generate graph:
```sh
python3 ../lab/util/graphgen.py -s 64M --seed 427 -o graph-00.bin
python3 ../lab/util/graphgen.py -s 64M --seed 42 --topology chain -b 0.7 --min-step-pages 2  -o graph-04.bin
```

Build traverser
```sh
clang -o out/graph_traverse src/graph_traverse.c -Wall -O2
```

Execute traverser
```sh
./out/graph_traverse 5 file1.bin file2.bin
./out/graph_traverse --no-cache 10 file.bin
./out/graph_traverse --write --no-cache 5 file1.bin file2.bin
```

Basic measurement:
```sh
time ./out/graph_traverse 1 file1.bin
```


Same thing but with MMAP:
```sh
# Компиляция (Linux / macOS)
clang -o out/graph_traverse_mmap src/graph_traverse_mmap.c -Wall -O2

# Запуск в режиме чтения (10 итераций, циклически по двум файлам)
./out/graph_traverse_mmap 10 graph1.bin graph2.bin

# Режим записи с отключением кэша
./out/graph_traverse_mmap --write --no-cache 5 data1.bin data2.bin data3.bin
```
