#include "warc_writer.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <random>
#include <zlib.h>

namespace crawler {

WarcWriter::WarcWriter(const std::string& filename) : filename(filename) {
    file_stream.open(filename, std::ios::binary | std::ios::app);
    if (!file_stream.is_open()) {
        throw std::runtime_error("Failed to open WARC file: " + filename);
    }
}

WarcWriter::~WarcWriter() {
    if (file_stream.is_open()) {
        file_stream.close();
    }
}

WarcRecordInfo WarcWriter::write_record(const std::string& url, const std::string& content) {
    std::lock_guard<std::mutex> lock(write_mutex);
    
    std::string warc_header = create_warc_header(url, content.size());
    std::string full_record = warc_header + content + "\r\n\r\n";
    std::string compressed_record = compress_string(full_record);

    file_stream.seekp(0, std::ios::end);
    int64_t offset = file_stream.tellp();
    
    file_stream.write(compressed_record.data(), compressed_record.size());
    
    if (!file_stream.good()) {
        throw std::runtime_error("Failed to write WARC record to file: write error");
    }
    
    // Ensure data is written to disk
    file_stream.flush();
    
    if (!file_stream.good()) {
        throw std::runtime_error("Failed to write WARC record to file: flush error");
    }

    return {offset, static_cast<int64_t>(compressed_record.size())};
}

std::string WarcWriter::create_warc_header(const std::string& url, size_t content_length) {
    std::time_t now = std::time(nullptr);
    char buf[100];
    struct tm tm_buf;
    // Platform-agnostic thread-safe time formatting
    #ifdef _WIN32
        gmtime_s(&tm_buf, &now);
    #else
        gmtime_r(&now, &tm_buf);
    #endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    
    std::stringstream ss;
    ss << "WARC/1.0\r\n";
    ss << "WARC-Type: response\r\n";
    ss << "WARC-Target-URI: " << url << "\r\n";
    ss << "WARC-Date: " << buf << "\r\n";
    ss << "WARC-Record-ID: <urn:uuid:" << generate_uuid() << ">\r\n";
    ss << "Content-Type: application/http; msgtype=response\r\n";
    ss << "Content-Length: " << content_length << "\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string WarcWriter::generate_uuid() {
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    thread_local std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    // Variant 1 (RFC 4122)
    // Version 4 (random)
    part1 = (part1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::stringstream ss;
    ss << std::hex << std::setfill('0') 
       << std::setw(8) << (part1 >> 32) << "-"
       << std::setw(4) << ((part1 >> 16) & 0xFFFF) << "-"
       << std::setw(4) << (part1 & 0xFFFF) << "-"
       << std::setw(4) << (part2 >> 48) << "-"
       << std::setw(12) << (part2 & 0xFFFFFFFFFFFFULL);
    
    return ss.str();
}

std::string WarcWriter::compress_string(const std::string& str) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("deflateInit2 failed while compressing.");
    }

    // RAII-style cleanup for z_stream to ensure deflateEnd is called
    struct ZStreamGuard {
        z_stream* zs_ptr;
        ~ZStreamGuard() { deflateEnd(zs_ptr); }
    } guard{&zs};

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(str.data()));
    zs.avail_in = str.size();

    int ret;
    char outbuffer[32768];
    std::string outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    if (ret != Z_STREAM_END) {
        std::string msg = zs.msg ? zs.msg : "unknown error";
        throw std::runtime_error("Exception during zlib compression: (" + std::to_string(ret) + ") " + msg);
    }

    return outstring;
}

} // namespace crawler
