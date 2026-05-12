CXX			:= clang++-20
CXXFLAGS	:= -std=c++23 -O2 -g -DNDEBUG -I include -I ../athens/include -Wall -Wextra -pedantic -stdlib=libstdc++ --gcc-toolchain=/usr -rdynamic
SRC			:= src/berlin.cpp src/mlir_gen.cpp
ATHENS_SRC	:= ../athens/src/codegen.cpp ../athens/src/athens_lex_rules.cpp ../athens/src/error.cpp
SHARED_LEX_SRC := ../../shared/frontend/lex/src/char_stream.cpp ../../shared/frontend/lex/src/lexer.cpp

LLVMINC		:= $(shell llvm-config-20 --includedir)
LLVMLIBS	:= $(shell llvm-config-20 --ldflags --system-libs --libs core orcjit native)
MLIRLIBS	:= -lMLIR
LDFLAGS		:= -Wl,-rpath,/usr/lib/llvm-20/lib

TARGET		:= berlin

.PHONY: compile run test clean

compile: $(TARGET)

$(TARGET): $(SRC) $(ATHENS_SRC) $(SHARED_LEX_SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(ATHENS_SRC) $(SHARED_LEX_SRC) \
		-isystem $(LLVMINC) \
		$(LDFLAGS) $(LLVMLIBS) $(MLIRLIBS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) examples/simple_loop.ath

test: $(TARGET)
	@echo "=== simple_loop.ath ==="
	./$(TARGET) examples/simple_loop.ath > /tmp/berlin_test.mlir 2>&1
	@grep -q "scf.for" /tmp/berlin_test.mlir && echo "  PASS: contains loop"
	@echo "=== nested_loop.ath ==="
	./$(TARGET) examples/nested_loop.ath > /tmp/berlin_test.mlir 2>&1
	@grep -q "scf.for" /tmp/berlin_test.mlir && echo "  PASS: contains loop"
	@echo "=== saxpy.ath ==="
	./$(TARGET) examples/saxpy.ath > /tmp/berlin_test.mlir 2>&1
	@grep -q "arith.mulf" /tmp/berlin_test.mlir && echo "  PASS: contains multiplication"
	@echo "All tests passed."

clean:
	rm -f $(TARGET) *.o *.out
