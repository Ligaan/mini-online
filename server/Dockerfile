# ---- stage 1: build ----
FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config libssl-dev \
        libprotobuf-dev protobuf-compiler libgrpc++-dev protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target server -j2

# ---- stage 2: runtime ----
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgrpc++1.51t64 libprotobuf32t64 libssl3t64 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 game
COPY --from=build /build/server/server /usr/local/bin/server
USER game
EXPOSE 9000/udp 50052
CMD ["server"]