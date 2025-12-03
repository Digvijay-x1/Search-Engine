#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <pqxx/pqxx>
#include <hiredis/hiredis.h>

// --- Config ---
const std::string REDIS_HOST = "redis_service";
const std::string DB_CONN_STR = "dbname=search_engine user=admin password=password123 host=postgres_service port=5432";
const std::string SEED_URL = "https://en.wikipedia.org/wiki/Main_Page";

// --- CURL Callback ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// --- Helper: Download URL ---
std::string download_url(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if(res != CURLE_OK) {
            std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    return readBuffer;
}

int main() {
    std::cout << "--- Crawler Service Started ---" << std::endl;

    // 1. Connect to Redis
    redisContext *redis = redisConnect(REDIS_HOST.c_str(), 6379);
    if (redis == NULL || redis->err) {
        std::cerr << "Redis connection failed" << std::endl;
        return 1;
    }
    std::cout << "Connected to Redis" << std::endl;

    // 2. Connect to Postgres (With Retry Logic)
    pqxx::connection* C = nullptr;
    int retries = 10;
    while (retries > 0) {
        try {
            C = new pqxx::connection(DB_CONN_STR);
            if (C->is_open()) {
                std::cout << "Connected to DB: " << C->dbname() << std::endl;
                break;
            }
        } catch (const std::exception &e) {
            std::cerr << "Postgres connection attempt failed: " << e.what() << std::endl;
            if (C) { delete C; C = nullptr; }
        }
        std::cout << "Retrying Postgres connection in 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        retries--;
    }

    if (!C || !C->is_open()) {
        std::cerr << "Failed to connect to Postgres after retries." << std::endl;
        return 1;
    }

    // 3. Seed the Queue (if empty)
    redisReply *reply = (redisReply*)redisCommand(redis, "LLEN crawl_queue");
    if (reply->integer == 0) {
        std::cout << "Queue empty. Seeding: " << SEED_URL << std::endl;
        freeReplyObject(reply);
        // Push seed to Redis List (RPUSH)
        reply = (redisReply*)redisCommand(redis, "RPUSH crawl_queue %s", SEED_URL.c_str());
    }
    freeReplyObject(reply);

    // 4. The Infinite Crawl Loop
    while (true) {
        // A. Pop URL from Redis (LPOP)
        reply = (redisReply*)redisCommand(redis, "LPOP crawl_queue");
        
        if (reply == NULL || reply->type == REDIS_REPLY_NIL) {
            // std::cout << "Queue empty. Waiting 5s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (reply) freeReplyObject(reply);
            continue;
        }

        std::string url = reply->str;
        freeReplyObject(reply);
        std::cout << "Fetching: " << url << std::endl;

        // B. Insert into DB "Pending" to get an ID (and handle duplicates)
        int doc_id = -1;
        try {
            pqxx::work W(*C);
            // Insert and return ID. If URL exists, do nothing.
            pqxx::result R = W.exec_params(
                "INSERT INTO documents (url, status) VALUES ($1, 'processing') ON CONFLICT (url) DO NOTHING RETURNING id",
                url
            );
            
            if (R.empty()) {
                std::cout << "Skipping duplicate: " << url << std::endl;
                W.commit();
                continue; // Skip to next URL
            }
            
            doc_id = R[0][0].as<int>();
            W.commit();
        } catch (const std::exception &e) {
            std::cerr << "DB Error: " << e.what() << std::endl;
            continue;
        }

        // C. Download HTML
        std::string html = download_url(url);
        if (html.empty()) {
            std::cerr << "Failed to download." << std::endl;
            // Mark as error in DB (Optional exercise for later)
            continue;
        }

        // D. Save to Disk
        std::string filename = "doc_" + std::to_string(doc_id) + ".html";
        std::string filepath = "/shared_data/" + filename;
        
        std::ofstream outFile(filepath);
        outFile << html;
        outFile.close();

        // E. Update DB with file path
        try {
            pqxx::work W(*C);
            W.exec_params(
                "UPDATE documents SET status = 'crawled', file_path = $1 WHERE id = $2",
                filename, doc_id
            );
            W.commit();
            std::cout << "Saved " << filename << " (" << html.length() << " bytes)" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "DB Update Error: " << e.what() << std::endl;
        }
        
        // Be polite!
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    redisFree(redis);
    delete C;
    return 0;
}