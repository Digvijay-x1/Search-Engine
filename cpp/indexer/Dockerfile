FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpqxx-dev \
    libhiredis-dev \
    librocksdb-dev \
    libgumbo-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy necessary directories
COPY cpp/indexer cpp/indexer
COPY cpp/crawler cpp/crawler

# Set working directory to indexer for building
WORKDIR /app/cpp/indexer

RUN mkdir -p build && cd build && cmake ../src && make && ctest --output-on-failure


CMD ["./build/indexer"]
