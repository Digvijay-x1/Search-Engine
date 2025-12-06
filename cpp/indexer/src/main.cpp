#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>
#include <thread>
#include <chrono>
#include <pqxx/pqxx>
#include <hiredis/hiredis.h>
#include <rocksdb/db.h>
#include <gumbo.h>
#include <zlib.h>
#include <cstdlib>
#include <cctype>
#include <stdexcept>
#include <climits>

// --- Config ---
std::string get_env_or_default(const char* var, const std::string& def) {
    const char* env = std::getenv(var);
    return env ? std::string(env) : def;
}

const std::string REDIS_HOST = get_env_or_default("REDIS_HOST", "redis_service");

std::string build_db_conn_str() {
    const char* env_conn = std::getenv("DB_CONN_STR");
    if (env_conn) {
        return std::string(env_conn);
    } else {
        std::string db_name = get_env_or_default("DB_NAME", "search_engine");
        std::string db_user = get_env_or_default("DB_USER", "admin");
        std::string db_pass = get_env_or_default("DB_PASS", "password123");
        std::string db_host = get_env_or_default("DB_HOST", "postgres_service");
        std::string db_port = get_env_or_default("DB_PORT", "5432");
        return "dbname=" + db_name + " user=" + db_user + " password=" + db_pass + " host=" + db_host + " port=" + db_port;
    }
}

const std::string DB_CONN_STR = build_db_conn_str();
const std::string ROCKSDB_PATH = get_env_or_default("ROCKSDB_PATH", "/shared_data/search_index.db");
const std::string WARC_BASE_PATH = get_env_or_default("WARC_BASE_PATH", "/shared_data/");

// --- Helper: Gumbo Text Extraction ---
std::string clean_text(GumboNode* node) {
    if (node->type == GUMBO_NODE_TEXT) {
        return std::string(node->v.text.text);
    } else if (node->type == GUMBO_NODE_ELEMENT &&
               node->v.element.tag != GUMBO_TAG_SCRIPT &&
               node->v.element.tag != GUMBO_TAG_STYLE) {
        std::string contents = "";
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            const std::string text = clean_text(static_cast<GumboNode*>(children->data[i]));
            if (i != 0 && !text.empty()) {
                contents.append(" ");
            }
            contents.append(text);
        }
        return contents;
    }
    return "";
}

// --- Helper: Decompress GZIP ---
std::string decompress_gzip(const std::string& compressed_data) {
    if (compressed_data.size() > UINT_MAX) {
        throw std::runtime_error("Compressed data too large (> 4GB)");
    }

    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = (uInt)compressed_data.size();
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed_data.data()));

    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }

    int ret;
    char buffer[32768];
    std::string outstring;

    do {
        zs.avail_out = sizeof(buffer);
        zs.next_out = (Bytef*)buffer;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("inflate failed with code: " + std::to_string(ret));
        }
        if (outstring.size() < zs.total_out) {
            outstring.append(buffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);
    return outstring;
}

// --- Helper: Tokenizer ---
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string token;
    for (char c : text) {
        if (isalnum(static_cast<unsigned char>(c))) {
            token += tolower(static_cast<unsigned char>(c));
        } else if (!token.empty()) {
            if (token.length() > 2) tokens.push_back(token); // Min word length 3
            token = "";
        }
    }
    if (!token.empty() && token.length() > 2) tokens.push_back(token);
    return tokens;
}

int main() {
    std::cout << "--- Indexer Service Started ---" << std::endl;

    // 1. Connect to Redis
    redisContext *redis = redisConnect(REDIS_HOST.c_str(), 6379);
    if (redis == NULL || redis->err) {
        std::cerr << "Redis connection failed" << std::endl;
        return 1;
    }

    // 2. Connect to Postgres
    pqxx::connection* C = nullptr;
    int retries = 10;
    while (retries > 0) {
        try {
            C = new pqxx::connection(DB_CONN_STR);
            if (C->is_open()) {
                std::cout << "Connected to DB" << std::endl;
                break;
            }
        } catch (const std::exception &e) {
            std::cerr << "Postgres connection attempt failed" << std::endl;
            if (C) { delete C; C = nullptr; }
        }
        std::cout << "Retrying Postgres connection in 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        retries--;
    }

    if (!C || !C->is_open()) {
        std::cerr << "Failed to connect to Postgres after retries." << std::endl;
        redisFree(redis);
        return 1;
    }

    // 3. Open RocksDB
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status status = rocksdb::DB::Open(options, ROCKSDB_PATH, &db);
    if (!status.ok()) {
        std::cerr << "RocksDB Open failed: " << status.ToString() << std::endl;
        delete C;
        redisFree(redis);
        return 1;
    }

    while (true) {
        // A. Pop from Queue
        redisReply *reply = (redisReply*)redisCommand(redis, "BLPOP indexing_queue 0");
        if (reply == NULL || reply->type != REDIS_REPLY_ARRAY) {
            if (reply) freeReplyObject(reply);
            continue; // Should not happen with BLPOP unless timeout/error
        }

        if (reply->elements < 2 || reply->element[1] == nullptr || reply->element[1]->str == nullptr) {
            freeReplyObject(reply);
            continue;
        }

        std::string doc_id_str = reply->element[1]->str;
        int doc_id;
        try {
            doc_id = std::stoi(doc_id_str);
        } catch (const std::exception&) {
            freeReplyObject(reply);
            continue;
        }
        freeReplyObject(reply);
        
        std::cout << "Indexing Doc ID: " << doc_id << std::endl;

        try {
            // B. Get Metadata
            pqxx::work W(*C);
            pqxx::row row = W.exec_params1("SELECT file_path, \"offset\", length FROM documents WHERE id = $1", doc_id);
            std::string file_path = WARC_BASE_PATH + row[0].as<std::string>();
            long offset = row[1].as<long>();
            long length = row[2].as<long>();
            W.commit();

            // C. Read WARC Record
            std::ifstream infile(file_path, std::ios::binary);
            if (!infile) {
                std::cerr << "Could not open file: " << file_path << std::endl;
                continue;
            }
            infile.seekg(offset);
            std::vector<char> buffer(length);
            infile.read(buffer.data(), length);
            std::streamsize readBytes = infile.gcount();
            if (readBytes != length) {
                std::cerr << "Failed to read full record: expected " << length << " bytes, got " << readBytes << std::endl;
                continue;
            }
            std::string compressed_data(buffer.begin(), buffer.end());
            
            // D. Decompress & Parse
            std::string full_warc_record = decompress_gzip(compressed_data);
            // Skip WARC headers (find first double newline)
            size_t header_end = full_warc_record.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            
            std::string html_content = full_warc_record.substr(header_end + 4);
            
            GumboOutput* output = gumbo_parse(html_content.c_str());
            std::string plain_text = clean_text(output->root);
            gumbo_destroy_output(&kGumboDefaultOptions, output);

            // E. Tokenize & Index
            std::vector<std::string> tokens = tokenize(plain_text);
            std::set<std::string> unique_tokens(tokens.begin(), tokens.end()); // Simple boolean index for now

            for (const auto& token : unique_tokens) {
                std::string current_list;
                status = db->Get(rocksdb::ReadOptions(), token, &current_list);
                
                std::set<std::string> doc_ids;
                if (status.ok() && !current_list.empty()) {
                    std::stringstream ss(current_list);
                    std::string id;
                    while (std::getline(ss, id, ',')) {
                        doc_ids.insert(id);
                    }
                }
                
                std::string doc_id_str = std::to_string(doc_id);
                if (doc_ids.find(doc_id_str) == doc_ids.end()) {
                    doc_ids.insert(doc_id_str);
                    current_list = "";
                    for (auto it = doc_ids.begin(); it != doc_ids.end(); ++it) {
                        if (it != doc_ids.begin()) current_list += ",";
                        current_list += *it;
                    }
                    db->Put(rocksdb::WriteOptions(), token, current_list);
                }
            }

            // F. Update Doc Length
            pqxx::work W2(*C);
            W2.exec_params("UPDATE documents SET doc_length = $1 WHERE id = $2", tokens.size(), doc_id);
            W2.commit();
            
            std::cout << "Indexed " << tokens.size() << " words for Doc " << doc_id << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "Error indexing doc " << doc_id << ": " << e.what() << std::endl;
        }
    }

    delete db;
    delete C;
    redisFree(redis);
    return 0;
}
