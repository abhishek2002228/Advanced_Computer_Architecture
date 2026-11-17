CXX = c++
CXXFLAGS = -O2 -g -Wall -Wextra -std=c++11
SRCS = pipeline.cpp sim.cpp bpred.cpp
OBJS = $(SRCS:.cpp=.o)
.PHONY: all clean
all: sim
sim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
%.o: %.cpp $(wildcard *.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@
clean:
	rm -f sim $(OBJS)
