# Google-Like Search Engine - Architecture Plan

**Project Overview:** A multi-language distributed search engine implementing web crawling, indexing, TF-IDF ranking, and a web interface.

**Technology Stack:**
- **C++**: High-performance crawler and indexer (concurrency, speed)
- **Python**: Mathematical operations (TF-IDF ranking, numpy/scipy)
- **Ruby on Rails**: Web interface, API, orchestration

---

## Phase 1: The Spider (Web Crawler)

### Goal
Download web pages and extract links to discover more pages using BFS traversal.

### Technology: C++

**Why C++?**
- Superior concurrency with `std::thread` or `Boost.Asio`
- High-performance I/O operations
- Low memory footprint for managing millions of URLs
- Thread-safe data structures with `std::mutex` and `std::atomic`

### Components

#### 1.1 Queue & Seed
- **Data Structure**: `std::queue<std::string>` for BFS traversal
- **Seed URLs**: Start with configurable seeds (e.g., wikipedia.org, news sites)
- **Visited Set**: `std::unordered_set<std::string>` with mutex protection

#### 1.2 The Fetcher (Networking)
- **Library**: `libcurl` for HTTP GET requests
- **Features**:
  - Do not Respect `robots.txt` 
  - HTTP headers (User-Agent, Accept)
  - Timeout handling (30s default)
  - Retry logic with exponential backoff

#### 1.3 The Parser
- **Library**: `gumbo-parser` for HTML parsing
- **Extraction**:
  - Raw text content (strip HTML tags)
  - All `<a href="...">` links
  - Page title from `<title>` tag
  - Meta description

#### 1.4 Concurrency (OS)
- **Thread Pool**: 100-500 worker threads (configurable)
- **Synchronization**:
  - `std::mutex` for visited URLs set
  - `std::condition_variable` for queue notifications
  - Lock-free queues (optional): `boost::lockfree::queue`
- **Rate Limiting**: Per-domain politeness (5 request/second)
- **Domain-specific delays**: Configurable in `config/crawler.json`

#### 1.5 Storage
- **Output Path**: `data/pages/doc_{id}.txt`
- **Metadata**: `data/metadata.json`
  ```json
  {
    "doc_id": 1,
    "url": "https://example.com/page",
    "title": "Example Page",
    "fetch_time": "2025-11-29T10:30:00Z",
    "word_count": 1500
  }
  ```

### Deliverable
- Executable: `crawler.exe` (Windows) or `./crawler` (Linux/Mac)
- Fetches 100+ pages
- Prints: `[Thread-5] Fetching https://example.com...`
- Respects robots.txt
- Handles network errors gracefully

---

## Phase 2: The Processor (Indexer)

### Goal
Convert raw text into inverted index for instant search.

### Technology: C++

**Why C++?**
- Fast text processing (millions of tokens per second)
- Efficient data structures (`std::unordered_map`, custom Trie)
- Minimal memory overhead for large indexes
- Multi-threaded document processing

### Components

#### 2.1 Text Pre-processing

**Tokenization**
```cpp
std::vector<std::string> tokenize(const std::string& text) {
    // Split on whitespace and punctuation
    // Return vector of tokens
}
```

**Normalization**
- Convert to lowercase
- Remove punctuation
- Handle Unicode (UTF-8)

**Stop Words**
- Load from `config/stopwords.txt`
- Common words: "the", "is", "at", "which", "on"
- Use `std::unordered_set` for O(1) lookup

**Stemming (Optional)**
- Porter Stemmer algorithm
- "running" → "run", "computers" → "comput"

#### 2.2 The Inverted Index

**Data Structure**
```cpp
std::unordered_map<std::string, std::unordered_set<int>> inverted_index;
// Key: word (string)
// Value: set of document IDs containing the word
```

**Example**
```
"computer" -> {1, 5, 99, 230}
"science" -> {1, 12, 99}
"algorithm" -> {5, 12, 67}
```

**Advanced: Trie for Autocomplete**
```cpp
struct TrieNode {
    std::unordered_map<char, TrieNode*> children;
    bool is_end_of_word;
    std::unordered_set<int> doc_ids;
};
```

#### 2.3 Multi-threading
- Thread pool processes documents in parallel
- Each thread:
  1. Reads a document from `data/pages/`
  2. Tokenizes and normalizes
  3. Updates shared inverted index (with mutex)
- Lock contention optimization: per-thread local indexes, then merge

#### 2.4 Storage
- **Binary Format**: `data/inverted_index.bin` (fast loading)
- **JSON Export**: `data/inverted_index.json` (Python readable)
- **Document Map**: `data/doc_map.json`
  ```json
  {
    "1": {"url": "...", "title": "...", "word_count": 1500},
    "5": {"url": "...", "title": "...", "word_count": 2300}
  }
  ```

### Deliverable
- Executable: `indexer.exe`
- Processes all documents in `data/pages/`
- Outputs inverted index
- Prints: `Indexing doc_1.txt... Found 1234 unique words`
- Completion time: < 10 seconds for 1000 documents

---

## Phase 3: The Brain (Ranking Algorithm)

### Goal
Rank documents by relevance using TF-IDF scoring.

### Technology: Python

**Why Python?**
- Rich scientific computing libraries (`numpy`, `scipy`)
- Readable mathematical operations
- Rapid prototyping of ranking algorithms
- Easy integration with Rails via subprocess or REST API

### Components

#### 3.1 Term Frequency (TF)

**Formula**
$$TF(t, d) = \frac{\text{count of term } t \text{ in document } d}{\text{total words in document } d}$$

**Implementation**
```python
def term_frequency(term, doc_id):
    count = term_counts[doc_id].get(term, 0)
    total_words = doc_metadata[doc_id]['word_count']
    return count / total_words
```

#### 3.2 Inverse Document Frequency (IDF)

**Formula**
$$IDF(t) = \log\left(\frac{N}{df_t}\right)$$

- **N**: Total number of documents
- **df_t**: Number of documents containing term *t*

**Implementation**
```python
import numpy as np

def inverse_document_frequency(term):
    N = total_documents
    df = len(inverted_index.get(term, []))
    if df == 0:
        return 0
    return np.log(N / df)
```

#### 3.3 The Scoring Function

**Formula**
$$Score(q, d) = \sum_{t \in q} TF(t, d) \times IDF(t)$$

For query "Blue Car":
- Calculate score for "blue" in each document
- Calculate score for "car" in each document
- Sum the scores
- Sort documents by total score (descending)

**Implementation**
```python
def search(query_string):
    terms = preprocess(query_string)  # tokenize, lowercase, remove stopwords
    scores = {}
    
    for term in terms:
        idf = inverse_document_frequency(term)
        doc_ids = inverted_index.get(term, [])
        
        for doc_id in doc_ids:
            tf = term_frequency(term, doc_id)
            scores[doc_id] = scores.get(doc_id, 0) + (tf * idf)
    
    # Sort by score descending
    ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)
    return ranked[:10]  # Top 10 results
```

#### 3.4 Optimizations
- **Pre-compute IDF**: Calculate once during indexing, store in `data/idf_cache.json`
- **Sparse Matrices**: Use `scipy.sparse` for large indexes
- **Caching**: Memoize popular queries with LRU cache

#### 3.5 Integration with Rails
- **Option 1: Subprocess**
  ```ruby
  result = `python python/ranker/search.py "#{query}"`
  JSON.parse(result)
  ```
- **Option 2: REST Microservice** (Recommended)
  - Flask/FastAPI service on `localhost:5000`
  - Endpoint: `GET /rank?q=query`
  - Rails calls via `HTTParty.get()`

### Deliverable
- Python module: `python/ranker/search.py`
- CLI usage: `python search.py "computer science"`
- Output: JSON array of `{doc_id, url, title, score, snippet}`
- Response time: < 100ms for typical queries

---

## Phase 4: The Interface (Web & System Design)

### Goal
Present search results to users through a web interface.

### Technology: Ruby on Rails

**Why Ruby on Rails?**
- Full-stack framework (MVC, routing, views)
- Rapid development with conventions
- Built-in asset pipeline for frontend
- Easy orchestration of C++/Python components

### Components

#### 4.1 The API (Backend)

**Rails Controller**
```ruby
# app/controllers/search_controller.rb
class SearchController < ApplicationController
  def index
    @query = params[:q]
    
    if @query.present?
      # Call Python ranker microservice
      response = HTTParty.get("http://localhost:5000/rank", {
        query: { q: @query }
      })
      
      @results = JSON.parse(response.body)
    else
      @results = []
    end
  end
end
```

**Routes**
```ruby
# config/routes.rb
Rails.application.routes.draw do
  root 'search#home'
  get '/search', to: 'search#index'
  get '/health', to: 'health#check'
end
```

**API Response Format**
```json
{
  "query": "computer science",
  "results": [
    {
      "doc_id": 1,
      "url": "https://example.com/cs",
      "title": "Computer Science Fundamentals",
      "snippet": "Computer science is the study of...",
      "score": 8.45
    }
  ],
  "total_results": 42,
  "time_ms": 85
}
```

#### 4.2 The Frontend

**Home Page** (`app/views/search/home.html.erb`)
- Centered search box (Google-style)
- Logo
- Minimalist design

```erb
<div class="search-container">
  <h1 class="logo">Search Engine</h1>
  <%= form_with url: search_path, method: :get, class: "search-form" do |f| %>
    <%= f.text_field :q, placeholder: "Search...", class: "search-input", autofocus: true %>
    <%= f.submit "Search", class: "search-button" %>
  <% end %>
</div>
```

**Results Page** (`app/views/search/index.html.erb`)
```erb
<div class="results-container">
  <div class="search-header">
    <%= form_with url: search_path, method: :get, class: "search-form-compact" do |f| %>
      <%= f.text_field :q, value: @query, class: "search-input-compact" %>
      <%= f.submit "Search", class: "search-button-compact" %>
    <% end %>
  </div>

  <div class="results-stats">
    About <%= @results.length %> results
  </div>

  <div class="results-list">
    <% @results.each do |result| %>
      <div class="result-item">
        <a href="<%= result['url'] %>" class="result-title">
          <%= result['title'] %>
        </a>
        <div class="result-url"><%= result['url'] %></div>
        <div class="result-snippet"><%= result['snippet'] %></div>
      </div>
    <% end %>
  </div>
</div>
```

**Styling** (`app/assets/stylesheets/search.css`)
- Google-inspired design
- Responsive layout
- Clean typography

#### 4.3 Snippet Generation
```ruby
# app/services/snippet_service.rb
class SnippetService
  def self.generate(text, query, max_length = 160)
    # Find query terms in text
    # Extract surrounding context
    # Highlight matching terms
    # Truncate to max_length
  end
end
```

### Deliverable
- Rails application running on `localhost:3000`
- Home page with search box
- Results page with titles, URLs, snippets
- Responsive design (mobile-friendly)
- Fast page loads (< 200ms)

---

## Phase 5: Inter-Process Communication & Integration

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        User Browser                         │
│                     (HTML/CSS/JavaScript)                   │
└───────────────────────────────┬─────────────────────────────┘
                                │ HTTP
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                    Ruby on Rails Server                     │
│                         (Port 3000)                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ Controllers  │  │    Views     │  │    Models    │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└───────────────────────────────┬─────────────────────────────┘
                                │ HTTP REST API
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                  Python Ranking Microservice                │
│                         (Port 5000)                         │
│                      Flask/FastAPI                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │  TF-IDF Calc │  │ Query Parser │  │ Result Cache │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└───────────────────────────────┬─────────────────────────────┘
                                │ Reads Files
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                        Data Storage                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  data/inverted_index.json (from C++ indexer)         │   │
│  │  data/doc_map.json (document metadata)               │   │
│  │  data/idf_cache.json (pre-computed IDF values)       │   │
│  │  data/pages/ (raw HTML documents)                    │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────────────┬─────────────────────────┘
                                    │ Written by
                                    ▼
┌─────────────────────────────────────────────────────────────┐
│                    C++ Components (Offline)                 │
│  ┌──────────────┐                  ┌──────────────┐         │
│  │   Crawler    │  (crawler.exe)   │   Indexer    │         │
│  │ Multi-thread │  ────────────>   │ Multi-thread │         │
│  │  BFS Queue   │   Fetches HTML   │ Inverted Idx │         │
│  └──────────────┘                  └──────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### Communication Methods

#### 1. Rails → Python (Runtime)
**Option A: REST Microservice (Recommended)**
```ruby
# Gemfile
gem 'httparty'

# app/services/ranking_service.rb
class RankingService
  BASE_URL = "http://localhost:5000"
  
  def self.search(query)
    response = HTTParty.get("#{BASE_URL}/rank", {
      query: { q: query },
      timeout: 5
    })
    JSON.parse(response.body)
  rescue => e
    Rails.logger.error("Ranking service error: #{e.message}")
    []
  end
end
```

**Python Flask Microservice**
```python
# python/ranker/app.py
from flask import Flask, request, jsonify
import search_engine

app = Flask(__name__)

@app.route('/rank', methods=['GET'])
def rank():
    query = request.args.get('q', '')
    results = search_engine.search(query)
    return jsonify(results)

if __name__ == '__main__':
    app.run(port=5000)
```

**Option B: Subprocess (Simpler, but slower)**
```ruby
def self.search(query)
  result = `python python/ranker/search.py "#{query}"`
  JSON.parse(result)
end
```

#### 2. Rails → C++ (Build Time)
**Rake Tasks for Pipeline Orchestration**
```ruby
# lib/tasks/search_engine.rake
namespace :crawler do
  desc "Run the C++ web crawler"
  task :run => :environment do
    system("cpp/build/crawler.exe --config config/crawler.json")
  end
end

namespace :indexer do
  desc "Build the inverted index from crawled pages"
  task :build => :environment do
    system("cpp/build/indexer.exe --input data/pages --output data/inverted_index.json")
  end
end

namespace :pipeline do
  desc "Run full pipeline: crawl -> index -> start services"
  task :full => :environment do
    Rake::Task['crawler:run'].invoke
    Rake::Task['indexer:build'].invoke
    puts "Starting Python ranking service..."
    system("python python/ranker/app.py &")
    puts "Starting Rails server..."
    system("rails server")
  end
end
```

#### 3. C++ → Python (Data Files)
- C++ writes JSON files
- Python reads JSON files
- Shared data directory: `data/`
- Optional: Use MessagePack or Protocol Buffers for binary serialization

---

## Project Structure

```
Google/
├── cpp/
│   ├── crawler/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── fetcher.cpp
│   │   ├── fetcher.h
│   │   ├── parser.cpp
│   │   ├── parser.h
│   │   ├── robots_parser.cpp
│   │   └── robots_parser.h
│   ├── indexer/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── tokenizer.cpp
│   │   ├── tokenizer.h
│   │   ├── inverted_index.cpp
│   │   └── inverted_index.h
│   └── build/
│       ├── crawler.exe
│       └── indexer.exe
├── python/
│   ├── ranker/
│   │   ├── __init__.py
│   │   ├── app.py (Flask microservice)
│   │   ├── search_engine.py (TF-IDF logic)
│   │   ├── preprocessing.py
│   │   └── search.py (CLI interface)
│   └── requirements.txt
├── rails/
│   ├── app/
│   │   ├── controllers/
│   │   │   ├── search_controller.rb
│   │   │   └── health_controller.rb
│   │   ├── views/
│   │   │   ├── search/
│   │   │   │   ├── home.html.erb
│   │   │   │   └── index.html.erb
│   │   │   └── layouts/
│   │   │       └── application.html.erb
│   │   ├── services/
│   │   │   ├── ranking_service.rb
│   │   │   └── snippet_service.rb
│   │   └── assets/
│   │       ├── stylesheets/
│   │       │   └── search.css
│   │       └── javascripts/
│   │           └── search.js
│   ├── config/
│   │   ├── routes.rb
│   │   └── database.yml
│   ├── lib/
│   │   └── tasks/
│   │       └── search_engine.rake
│   ├── Gemfile
│   └── Gemfile.lock
├── data/
│   ├── pages/
│   │   ├── doc_1.txt
│   │   ├── doc_2.txt
│   │   └── ...
│   ├── inverted_index.json
│   ├── inverted_index.bin
│   ├── doc_map.json
│   ├── metadata.json
│   ├── idf_cache.json
│   └── crawler_state.json
├── config/
│   ├── crawler.json
│   ├── indexer.json
│   └── stopwords.txt
├── scripts/
│   ├── build_cpp.sh
│   ├── start_services.sh
│   └── run_pipeline.sh
├── tests/
│   ├── cpp_tests/
│   ├── python_tests/
│   └── rails_tests/
├── .gitignore
├── README.md
├── Makefile
└── ARCHITECTURE.md (this file)
```

---

## Build & Deployment

### Prerequisites

**C++ Development**
- CMake 3.15+
- C++17 compiler (MSVC, GCC, or Clang)
- libcurl development libraries
- gumbo-parser

**Python**
- Python 3.8+
- pip

**Ruby on Rails**
- Ruby 3.0+
- Rails 7.0+
- Bundler

### Build Commands

**1. Build C++ Components**
```bash
# Windows (PowerShell)
cd cpp\crawler
cmake -B build -S .
cmake --build build --config Release

cd ..\indexer
cmake -B build -S .
cmake --build build --config Release
```

**2. Install Python Dependencies**
```bash
cd python\ranker
pip install -r requirements.txt
```

**3. Setup Rails Application**
```bash
cd rails
bundle install
rails db:create
rails db:migrate
```

### Running the Pipeline

**Step 1: Crawl Web Pages**
```bash
.\cpp\build\crawler.exe --config config\crawler.json
```

**Step 2: Build Inverted Index**
```bash
.\cpp\build\indexer.exe --input data\pages --output data\inverted_index.json
```

**Step 3: Start Python Ranking Service**
```bash
python python\ranker\app.py
```

**Step 4: Start Rails Server**
```bash
cd rails
rails server
```

**Step 5: Access Application**
Open browser: `http://localhost:3000`

---

## Configuration Files

### crawler.json
```json
{
  "seed_urls": [
    "https://en.wikipedia.org/wiki/Computer_science",
    "https://news.ycombinator.com"
  ],
  "max_pages": 1000,
  "max_depth": 3,
  "thread_count": 200,
  "request_timeout": 30,
  "rate_limit": {
    "requests_per_second": 10,
    "per_domain_delay_ms": 1000
  },
  "user_agent": "CustomSearchBot/1.0",
  "respect_robots_txt": true,
  "output_dir": "data/pages"
}
```

### indexer.json
```json
{
  "input_dir": "data/pages",
  "output_file": "data/inverted_index.json",
  "binary_output": "data/inverted_index.bin",
  "thread_count": 8,
  "stopwords_file": "config/stopwords.txt",
  "enable_stemming": true,
  "min_word_length": 3,
  "max_word_length": 50
}
```

### stopwords.txt
```
a
an
and
are
as
at
be
by
for
from
has
he
in
is
it
its
of
on
that
the
to
was
will
with
```

---

## Performance Targets

### Crawler (C++)
- **Throughput**: 100-500 pages/minute
- **Concurrency**: 200-500 threads
- **Memory**: < 2GB for 10,000 URLs in queue
- **Network**: Handle timeouts, retries, redirects

### Indexer (C++)
- **Speed**: 1,000 documents/second
- **Index Size**: 100MB for 10,000 documents
- **Memory**: < 4GB during indexing
- **Startup**: Load index in < 1 second

### Ranking (Python)
- **Query Speed**: < 100ms for typical queries
- **Throughput**: 100 queries/second
- **Memory**: < 1GB for loaded index
- **Cache Hit Rate**: > 80% for popular queries

### Web Interface (Rails)
- **Page Load**: < 200ms (TTFB)
- **Concurrent Users**: 100+
- **Uptime**: 99.9%

---

## Testing Strategy

### Unit Tests

**C++ (Google Test)**
```cpp
TEST(FetcherTest, HandlesRobotsTxt) {
  Fetcher fetcher;
  EXPECT_FALSE(fetcher.is_allowed("https://example.com/admin"));
  EXPECT_TRUE(fetcher.is_allowed("https://example.com/public"));
}
```

**Python (pytest)**
```python
def test_tf_idf_calculation():
    ranker = SearchEngine()
    score = ranker.calculate_tf_idf("computer", 1)
    assert score > 0
```

**Rails (RSpec)**
```ruby
describe SearchController do
  it "returns results for valid query" do
    get :index, params: { q: "computer" }
    expect(response).to be_successful
    expect(assigns(:results)).not_to be_empty
  end
end
```

### Integration Tests
- Full pipeline test: crawl → index → search
- API endpoint testing
- Error handling (network failures, malformed HTML)

### Performance Tests
- Benchmark crawler speed
- Load testing (Apache Bench, wrk)
- Memory profiling (Valgrind, Python memory_profiler)

---

## Future Enhancements

### Phase 5: Advanced Features
1. **PageRank Algorithm** (Python/C++)
2. **Autocomplete/Suggestions** (Trie in C++)
3. **Spell Correction** (Levenshtein distance)
4. **Filters**: Date range, domain, file type
5. **GraphQL API** (Rails + GraphQL Ruby gem)
6. **User Accounts & Search History** (Rails ActiveRecord)

### Phase 6: Scalability
1. **Distributed Crawling** (Multiple crawler instances)
2. **Redis Cache** (Query result caching)
3. **PostgreSQL** (Replace JSON files)
4. **Elasticsearch** (Alternative to custom indexer)
5. **Docker Deployment**
6. **Load Balancing** (Multiple Rails instances)

### Phase 7: Machine Learning
1. **Learning to Rank** (LambdaMART)
2. **Query Understanding** (NLP, BERT)
3. **Personalization** (User behavior tracking)
4. **Click-through Rate Optimization**

---

## Resources & Dependencies

### C++ Libraries
- **libcurl**: HTTP client ([curl.se](https://curl.se))
- **gumbo-parser**: HTML5 parser ([github.com/google/gumbo-parser](https://github.com/google/gumbo-parser))
- **Boost.Asio**: Async I/O (optional) ([boost.org](https://boost.org))
- **nlohmann/json**: JSON parsing ([github.com/nlohmann/json](https://github.com/nlohmann/json))

### Python Libraries
```txt
# python/requirements.txt
numpy>=1.24.0
scipy>=1.10.0
flask>=2.3.0
flask-cors>=4.0.0
nltk>=3.8.0
```

### Ruby Gems
```ruby
# rails/Gemfile
gem 'httparty'      # HTTP client
gem 'redis'         # Caching
gem 'sidekiq'       # Background jobs
```

### Installation Guides
- **Windows**: Use vcpkg for C++ dependencies
- **Linux**: `apt-get install libcurl4-openssl-dev libgumbo-dev`
- **macOS**: `brew install curl gumbo-parser`

---

## Timeline Estimate

| Phase | Tasks | Estimated Time |
|-------|-------|----------------|
| **Phase 1: Crawler** | C++ setup, networking, concurrency, robots.txt | 2-3 weeks |
| **Phase 2: Indexer** | Tokenization, inverted index, multi-threading | 1-2 weeks |
| **Phase 3: Ranker** | TF-IDF implementation, Python service | 1 week |
| **Phase 4: Interface** | Rails app, views, API integration | 1-2 weeks |
| **Phase 5: Integration** | IPC, testing, debugging | 1 week |
| **Total** | | **6-9 weeks** |

---

## Success Metrics

- [ ] Crawler fetches 1,000+ pages without crashing
- [ ] Indexer processes documents in < 10 seconds
- [ ] Search returns results in < 100ms
- [ ] Web interface loads in < 200ms
- [ ] All unit tests pass
- [ ] Code documented and readable
- [ ] Handles edge cases (network errors, malformed HTML, empty queries)

---

## Conclusion

This architecture leverages the strengths of each language:
- **C++** for raw speed and concurrency in I/O-bound operations
- **Python** for mathematical clarity in ranking algorithms
- **Ruby on Rails** for rapid web development and orchestration

The modular design allows independent development and testing of each component, with clean interfaces between languages. The system is designed for learning, with clear extension points for advanced features.

**Next Steps:**
1. Setup development environment (C++, Python, Ruby)
2. Implement Phase 1 (Crawler) - Start with basic single-threaded version
3. Add concurrency incrementally
4. Build remaining phases iteratively
5. Test and optimize

---

*Last Updated: November 29, 2025*
