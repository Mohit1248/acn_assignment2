#ifndef COMMON_CSV_WRITER_H
#define COMMON_CSV_WRITER_H

#include <fstream>
#include <mutex>
#include <string>

#include "request.h"

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);

    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;

    void write_row(const Request& req);

    void close();

    ~CsvWriter();

    static constexpr const char* kHeader =
        "request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns";

private:
    std::ofstream out_;
    std::mutex mu_;
};

#endif
