
# IGI

![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/Digvijay-x1/IGI?utm_source=oss&utm_medium=github&utm_campaign=Digvijay-x1%2FIGI&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)


A high-performance, distributed search engine designed to crawl, index, and rank web content at scale. This project implements a Google-like search architecture using microservices, featuring a C++ crawler and indexer, Python-based BM25 ranking algorithm, and Ruby on Rails web interface.

## 📋 Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Technology Stack](#technology-stack)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
  - [Configuration](#configuration)
- [Usage](#usage)
- [API Documentation](#api-documentation)
- [Development](#development)
- [Architecture Details](#architecture-details)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

## ✨ Features

- **Distributed Web Crawler**: High-performance C++ crawler with politeness controls and robots.txt support
- **Efficient Indexing**: Inverted index using RocksDB for fast lookups
- **BM25 Ranking Algorithm**: Advanced probabilistic ranking for relevant search results
- **Microservices Architecture**: Event-driven, queue-based system for scalability
- **WARC Storage**: Efficient HTML storage format with random access support
- **Docker Support**: Containerized deployment with Docker Compose
- **Redis Caching**: Fast query response with intelligent caching
- **RESTful API**: Clean API interface for search queries

## 🏗️ Architecture

This project follows a microservices-based, event-driven architecture with four main components:

```
┌─────────────┐
│    User     │
└──────┬──────┘
       │
       ▼
┌─────────────────┐      ┌──────────────┐
│  Rails Interface│─────▶│ Redis Cache  │
│   (Port 3000)   │      └──────────────┘
└────────┬────────┘
         │
         ▼
┌─────────────────┐      ┌──────────────┐
│ Python Ranker   │─────▶│  PostgreSQL  │
│   (Port 5000)   │      │   Metadata   │
└─────────────────┘      └──────────────┘
         │
         ▼
    ┌────────────┐
    │  RocksDB   │
    │   Index    │
    └────────────┘

Offline Processing:
┌──────────────┐      ┌──────────────┐
│ C++ Crawler  │─────▶│ Message Queue│
└──────────────┘      │   (Redis)    │
                      └──────┬───────┘
                             │
                             ▼
                      ┌──────────────┐
                      │ C++ Indexer  │
                      └──────────────┘
```

## 🛠️ Technology Stack

| Component        | Technology            | Purpose                                    |
| :--------------- | :-------------------- | :----------------------------------------- |
| **Crawler**      | C++ (C++17/20)        | High concurrency, low memory footprint     |
| **Indexer**      | C++                   | Fast string processing, I/O optimization   |
| **Ranker**       | Python (NumPy/Flask)  | BM25 algorithm, matrix operations          |
| **Interface**    | Ruby on Rails 8.0     | MVC framework, API orchestration           |
| **Message Queue**| Redis                 | Asynchronous task processing               |
| **Metadata DB**  | PostgreSQL 17.2       | Structured data storage                    |
| **Index Store**  | RocksDB               | High-performance key-value storage         |
| **Containerization** | Docker & Docker Compose | Easy deployment and scaling            |

## 📁 Project Structure

```
Search-Engine/
├── cpp/
│   ├── crawler/          # C++ web crawler
│   │   ├── src/
│   │   │   ├── main.cpp
│   │   │   ├── warc_writer.cpp
│   │   │   └── warc_writer.hpp
│   │   ├── tests/
│   │   └── Dockerfile
│   └── indexer/          # C++ indexer
│       ├── src/
│       ├── tests/
│       └── Dockerfile
├── python/
│   └── ranker/           # Python ranking service
│       ├── app.py        # Flask application
│       ├── engine.py     # BM25 ranking logic
│       ├── requirements.txt
│       └── Dockerfile
├── API/                  # Ruby on Rails interface
│   ├── app/
│   ├── config/
│   ├── Gemfile
│   └── README.md
├── data/
│   ├── init.sql          # Database initialization
│   └── crawled_pages/    # WARC storage
├── docker-compose.yml    # Service orchestration
├── .env.example          # Environment variables template
├── ARCHITECTURE.md       # Detailed architecture documentation
└── README.md             # This file
```

## 🚀 Getting Started

### Prerequisites

- **Docker** (version 20.10 or higher)
- **Docker Compose** (version 2.0 or higher)
- At least 4GB of available RAM
- 10GB of free disk space

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/Digvijay-x1/Search-Engine.git
   cd Search-Engine
   ```

2. **Set up environment variables**
   ```bash
   cp .env.example .env
   ```

3. **Edit `.env` file with your configuration**
   ```ini
   DB_USER=admin
   DB_PASS=your_secure_password
   DB_NAME=search_engine
   ```
   > ⚠️ **Important**: Change the default password in production!

4. **Build and start all services**
   ```bash
   docker-compose up --build
   ```

   This will start all services:
   - **Rails Interface**: http://localhost:3000
   - **Python Ranker API**: http://localhost:5000
   - **PostgreSQL**: localhost:5434
   - **Redis**: localhost:6380
   - **Crawler** and **Indexer**: Running in background

### Configuration

The project uses environment variables for configuration. Key variables include:

- `DB_USER`: Database username
- `DB_PASS`: Database password
- `DB_NAME`: Database name
- `DB_HOST`: Database host (defaults to `postgres_service` in Docker)
- `FLASK_ENV`: Flask environment (development/production)
- `ROCKSDB_PATH`: Path to RocksDB index files

## 📖 Usage

### Accessing the Search Interface

Once all services are running, navigate to:
```
http://localhost:3000
```

### Using the Search API

The Python ranker service exposes a REST API:

**Health Check**
```bash
curl http://localhost:5000/health
```

**Search Query**
```bash
curl "http://localhost:5000/search?q=your+search+query"
```

**Response Format**
```json
{
  "query": "your search query",
  "results": [
    {
      "id": 1,
      "url": "https://example.com",
      "title": "Example Page",
      "snippet": "Relevant snippet from the page...",
      "score": 4.52
    }
  ],
  "meta": {
    "count": 10,
    "latency_ms": 23.45
  }
}
```

## 📚 API Documentation

### Ranker Service API

#### `GET /health`
Check the health status of the ranker service.

**Response:**
```json
{
  "status": "healthy",
  "service": "ranker"
}
```

#### `GET /search`
Execute a search query.

**Query Parameters:**
- `q` (required): Search query string

**Response:**
- `query`: The original search query
- `results`: Array of ranked search results
  - `id`: Document ID
  - `url`: Page URL
  - `title`: Page title
  - `snippet`: Text preview
  - `score`: BM25 relevance score
- `meta`: Metadata about the search
  - `count`: Number of results
  - `latency_ms`: Query processing time

## 🔧 Development

### Running Individual Services

**Start only the ranker service:**
```bash
docker-compose up ranker_service postgres_service redis_service
```

**Start only the crawler:**
```bash
docker-compose up crawler_service redis_service postgres_service
```

### Building Components Locally

**Python Ranker:**
```bash
cd python/ranker
pip install -r requirements.txt
python app.py
```

**C++ Crawler:**
```bash
cd cpp/crawler
mkdir build && cd build
cmake ../src
make
./crawler
```

### Viewing Logs

```bash
# All services
docker-compose logs -f

# Specific service
docker-compose logs -f ranker_service
docker-compose logs -f crawler_service
docker-compose logs -f rails_interface
```

### Database Access

**Connect to PostgreSQL:**
```bash
docker-compose exec postgres_service psql -U admin -d search_engine
```

**View crawled documents:**
```sql
SELECT id, url, title, status FROM documents LIMIT 10;
```

### Stopping Services

```bash
# Stop all services
docker-compose down

# Stop and remove volumes (clears database)
docker-compose down -v
```

## 🏛️ Architecture Details

For comprehensive architecture documentation, see [ARCHITECTURE.md](ARCHITECTURE.md).

### Key Components

1. **Web Crawler (C++)**
   - Implements URL frontier with Bloom filter for visited check
   - Respects robots.txt and rate limiting
   - Stores content in WARC format
   - Handles DNS caching and connection pooling

2. **Indexer (C++)**
   - Tokenizes and processes HTML content
   - Builds inverted index in RocksDB
   - Calculates document statistics for BM25
   - Implements Porter2 stemming algorithm

3. **Ranker (Python)**
   - BM25 (Okapi) ranking algorithm
   - Vectorized operations with NumPy
   - Memory-mapped index access
   - Redis caching for frequent queries

4. **Web Interface (Ruby on Rails)**
   - Query orchestration
   - Result formatting and snippet generation
   - Cache management
   - User interface

### Data Flow

1. **Crawling Phase** (Offline):
   - Crawler fetches pages → Stores in WARC files
   - Metadata saved to PostgreSQL
   - Jobs queued in Redis

2. **Indexing Phase** (Offline):
   - Indexer reads WARC files
   - Extracts and tokenizes content
   - Updates inverted index in RocksDB
   - Updates document metadata

3. **Search Phase** (Online):
   - User submits query via Rails interface
   - Rails checks Redis cache
   - If miss: Calls Python ranker API
   - Ranker queries RocksDB index
   - Returns ranked document IDs
   - Rails fetches metadata from PostgreSQL
   - Results displayed to user

## 🐛 Troubleshooting

### Common Issues

**Issue: Services won't start**
```bash
# Check Docker is running
docker ps

# Check logs for errors
docker-compose logs

# Rebuild containers
docker-compose down
docker-compose up --build
```

**Issue: Database connection errors**
```bash
# Verify environment variables
cat .env

# Check PostgreSQL is running
docker-compose ps postgres_service

# Restart database service
docker-compose restart postgres_service
```

**Issue: Port already in use**
```bash
# Find process using port
lsof -i :3000
lsof -i :5000

# Kill the process or change port in docker-compose.yml
```

**Issue: Out of memory**
```bash
# Increase Docker memory limit in Docker Desktop settings
# Or reduce number of running services
```

### Checking Service Health

```bash
# Check all running containers
docker-compose ps

# Test ranker API
curl http://localhost:5000/health

# Test Rails interface
curl http://localhost:3000
```

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

### Development Guidelines

- Follow existing code style and conventions
- Write meaningful commit messages
- Add tests for new features
- Update documentation as needed
- Ensure Docker builds succeed

## 📄 License

This project is open source and available under the MIT License.

## 🙏 Acknowledgments

- Inspired by Google's original search engine architecture
- Built with modern microservices best practices
- Uses industry-standard algorithms (BM25, Porter2 Stemmer)

## 📞 Support

For questions and support:
- Open an issue on GitHub
- Check the [ARCHITECTURE.md](ARCHITECTURE.md) for detailed technical information
- Review the API README: [API/README.md](API/README.md)

---

**Built with ❤️ by the Search Engine Team**
