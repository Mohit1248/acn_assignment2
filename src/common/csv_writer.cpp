#include "csv_writer.h"

#include <cstdlib>
#include <iostream>

CsvWriter::CsvWriter(const std::string& path) {
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_) {
        std::cerr << "error: cannot open metrics output file '" << path << "'\n";
        std::exit(1);
    }
    out_ << kHeader << "\n";
    out_.flush();
}

void CsvWriter::write_row(const Request& req) {
    std::lock_guard<std::mutex> lock(mu_);
    out_ << req.id << ','
         << op_to_string(req.op) << ','
         << req.filename << ','
         << req.bytes << ','
         << req.rounds << ','
         << req.forfeited_bytes << ','
         << req.arrival_ns << ','
         << req.start_ns << ','
         << req.finish_ns << '\n';
}

void CsvWriter::close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

CsvWriter::~CsvWriter() {
    close();
}
