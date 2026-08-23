# Introductionary Experiment


Generate graph:
```sh
python3 ./lab/util/graphgen.py -s 64M --seed 427 -o graph-00.bin
python3 ./lab/util/graphgen.py -s 64M --seed 42 --topology chain -b 0.7 --min-step-pages 2  -o graph-04.bin
```

Build traverser
```sh
clang -o graph_traverse graph_traverse.c -Wall -O2
```

Execute traverser
```sh
./graph_traverse 5 file1.bin file2.bin
./graph_traverse --no-cache 10 file.bin
./graph_traverse --write --no-cache 5 file1.bin file2.bin
```

Basic measurement:
```sh
time ./graph_traverse 1 file1.bin
```
