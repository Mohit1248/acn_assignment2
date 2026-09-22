#include "client_ops.h"

// TODO(C2, Stage 1, Mohit): implement over a plain TCP connect() + the
// protocol.h primitives (send_all/read_exact/read_header_line once S2/S3
// land, or raw send()/recv() here if you'd rather not wait on those).
// Remember: one connection carries exactly one request (framing rules) -
// connect fresh for every put_file/get_file call.

ExchangeResult put_file(const std::string& /*ip*/, uint16_t /*port*/, const std::string& /*local_path*/) {
    return ExchangeResult{false, 0, "not implemented"};
}

ExchangeResult get_file(const std::string& /*ip*/, uint16_t /*port*/, const std::string& /*name*/,
                         const std::string& /*out_path*/) {
    return ExchangeResult{false, 0, "not implemented"};
}
