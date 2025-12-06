#include "warc_writer.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

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
    std::string warc_header = create_warc_header(url, content.size());
    std::string full_record = warc_header + content + "\r\n\r\n";
    std::string compressed_record = compress_string(full_record);

    file_stream.seekp(0, std::ios::end);
    long long offset = file_stream.tellp();
    
    file_stream.write(compressed_record.data(), compressed_record.size());
    
    // Ensure data is written to disk
    file_stream.flush();

    return {offset, (int)compressed_record.size()};
}

std::string WarcWriter::create_warc_header(const std::string& url, size_t content_length) {
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    
    std::stringstream ss;
    ss << "WARC/1.0\r\n";
    ss << "WARC-Type: response\r\n";
    ss << "WARC-Target-URI: " << url << "\r\n";
    ss << "WARC-Date: " << buf << "\r\n";
    ss << "Content-Type: application/http; msgtype=response\r\n";
    ss << "Content-Length: " << content_length << "\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string WarcWriter::compress_string(const std::string& str) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("deflateInit2 failed while compressing.");
    }

    zs.next_in = (Bytef*)str.data();
    zs.avail_in = str.size();

    int ret;
    char outbuffer[32768];
    std::string outstring;

    do {
        zs.next_out = (Bytef*)outbuffer;
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("Exception during zlib compression: (" + std::to_string(ret) + ") " + zs.msg);
    }

    return outstring;
}
