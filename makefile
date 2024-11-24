CXX=g++
CXXFLAGS=-Wall -ggdb -Werror -std=c++17 -O2
LIBS=-pthread -lcurl
SOURCES=GetURL.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=geturl

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o: $(CXX) $(CXXFLAGS) $< -o $@

clean: rm -rf *.o $(EXECUTABLE)



