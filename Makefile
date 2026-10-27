CXX=g++
CXXFLAGS= -std=c++17 -O2 -Wall

all: tea

tea: tea.cpp tea.h
	$(CXX) $(CXXFLAGS) tea.cpp -otea

clean:
	rm -f tea

.PHONY: clean 