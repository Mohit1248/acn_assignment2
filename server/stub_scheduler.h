#ifndef SERVER_STUB_SCHEDULER_H
#define SERVER_STUB_SCHEDULER_H

#include <string>

#include "scheduler.h"

// Throwaway scaffolding for Stage 1, before the real scheduler core
// (K1/K2/K3, Stage 2) lands. FIFO order, no preemption. Lets the server run
// end-to-end (accept -> parse -> transfer -> respond) against a real client
// while Stage 1 is in progress. Delete once Stage 2 replaces it (A5/A6 are
// NOT satisfied by this path - do not run experiments against it, per the
// team plan).
IScheduler* make_stub_scheduler();

// Transfers `req` in full over `fd` in one shot: GET reads the whole file
// at `full_path` and sends "OK <size>\n" + all bytes; PUT sends "OK 0\n",
// reads exactly req->bytes (draining req->leftover first), writes them to
// `full_path`, then sends the final "OK 0\n". No whole-line rule, no --p
// batching (A6/A8 are stub-only exempt here - see header comment above).
// Returns false on any I/O error; caller is responsible for sending an
// ERR/closing per A24 in that case.
bool stub_serve_whole(Request* req, int fd, const std::string& full_path);

#endif  // SERVER_STUB_SCHEDULER_H
