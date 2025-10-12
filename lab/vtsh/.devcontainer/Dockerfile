FROM silkeh/clang:latest

# Установка только необходимых утилит для разработки
RUN apt-get update && apt-get install -y \
    # Python для тестов
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    python3-pytest \
    
    build-essential \
    cmake \
    ninja-build \
    
    
    lldb \
    
    clang-format \
    clang-tidy \
    clangd \
    
    valgrind \
    strace \
    ltrace \
    
    && rm -rf /var/lib/apt/lists/*

# Установка lldb-mi из GitHub


# Установка рабочей директории
WORKDIR /workspace


# Настройка переменных окружения
ENV CC=clang CXX=clang++ CMAKE_EXPORT_COMPILE_COMMANDS=ON

