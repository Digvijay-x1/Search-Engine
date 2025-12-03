#include <iostream>
#include <pqxx/pqxx> // Postgres C++ library
#include <hiredis/hiredis.h> // Redis C library
#include <unistd.h> // For sleep()

int main() {
    std::cout << "--- Starting Connectivity Check ---" << std::endl;

    // 1. Test Redis Connection
    std::cout << "[Redis] Connecting..." << std::endl;
    // Note: "redis_service" is the hostname from docker-compose
    redisContext *c = redisConnect("redis_service", 6379);
    
    if (c == NULL || c->err) {
        if (c) {
            std::cerr << "[Redis] Error: " << c->errstr << std::endl;
            redisFree(c);
        } else {
            std::cerr << "[Redis] Error: Can't allocate redis context" << std::endl;
        }
    } else {
        std::cout << "[Redis] Connected!" << std::endl;
        redisReply *reply = (redisReply*)redisCommand(c, "PING");
        std::cout << "[Redis] PING Response: " << reply->str << std::endl;
        freeReplyObject(reply);
        redisFree(c);
    }

    // 2. Test Postgres Connection
    std::cout << "[Postgres] Connecting..." << std::endl;
    int retries = 5;
    while (retries > 0) {
        try {
            // Note: "postgres_service" is the hostname
            pqxx::connection C("dbname=search_engine user=admin password=password123 host=postgres_service port=5432");
            
            if (C.is_open()) {
                std::cout << "[Postgres] Connected to database: " << C.dbname() << std::endl;
                
                // Create a transactional worker
                pqxx::work W(C);
                
                // Execute a test query
                // Check if table exists first to avoid error if init.sql hasn't run yet
                try {
                    pqxx::result R = W.exec("SELECT count(*) FROM documents");
                    std::cout << "[Postgres] Documents table row count: " << R[0][0].c_str() << std::endl;
                } catch (const std::exception &e) {
                     std::cout << "[Postgres] Table 'documents' might not exist yet (init.sql running?): " << e.what() << std::endl;
                }
                W.commit();
                break; // Connected successfully
            } else {
                std::cout << "[Postgres] Failed to open connection" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "[Postgres] Exception: " << e.what() << std::endl;
            std::cerr << "[Postgres] Retrying in 5 seconds..." << std::endl;
            sleep(5);
            retries--;
        }
    }

    return 0;
}