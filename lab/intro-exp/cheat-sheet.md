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

Sample logs
```log
time ./lab/intro-exp/out/graph_traverse --write 2 graph.bin
Iteration 1/2 (write): traversing graph.bin ... OK (2796201 nodes processed)
Iteration 2/2 (write): traversing graph.bin ... OK (2796201 nodes processed)

________________________________________________________
Executed in    9.36 secs    fish           external
   usr time    1.76 secs    0.28 millis    1.76 secs
   sys time    6.95 secs    1.21 millis    6.95 secs
```
