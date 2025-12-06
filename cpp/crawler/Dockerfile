FROM ubuntu:latest

# Install Build Tools and Libraries
# libpqxx-dev: C++ adapter for Postgres
# libhiredis-dev: C adapter for Redis
# libcurl4-openssl-dev: HTTP Client
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libcurl4-openssl-dev \
    libpqxx-dev \
    libhiredis-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build
RUN cmake src && make

CMD ["./crawler"]