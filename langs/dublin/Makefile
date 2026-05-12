CXX      := clang++-21
CXXFLAGS := -std=c++23 -O2 -g -DNDEBUG -I include -Wall -Wextra -pedantic -stdlib=libstdc++ --gcc-toolchain=/usr
SRC      := src/dublin.cpp src/dublin_lex_rules.cpp
SHARED_LEX_SRC := ../../shared/frontend/lex/src/char_stream.cpp ../../shared/frontend/lex/src/lexer.cpp

TARGET   := dublin

.PHONY: compile run test clean

compile: $(TARGET)

$(TARGET): $(SRC) $(SHARED_LEX_SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(SHARED_LEX_SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) examples/verified.dub

test: $(TARGET)
	@echo "=== verified.dub ==="
	./$(TARGET) examples/verified.dub > /tmp/dublin_test.out
	@grep -q "verified: add_zero" /tmp/dublin_test.out && echo "  PASS: verified"
	@echo "=== fails.dub ==="
	! ./$(TARGET) examples/fails.dub > /tmp/dublin_test.out
	@grep -q "counterexample: signed_bad" /tmp/dublin_test.out && echo "  PASS: counterexample"
	@echo "=== unsigned.dub ==="
	./$(TARGET) examples/unsigned.dub > /tmp/dublin_test.out
	@grep -q "verified: unsigned_wrap" /tmp/dublin_test.out && echo "  PASS: unsigned comparison"
	@echo "=== bool_true.dub ==="
	./$(TARGET) examples/bool_true.dub > /tmp/dublin_test.out
	@grep -q "verified: bool_true" /tmp/dublin_test.out && echo "  PASS: bool true"
	@echo "=== unsupported.dub ==="
	! ./$(TARGET) examples/unsupported.dub > /tmp/dublin_test.out 2>&1
	@grep -q "unsupported" /tmp/dublin_test.out && echo "  PASS: unsupported rejected"
	@echo "=== emit smt2 ==="
	./$(TARGET) --emit-smt2 examples/verified.dub > /tmp/dublin_test.smt2
	@grep -q "check-sat" /tmp/dublin_test.smt2 && echo "  PASS: emitted SMT-LIB"
	@echo "All Dublin tests passed."

clean:
	rm -f $(TARGET) *.o *.out
