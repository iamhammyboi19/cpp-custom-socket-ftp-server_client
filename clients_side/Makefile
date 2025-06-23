CXX = g++

CXXFLAGS = -Wall -Wextra -std=c++17

TARGET = client_side

SRCS = $(wildcard *.cpp)

OBJS = $(SRCS:.cpp=.o)

all = $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)