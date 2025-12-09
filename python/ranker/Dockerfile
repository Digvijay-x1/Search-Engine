FROM ubuntu:22.04

# Install system dependencies
# python3-dev is needed for headers
RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    python3-dev \
    git \
    build-essential \
    librocksdb-dev \
    libpq-dev \
    zlib1g-dev \
    libbz2-dev \
    liblz4-dev \
    libsnappy-dev \
    libzstd-dev \
    && rm -rf /var/lib/apt/lists/*

# Create a symlink for python if needed, though python3 is standard
RUN ln -s /usr/bin/python3 /usr/bin/python

WORKDIR /app
COPY requirements.txt .
RUN pip3 install --no-cache-dir "Cython<3"
RUN pip3 install --no-cache-dir -r requirements.txt
COPY . .
CMD ["python3", "app.py"]