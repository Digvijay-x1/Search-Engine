#ifndef WARC_WRITER_HPP
#define WARC_WRITER_HPP

#include <string>
#include <fstream>
#include <vector>

struct WarcRecordInfo {
    long long offset;
    int length;
};

class WarcWriter {
public:
    explicit WarcWriter(const std::string& filename);
    ~WarcWriter();

    // Writes a compressed WARC record and returns its offset and length
    WarcRecordInfo write_record(const std::string& url, const std::string& content);

private:
    std::ofstream file_stream;
    std::string filename;

    std::string create_warc_header(const std::string& url, size_t content_length);
    std::string compress_string(const std::string& str);
};

#endif // WARC_WRITER_HPP
