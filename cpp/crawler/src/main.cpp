#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <pqxx/pqxx>
#include <hiredis/hiredis.h>
#include "warc_writer.hpp"

// --- Config ---
const std::string REDIS_HOST = "redis_service";
const std::string DB_CONN_STR = "dbname=search_engine user=admin password=password123 host=postgres_service port=5432";
const std::string SEED_URL = "https://en.wikipedia.org/wiki/Main_Page";
const std::string WARC_FILENAME = "/shared_data/crawled.warc.gz";
const long CURL_TIMEOUT_SECONDS = 10;
const int DB_MAX_RETRIES = 10;
const int DB_RETRY_DELAY_SECONDS = 5;
const int QUEUE_POLL_INTERVAL_SECONDS = 5;
const int CRAWL_DELAY_SECONDS = 1;
const size_t MIN_URL_LENGTH = 10;

// --- CURL Callback ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<const char*>(contents), size * nmemb);
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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MaxSearchEngineBot/1.0 (Open source search engine)");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if(res != CURLE_OK) {
            std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    return readBuffer;
}

// --- Helper: Validate URL ---
bool is_valid_url(const std::string& url) {
    if (url.length() < MIN_URL_LENGTH) return false;
    if (url.compare(0, 7, "http://") == 0) return true;
    if (url.compare(0, 8, "https://") == 0) return true;
    return false;
}

// --- Helper: Extract Filename ---
std::string get_filename_from_path(const std::string& path) {
    size_t found = path.find_last_of("/\\");
    if (found != std::string::npos) {
        return path.substr(found + 1);
    }
    return path;
}

int main() {
    std::cout << "--- Crawler Service Started (WARC Mode) ---" << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 1. Connect to Redis
    redisContext *redis = redisConnect(REDIS_HOST.c_str(), 6379);
    if (redis == NULL || redis->err) {
        std::cerr << "Redis connection failed: " << (redis ? redis->errstr : "Can't allocate context") << std::endl;
        if (redis) redisFree(redis);
        curl_global_cleanup();
        return 1;
    }
    std::cout << "Connected to Redis" << std::endl;

    // 2. Connect to Postgres
    pqxx::connection* C = nullptr;
    int retries = DB_MAX_RETRIES;
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
        std::cout << "Retrying Postgres connection in " << DB_RETRY_DELAY_SECONDS << " seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(DB_RETRY_DELAY_SECONDS));
        retries--;
    }

    if (!C || !C->is_open()) {
        std::cerr << "Failed to connect to Postgres after retries." << std::endl;
        redisFree(redis);
        curl_global_cleanup();
        return 1;
    }

    // 3. Seed the Queue
    redisReply *reply = (redisReply*)redisCommand(redis, "LLEN crawl_queue");
    if (reply) {
        if (reply->integer == 0) {
            std::cout << "Queue empty. Seeding: " << SEED_URL << std::endl;
            freeReplyObject(reply);
            reply = (redisReply*)redisCommand(redis, "RPUSH crawl_queue %s", SEED_URL.c_str());
            if (!reply || redis->err) {
                std::cerr << "Failed to seed queue: " << (redis->err ? redis->errstr : "Unknown error") << std::endl;
                std::cerr << "Failed to seed crawl queue with initial URL" << std::endl;
                if (reply) freeReplyObject(reply);
                redisFree(redis);
                delete C;
                curl_global_cleanup();
                return 1;
            }
        }
        if (reply) freeReplyObject(reply);
    } else {
        std::cerr << "Failed to check queue length." << std::endl;
        redisFree(redis);
        delete C;
        curl_global_cleanup();
        return 1;
    }

    // 4. Initialize WarcWriter
    crawler::WarcWriter warc_writer(WARC_FILENAME);
    std::string warc_db_filename = get_filename_from_path(WARC_FILENAME);

    // 5. The Infinite Crawl Loop
    while (true) {
        reply = (redisReply*)redisCommand(redis, "LPOP crawl_queue");
        
        if (reply == NULL || reply->type == REDIS_REPLY_NIL) {
            if (reply) freeReplyObject(reply);
            std::this_thread::sleep_for(std::chrono::seconds(QUEUE_POLL_INTERVAL_SECONDS));
            continue;
        }

        if (reply->type != REDIS_REPLY_STRING) {
            std::cerr << "Unexpected Redis reply type: " << reply->type << std::endl;
            freeReplyObject(reply);
            continue;
        }

        std::string url = reply->str;
        freeReplyObject(reply);
        
        if (!is_valid_url(url)) continue;
        
        std::cout << "Fetching: " << url << std::endl;

        // B. Insert into DB "Pending"
        int doc_id = -1;
        try {
            pqxx::work W(*C);
            
            pqxx::result R = W.exec_params(
                "INSERT INTO documents (url, status) VALUES ($1, 'processing') ON CONFLICT (url) DO NOTHING RETURNING id",
                url
            );
            
            if (R.empty()) {
                std::cout << "Skipping duplicate: " << url << std::endl;
                W.commit();
                continue;
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
            std::cerr << "Failed to download: " << url << std::endl;
            continue;
        }

        // D. Save to WARC
        try {
            crawler::WarcRecordInfo info = warc_writer.write_record(url, html);

            // E. Update DB
            pqxx::work W(*C);
            W.exec_params(
                "UPDATE documents SET status = 'crawled', file_path = $1, \"offset\" = $2, length = $3 WHERE id = $4",
                warc_db_filename, info.offset, info.length, doc_id
            );
            W.commit();
            std::cout << "Saved to WARC at offset " << info.offset << " (" << info.length << " bytes)" << std::endl;

            // F. Push to Indexing Queue with error handling and retries
            const int MAX_RETRIES = 3;
            bool push_success = false;
            for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
                reply = (redisReply*)redisCommand(redis, "RPUSH indexing_queue %d", doc_id);
                if (reply == NULL) {
                    std::cerr << "Redis RPUSH failed for doc_id " << doc_id << " (attempt " << (attempt + 1) << "): NULL reply";
                    if (redis->err) {
                        std::cerr << ", Redis error: " << redis->errstr;
                    }
                    std::cerr << std::endl;
                    // No reply to free
                    continue;
                }
                if (reply->type == REDIS_REPLY_ERROR) {
                    std::cerr << "Redis RPUSH error for doc_id " << doc_id << " (attempt " << (attempt + 1) << "): " << reply->str << std::endl;
                    freeReplyObject(reply);
                    continue;
                }
                // Success
                freeReplyObject(reply);
                push_success = true;
                break;
            }
            if (!push_success) {
                // Handle failure: update DB status to indicate not queued
                try {
                    pqxx::work W_fail(*C);
                    W_fail.exec_params("UPDATE documents SET status = 'crawled_not_queued' WHERE id = $1", doc_id);
                    W_fail.commit();
                    std::cerr << "Failed to queue doc_id " << doc_id << " for indexing after " << MAX_RETRIES << " attempts, marked as crawled_not_queued" << std::endl;
                } catch (const std::exception &e) {
                    std::cerr << "Failed to update DB status for failed queue: " << e.what() << std::endl;
                }
            }

        } catch (const std::exception &e) {
            std::cerr << "Error saving WARC/DB: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(CRAWL_DELAY_SECONDS));
    }

    return 0;
}