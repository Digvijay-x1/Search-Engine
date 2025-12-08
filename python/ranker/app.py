from flask import Flask, jsonify, request
from engine import Ranker
import time
import atexit

app = Flask(__name__)

# Initialize Ranker (Global Singleton)
ranker = None
try:
    ranker = Ranker()
    atexit.register(ranker.close)
except Exception as e:
    print(f"Failed to initialize Ranker: {e}")

@app.route('/health')
def health():
    status = "healthy" if ranker else "degraded"
    return jsonify({"status": status, "service": "ranker"})

@app.route('/search')
def search():
    global ranker
    if not ranker:
        # Fallback for dev/restart if before_first_request didn't fire or failed
        try:
            ranker = Ranker()
        except Exception as e:
            return jsonify({"error": f"Ranker not initialized: {str(e)}"}), 500

    query = request.args.get('q', '').lower()
    print(f"Received query: {query}")
    
    start_time = time.time()
    results = ranker.search(query)
    duration_ms = (time.time() - start_time) * 1000
    
    return jsonify({
        "query": query, 
        "results": results,
        "meta": {
            "count": len(results),
            "latency_ms": round(duration_ms, 2)
        }
    })

if __name__ == '__main__':
    # host='0.0.0.0' is CRITICAL for Docker networking
    app.run(host='0.0.0.0', port=5000, debug=True)