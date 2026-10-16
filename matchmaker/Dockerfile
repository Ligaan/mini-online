# ---- stage 1: build ----
FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config libssl-dev \
        libprotobuf-dev protobuf-compiler libgrpc++-dev protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target matchmaker -j2

# ---- stage 2: runtime ----
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgrpc++1.51t64 libprotobuf32t64 libssl3t64 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 mm
COPY --from=build /build/matchmaker/matchmaker /usr/local/bin/matchmaker
USER mm
EXPOSE 8080 50051
CMD ["matchmaker"]