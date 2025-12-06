#ifndef WARC_WRITER_HPP
#define WARC_WRITER_HPP

#include <string>
#include <fstream>
#include <vector>
#include <cstdint>
#include <mutex>

namespace crawler {

struct WarcRecordInfo {
    int64_t offset;  // Byte offset where the compressed record starts in the WARC file
    int64_t length;  // Length of the compressed record in bytes
};

/**
 * @brief Writes web crawl data to a WARC (Web ARChive) format file with gzip compression.
 *
 * This class is responsible for creating and writing WARC records to a file, with each record compressed using gzip.
 * 
 * @note This class is thread-safe. Multiple threads can safely call write_record() concurrently.
 */
class WarcWriter {
public:
    /**
     * @brief Constructs a WarcWriter to write to the specified file.
     * @param filename The path to the WARC file to write. If the file does not exist, it will be created.
     * @throws std::runtime_error if the file cannot be opened for writing.
     */
    explicit WarcWriter(const std::string& filename);
    
    /**
     * @brief Destructor. Closes the WARC file if open.
     */
    ~WarcWriter();

    /**
     * @brief Writes a compressed WARC record for the given URL and content.
     *
     * The method creates a WARC record for the specified URL and content, compresses it using gzip,
     * and writes it to the output file.
     *
     * @param url The URL associated with the WARC record. Must be a valid, absolute URL as a UTF-8 encoded string.
     * @param content The content to store in the WARC record. Should be a UTF-8 encoded string containing the HTTP response or payload.
     * @return WarcRecordInfo containing the offset (in bytes) and length (in bytes) of the written record in the file.
     * @throws std::runtime_error if writing to the file fails.
     *
     * @note This method is thread-safe. Multiple threads can call this method concurrently.
     */
    WarcRecordInfo write_record(const std::string& url, const std::string& content);

private:
    std::ofstream file_stream;
    std::string filename;
    std::mutex write_mutex;  // Protects file operations for thread-safety

    std::string create_warc_header(const std::string& url, size_t content_length);
    std::string compress_string(const std::string& str);
    std::string generate_uuid();
};

} // namespace crawler

#endif // WARC_WRITER_HPP
