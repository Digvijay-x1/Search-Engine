#include "../src/warc_writer.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <filesystem>
#include <vector>

// Simple assertion macro
#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion failed: " << (message) << "\n" \
                      << "File: " << __FILE__ << ", Line: " << __LINE__ << std::endl; \
            std::exit(EXIT_FAILURE); \
        } \
    } while (false)

void test_file_creation() {
    std::string filename = "test_warc_create.warc.gz";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    {
        crawler::WarcWriter writer(filename);
    } // Destructor closes file

    ASSERT(std::filesystem::exists(filename), "WARC file should be created");
    std::filesystem::remove(filename);
    std::cout << "test_file_creation passed" << std::endl;
}

void test_write_record() {
    std::string filename = "test_warc_write.warc.gz";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::string url = "http://example.com";
    std::string content = "<html><body>Hello World</body></html>";

    {
        crawler::WarcWriter writer(filename);
        auto info = writer.write_record(url, content);
        ASSERT(info.length > 0, "Record length should be positive");
        ASSERT(info.offset == 0, "First record offset should be 0");
    }

    // Verify file size is non-zero
    ASSERT(std::filesystem::file_size(filename) > 0, "File should not be empty");

    std::filesystem::remove(filename);
    std::cout << "test_write_record passed" << std::endl;
}

int main() {
    try {
        test_file_creation();
        test_write_record();
        std::cout << "All tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
