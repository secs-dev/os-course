
FROM ubuntu:22.04

RUN set -eux; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    python3 \
    python3-pip; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

RUN python3 - <<'PY'
import pathlib
pathlib.Path('test/__init__.py').touch()
PY

RUN cmake -S . -B build && cmake --build build

RUN ln -s /app/build /build

ENV PYTHONPATH=/app/test:/app

ENV PATH="/app/build/bin:/app/build/cpu:/app/build/ema:/app/build/io-loader:${PATH}"

CMD ["./build/bin/vtsh"]
