CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS := -pthread

COMMON_SRC_SERVER := src/common/protocol.cpp src/common/config.cpp src/common/csv_writer.cpp
COMMON_SRC_CLIENT := src/common/protocol.cpp src/common/config.cpp src/common/client_ops.cpp

SERVER_SRC := src/server/main.cpp \
              src/server/scheduler_factory.cpp \
              src/server/scheduler_fcfs.cpp \
              src/server/scheduler_sjf.cpp \
              src/server/scheduler_rr.cpp \
              src/server/scheduler_drr.cpp \
              src/server/slice.cpp \
              src/server/stub_scheduler.cpp \
              $(COMMON_SRC_SERVER)

CLIENT_SRC := src/client/main.cpp \
              src/client/load.cpp \
              $(COMMON_SRC_CLIENT)

# Sources live under src/ so the binaries can be ./server and ./client at the
# repo root, exactly as the assignment's examples invoke them.
.PHONY: all clean
all: server client

server: $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

client: $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

clean:
	rm -f server client
