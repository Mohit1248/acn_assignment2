CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS := -pthread

COMMON_SRC_SERVER := common/protocol.cpp common/config.cpp common/csv_writer.cpp
COMMON_SRC_CLIENT := common/protocol.cpp common/config.cpp common/client_ops.cpp

SERVER_SRC := server/main.cpp \
              server/scheduler_factory.cpp \
              server/scheduler_fcfs.cpp \
              server/scheduler_sjf.cpp \
              server/scheduler_rr.cpp \
              server/scheduler_drr.cpp \
              server/slice.cpp \
              server/stub_scheduler.cpp \
              $(COMMON_SRC_SERVER)

CLIENT_SRC := client/main.cpp \
              client/load.cpp \
              $(COMMON_SRC_CLIENT)

.PHONY: all clean
all: bin/server bin/client

bin/server: $(SERVER_SRC) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

bin/client: $(CLIENT_SRC) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

bin:
	mkdir -p bin

clean:
	rm -rf bin
