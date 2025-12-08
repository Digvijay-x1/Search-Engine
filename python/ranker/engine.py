import os
import re
import psycopg2
import numpy as np
from collections import defaultdict

# Try to import rocksdb, fallback to mock if failed (e.g. build issues)
try:
    import rocksdb
    ROCKSDB_AVAILABLE = True
except ImportError:
    ROCKSDB_AVAILABLE = False
    print("WARNING: python-rocksdb not available. Using Mock Index.")

class Ranker:
    def __init__(self):
        # 1. Connect to Postgres (Metadata)
        try:
            db_host = os.environ.get("DB_HOST", "postgres_service")
            db_name = os.environ.get("DB_NAME", "search_engine")
            db_user = os.environ.get("DB_USER")
            db_pass = os.environ.get("DB_PASS")

            if not db_user or not db_pass:
                raise ValueError("DB_USER and DB_PASS environment variables must be set.")

            self.db_conn = psycopg2.connect(
                host=db_host,
                database=db_name,
                user=db_user,
                password=db_pass
            )
            print("Connected to Postgres")
        except Exception as e:
            print(f"Failed to connect to Postgres: {e}")
            self.db_conn = None
        
        # 2. Open RocksDB (Inverted Index) - Read Only
        rocksdb_path = os.environ.get("ROCKSDB_PATH", "/shared_data/search_index.db")
        self.index_db = None
        
        if ROCKSDB_AVAILABLE:
            try:
                opts = rocksdb.Options()
                # We only need read access
                self.index_db = rocksdb.DB(rocksdb_path, opts, read_only=True)
                print(f"Opened RocksDB at {rocksdb_path}")
            except Exception as e:
                print(f"Failed to open RocksDB: {e}")
        
        # Mock Index for fallback
        self.mock_index = {
            "computer": "1,2",
            "cats": "3,4"
        }
        
        # 3. Load Global Stats (avgdl, total_docs)
        self.avgdl = self._calculate_avgdl()
        self.total_docs = self._get_total_docs()
        print(f"Ranker initialized. AvgDL: {self.avgdl}, Total Docs: {self.total_docs}")

    def _calculate_avgdl(self):
        if not self.db_conn:
            return 100.0 # Default if DB not connected
        try:
            with self.db_conn.cursor() as cur:
                cur.execute("SELECT AVG(doc_length) FROM documents")
                avg = cur.fetchone()[0]
                return float(avg) if avg else 100.0 #Default to 100 to avoid dividing by 0 
        except Exception as e:
            print(f"Error calculating avgdl: {e}")
            return 100.0

    def _get_total_docs(self):
        if not self.db_conn:
            return 1000 # Default
        try:
            with self.db_conn.cursor() as cur:
                cur.execute("SELECT COUNT(*) FROM documents")
                count = cur.fetchone()[0]
                return int(count) if count else 0
        except Exception as e:
            print(f"Error fetching total docs: {e}")
            return 1000

    def _get_doc_lengths(self, doc_ids):
        """
        Fetches document lengths for a list of doc_ids.
        Returns a dictionary: {doc_id: length}
        """
        if not self.db_conn or not doc_ids:
            return {}
        
        lengths = {}
        try:
            with self.db_conn.cursor() as cur:
                # Use tuple(doc_ids) for SQL IN clause
                # Handle single item tuple correctly
                if len(doc_ids) == 1:
                    query = "SELECT id, length FROM documents WHERE id = %s"
                    params = (doc_ids[0],)
                else:
                    query = "SELECT id, length FROM documents WHERE id IN %s"
                    params = (tuple(doc_ids),)
                
                cur.execute(query, params)
                rows = cur.fetchall()
                for r in rows:
                    lengths[r[0]] = r[1]
        except Exception as e:
            print(f"Error fetching doc lengths: {e}")
        return lengths

    def search(self, query, k=10):
        """
        Performs BM25 search for the given query.
        Returns top k results: [{'url': ..., 'title': ..., 'score': ...}]
        """
        # Preprocessing to match Indexer:
        # 1. Lowercase
        # 2. Remove non-alphanumeric (keep spaces)
        # 3. Split by whitespace
        # 4. Filter length >= 3
        
        query_clean = re.sub(r'[^a-z0-9\s]', '', query.lower())
        tokens = [t for t in query_clean.split() if len(t) >= 3]
        
        if not tokens:
            return []

        # BM25 Constants
        k1 = 1.5
        b = 0.75
        
        # Accumulate scores: doc_id -> score
        scores = defaultdict(float)
        
        # 1. Retrieve all posting lists and candidate docs
        token_postings = {} # token -> [doc_ids]
        candidate_doc_ids = set()

        for token in tokens:
            # A. Get Posting List from RocksDB or Mock
            postings_str = None
            
            if self.index_db:
                try:
                    val = self.index_db.get(token.encode('utf-8'))
                    if val:
                        postings_str = val.decode('utf-8')
                except Exception as e:
                    print(f"Error fetching token {token}: {e}")
            else:
                # Fallback to mock
                postings_str = self.mock_index.get(token)

            if not postings_str:
                continue
                
            # Format: "doc_id1,doc_id2,..." (Simplified for now, ideally should have TF)
            # For this phase, we assume TF=1 for all occurrences in the simplified index
            if isinstance(postings_str, bytes):
                postings_str = postings_str.decode('utf-8')
            
            doc_ids = [int(d) for d in postings_str.split(',')]
            token_postings[token] = doc_ids
            candidate_doc_ids.update(doc_ids)

        if not candidate_doc_ids:
            return []

        # 2. Batch fetch document lengths
        doc_lengths = self._get_doc_lengths(list(candidate_doc_ids))

        # 3. Calculate BM25 Scores
        for token in tokens:
            doc_ids = token_postings.get(token, [])
            if not doc_ids:
                continue

            # Calculate IDF
            # IDF(q_i) = log( (N - n(q_i) + 0.5) / (n(q_i) + 0.5) + 1 )
            N = self.total_docs
            if N == 0: N = 1 # Avoid division by zero issues if DB is empty
            
            n_qi = len(doc_ids)
            idf = np.log((N - n_qi + 0.5) / (n_qi + 0.5) + 1)
            
            for doc_id in doc_ids:
                # TODO: Fetch real TF from index. Currently index only stores doc_ids.
                # We assume TF=1 for now.
                tf = 1 
                
                # Get doc_len, fallback to avgdl if missing (e.g. sync issue)
                doc_len = doc_lengths.get(doc_id, self.avgdl)
                if doc_len is None or doc_len == 0:
                    doc_len = self.avgdl # Safety fallback
                
                # BM25 Score for this term
                numerator = idf * tf * (k1 + 1)
                denominator = tf + k1 * (1 - b + b * (doc_len / self.avgdl))
                scores[doc_id] += numerator / denominator

        # Sort by score
        sorted_docs = sorted(scores.items(), key=lambda item: item[1], reverse=True)[:k]
        
        # Fetch Metadata for top results
        results = []
        if self.db_conn:
            try:
                with self.db_conn.cursor() as cur:
                    for doc_id, score in sorted_docs:
                        cur.execute("SELECT url FROM documents WHERE id = %s", (doc_id,))
                        row = cur.fetchone()
                        if row:
                            results.append({
                                "id": doc_id,
                                "url": row[0],
                                "score": score,
                                "title": row[0] # Use URL as title for now
                            })
            except Exception as e:
                print(f"Error fetching metadata: {e}")
        else:
            # Fallback if DB is down
            for doc_id, score in sorted_docs:
                results.append({
                    "id": doc_id,
                    "url": f"http://mock-url.com/{doc_id}",
                    "score": score,
                    "title": f"Mock Document {doc_id}"
                })
                    
        return results

    def close(self):
        """Closes the database connection."""
        if self.db_conn:
            try:
                self.db_conn.close()
                print("Closed Postgres connection")
            except Exception as e:
                print(f"Error closing Postgres connection: {e}")
            finally:
                self.db_conn = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()
