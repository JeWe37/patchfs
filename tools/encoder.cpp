#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "fcntl.h"
#include "sys/stat.h"
#include "sys/mman.h"
#include "sys/xattr.h"

#include "google/vcencoder.h"

#define BUFSIZE 4 * 1024 * 1024

class FileOutput : public open_vcdiff::OutputStringInterface {
    FILE* file_;
public:
    FileOutput(const std::string& path) {
        file_ = fopen(path.c_str(), "wb");
        if (file_ == NULL)
            throw std::runtime_error("Failed to open output file");
    }

    ~FileOutput() {
        fclose(file_);
    }

    OutputStringInterface& append(const char* s, size_t n) {
        if (fwrite(s, n, 1, file_) < 0)
            throw std::runtime_error("Failed to write to output file");
        return *this;
    }

    void clear() { }

    void push_back(char c) { 
        if (fputc(c, file_) < 0)
            throw std::runtime_error("Failed to write to output file");
    }
    
    void ReserveAdditionalBytes(size_t res_arg) { }

    size_t size() const { 
        return ftell(file_);
    }

    void SetXAttr(const std::string& diffSourcePath, size_t diffSourceSize) {
        int fd = fileno(file_);
        if (fsetxattr(fd, "user.diff_src", diffSourcePath.c_str(), diffSourcePath.size(), 0) < 0)
            throw std::runtime_error(std::string("Failed to set xattr user.diff_src: ") + strerror(errno));
        std::string str_size = std::to_string(diffSourceSize);
        if (fsetxattr(fd, "user.diff_src_size", str_size.c_str(), str_size.size(), 0) < 0)
            throw std::runtime_error(std::string("Failed to set xattr user.diff_src_size: ") + strerror(errno));
    }
};

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " [OLD] [DIFF] [OLD_PATH] [NEW]" << std::endl;
        return 1;
    }

    // mmap the input file
    int input_fd = open(argv[1], O_RDONLY);
    if (input_fd < 0) {
        std::cerr << "Failed to open input file" << std::endl;
        return 1;
    }

    struct stat input_stat;
    if (fstat(input_fd, &input_stat) < 0) {
        std::cerr << "Failed to stat input file" << std::endl;
        return 1;
    }

    char* input_data = (char*) mmap(NULL, input_stat.st_size, PROT_READ, MAP_PRIVATE, input_fd, 0);
    if (input_data == MAP_FAILED) {
        std::cerr << "Failed to mmap input file" << std::endl;
        return 1;
    }

    FILE* new_file = fopen(argv[4], "rb");

    open_vcdiff::HashedDictionary dictionary(input_data, input_stat.st_size);
    dictionary.Init();

    open_vcdiff::VCDiffStreamingEncoder encoder(&dictionary, open_vcdiff::VCD_FORMAT_INTERLEAVED, false);

    FileOutput delta(argv[2]);

    if (!encoder.StartEncodingToInterface(&delta))
        return 1;
    char* buf = new char[BUFSIZE];
    size_t len;
    while ((len = fread(buf, 1, BUFSIZE, new_file)) > 0) {
        if (!encoder.EncodeChunkToInterface(buf, len, &delta)) {
            delete [] buf;
            return 1;
        }
    }
    if (!encoder.FinishEncodingToInterface(&delta)) {
        delete [] buf;
        return 1;
    }

    delta.SetXAttr(argv[3], ftell(new_file));

    delete [] buf;
    return 0;
}
