CXX			:= clang++-20
CXXFLAGS	:= -std=c++23 -O2 -g -DNDEBUG -Iinclude -Wall -Wextra -pedantic -stdlib=libstdc++ --gcc-toolchain=/usr -rdynamic
# *.cpp src/*.cpp
SRC			:= src/*.cpp *.cpp
SHARED_LEX_SRC := ../../shared/frontend/lex/src/char_stream.cpp ../../shared/frontend/lex/src/lexer.cpp
LEX_SMOKE_SRC := tools/lexer_smoke.cpp src/athens_lex_rules.cpp $(SHARED_LEX_SRC)
PARSER_SMOKE_SRC := tools/parser_smoke.cpp src/athens_lex_rules.cpp src/codegen.cpp src/error.cpp src/lexer.cpp src/parser.cpp $(SHARED_LEX_SRC)
LLVMINC   := $(shell llvm-config-20 --includedir)
LLVMLIBS  := $(shell llvm-config-20 --ldflags --system-libs --libs core orcjit native)
TARGET		:= athens
LEX_SMOKE := lexer_smoke
PARSER_SMOKE := parser_smoke

.PHONY: compile \
		lexer-smoke \
		parser-smoke \
		run \
		clean

compile: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -I$(LLVMINC) $(LLVMLIBS) -o $(TARGET)

$(LEX_SMOKE): $(LEX_SMOKE_SRC)
	$(CXX) $(CXXFLAGS) $(LEX_SMOKE_SRC) -o $(LEX_SMOKE)

$(PARSER_SMOKE): $(PARSER_SMOKE_SRC)
	$(CXX) $(CXXFLAGS) $(PARSER_SMOKE_SRC) -I$(LLVMINC) $(LLVMLIBS) -o $(PARSER_SMOKE)

lexer-smoke: $(LEX_SMOKE)
	./$(LEX_SMOKE) --self-test

parser-smoke: $(PARSER_SMOKE)
	./$(PARSER_SMOKE) --self-test

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(LEX_SMOKE) $(PARSER_SMOKE) *.o *.out
