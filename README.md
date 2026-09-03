# OS Course

Добро пожаловать на курс ОС! Этот репозиторий является точкой входа. Он содержит задания ЛР, а также документацию по настройке окружения, процессу сдачи ЛР и прочему.

Если вы защищаете ЛР, то автоматически считается, что вы приняли [норму поведения](./CODE_OF_CONDUCT.md).

## Лабораторные работы

Курс наполнен различными по сложности и направленности заданиями. Сложность задания влияет на количество баллов в качестве вознаграждения. Между заданиями определен рекомендуемый порядок выполнения. Обратите внимание на [порядок сдачи ЛР](./doc/process.md).

Часть заданий выполняется в [отдельном репозитории с учебной ОС Xv6](https://github.com/secs-dev/xv6-riscv).

## Карта

Легенда:

- Цвет означает количество баллов и сложность. Серый -- тривиально (0 баллов). По возрастанию сложности: серый, зеленый, желтый, красный.

- Стрелки показывают зависимости между этапами. Вы можете выполнять их в любом порядке, но мы рекомендуем именно этот.

- По ссылке на прямоугольнике можно перейти к формулировке этапа.

```mermaid
graph TD
    start["Начало"]

    subgraph other [" "]
        xv6["<a href='https://github.com/secs-dev/xv6-riscv' style='color:black;'>Xv6</a>"]
        project["<a href='https://github.com/secs-dev/os-course/blob/main/lab/project/README.md' style='color:black;'>Project</a>"]
    end

    subgraph fs ["Файловая система"]
        vtpc["<a href='https://github.com/secs-dev/os-course/blob/main/lab/vtpc/README.md' style='color:black;'>Page Cache</a>"]
        vtfs["<a href='https://github.com/secs-dev/os-course/blob/main/lab/vtfs/README.md' style='color:black;'>Kernel FS</a>"]
        fuse["<a href='https://github.com/secs-dev/os-course/blob/main/lab/fuse/README.md' style='color:black;'>FUSE</a>"]
    end

    subgraph mp ["Многозадачность"]
        vtsh["<a href='https://github.com/secs-dev/os-course/blob/main/lab/vtsh/README.md' style='color:black;'>Shell</a>"]
        corosched["<a href='https://github.com/secs-dev/os-course/blob/main/lab/corosched/README.md' style='color:black;'>CoroSched</a>"]
        corohttp["<a href='https://github.com/secs-dev/os-course/blob/main/lab/corohttp/README.md' style='color:black;'>CoroHTTP</a>"]
    end

    subgraph linux ["Linux"]
        vtkm["<a href='https://github.com/secs-dev/os-course/blob/main/lab/vtkm/README.md' style='color:black;'>Kernel Module</a>"]
        bpf_xdp["<a href='https://github.com/secs-dev/os-course/blob/main/lab/bpf-xdp/README.md' style='color:black;'>BPF. XDP</a>"]
    end

    start --> xv6
    start --> project
    start --> vtsh
    vtsh --> vtpc
    vtsh --> vtkm
    vtpc --> vtfs
    vtpc --> fuse
    vtsh --> corosched
    vtpc --> corohttp
    corosched --> corohttp
    vtsh --> bpf_xdp

    classDef free fill:#eceff1,stroke:#607d8b,stroke-width:2px,color:#37474f
    classDef easy fill:#c8e6c9,stroke:#388e3c,stroke-width:2px,color:#1b5e20
    classDef medium fill:#fff9c4,stroke:#f9a825,stroke-width:2px,color:#f57f17
    classDef hard fill:#ffcdd2,stroke:#c62828,stroke-width:2px,color:#b71c1c

    class project hard
    class vtsh easy
    class vtpc medium
    class vtkm easy
    class vtfs hard
    class fuse medium
    class corosched medium
    class corohttp medium
    class bpf_xdp medium
```
