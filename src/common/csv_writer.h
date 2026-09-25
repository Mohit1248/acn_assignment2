#ifndef COMMON_CSV_WRITER_H
#define COMMON_CSV_WRITER_H

#include <fstream>
#include <mutex>
#include <string>

#include "request.h"

// Per-request metrics CSV (A23). Exact header, one row per completed
// request. Never called for HEALTH (A5: HEALTH never appears in the CSV).
// Thread-safe: workers call write_row() as their requests complete.
class CsvWriter {
public:
    // Opens `path` for writing and writes the header line immediately.
    // Exits non-zero if the file cannot be opened (a config/flag error, not
    // a per-request one).
    explicit CsvWriter(const std::string& path);

    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;

    // Appends one row for a completed request. Thread-safe.
    void write_row(const Request& req);

    // Flushes and closes. Safe to call multiple times; the destructor also
    // calls this.
    void close();

    ~CsvWriter();

    static constexpr const char* kHeader =
        "request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns";

private:
    std::ofstream out_;
    std::mutex mu_;
};

#endif  // COMMON_CSV_WRITER_H
