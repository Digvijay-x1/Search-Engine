#include "utils.hpp"

#include <cstdlib>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <climits>
#include <zlib.h>

namespace indexer {

std::string get_env_or_default(const char* var, const std::string& def) {
    const char* env = std::getenv(var);
    return env ? std::string(env) : def;
}

std::string build_db_conn_str() {
    const char* env_conn = std::getenv("DB_CONN_STR");
    if (env_conn) {
        return std::string(env_conn);
    } else {
        std::string db_name = get_env_or_default("DB_NAME", "search_engine");
        std::string db_user = get_env_or_default("DB_USER", "admin");
        
        const char* env_pass = std::getenv("DB_PASS");
        if (!env_pass) {
            throw std::runtime_error("DB_PASS environment variable is required");
        }
        std::string db_pass(env_pass);

        std::string db_host = get_env_or_default("DB_HOST", "postgres_service");
        std::string db_port = get_env_or_default("DB_PORT", "5432");
        return "dbname=" + db_name + " user=" + db_user + " password=" + db_pass + " host=" + db_host + " port=" + db_port;
    }
}

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

std::string decompress_gzip(const std::string& compressed_data) {
    if (compressed_data.size() > UINT_MAX) {
        throw std::runtime_error("Compressed data too large (> 4GB)");
    }
    
    const size_t MAX_DECOMPRESSED_SIZE = 100 * 1024 * 1024; // 100MB limit

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
        if (outstring.size() > MAX_DECOMPRESSED_SIZE) {
            inflateEnd(&zs);
            throw std::runtime_error("Decompressed data exceeds maximum allowed size");
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);
    return outstring;
}

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

} // namespace indexer
