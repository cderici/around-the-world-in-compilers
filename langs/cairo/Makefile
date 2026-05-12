CXX			:= clang++-20
CXXFLAGS	:= -std=c++23 -O2 -g -DNDEBUG -I include -I ../athens/include -Wall -Wextra -pedantic -stdlib=libstdc++ --gcc-toolchain=/usr -rdynamic
SRC			:= src/cairo.cpp src/nvptx_codegen.cpp src/cuda_driver_launcher.cpp
ATHENS_SRC	:= ../athens/src/codegen.cpp ../athens/src/athens_lex_rules.cpp ../athens/src/error.cpp
SHARED_LEX_SRC := ../../shared/frontend/lex/src/char_stream.cpp ../../shared/frontend/lex/src/lexer.cpp

LLVMINC		:= $(shell llvm-config-20 --includedir)
LLVMLIBS	:= $(shell llvm-config-20 --ldflags --system-libs --libs core native)
LDFLAGS		:= -Wl,-rpath,/usr/lib/llvm-20/lib

ifeq ($(CUDA),1)
CXXFLAGS += -DHAVE_CUDA -I/usr/local/cuda/include
LDFLAGS += -L/usr/local/cuda/lib64 -lcuda
endif

TARGET		:= cairo

.PHONY: compile run test clean

compile: $(TARGET)

$(TARGET): $(SRC) $(ATHENS_SRC) $(SHARED_LEX_SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(ATHENS_SRC) $(SHARED_LEX_SRC) \
		-isystem $(LLVMINC) \
		$(LDFLAGS) $(LLVMLIBS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) examples/vec_add.ath

test: $(TARGET)
	@echo "=== vec_add.ath (LLVM IR) ==="
	./$(TARGET) --emit-llvm examples/vec_add.ath > /tmp/cairo_test.ll 2>&1; \
	if grep -q "nvptx64-nvidia-cuda" /tmp/cairo_test.ll && grep -q "kernel_vec_add" /tmp/cairo_test.ll; then \
		echo "  PASS: LLVM IR contains target triple and kernel"; \
	else \
		echo "  FAIL"; exit 1; \
	fi
	@echo "=== vec_add.ath (PTX) ==="
	./$(TARGET) examples/vec_add.ath > /tmp/cairo_test.ptx 2>&1; \
	if grep -q "kernel_vec_add" /tmp/cairo_test.ptx; then \
		echo "  PASS: PTX contains kernel entry"; \
	else \
		echo "  FAIL"; exit 1; \
	fi
	@echo "=== scale.ath (PTX) ==="
	./$(TARGET) examples/scale.ath > /tmp/cairo_test.ptx 2>&1; \
	if grep -q "kernel_scale" /tmp/cairo_test.ptx; then \
		echo "  PASS: PTX contains kernel entry"; \
	else \
		echo "  FAIL"; exit 1; \
	fi
	@echo "=== simple_kernel.ath (PTX) ==="
	./$(TARGET) examples/simple_kernel.ath > /tmp/cairo_test.ptx 2>&1; \
	if grep -q "kernel_simple" /tmp/cairo_test.ptx; then \
		echo "  PASS: PTX contains kernel entry"; \
	else \
		echo "  FAIL"; exit 1; \
	fi
	@echo "=== --run without CUDA ==="
	! ./$(TARGET) --run examples/simple_kernel.ath > /tmp/cairo_run.out 2>&1; \
	if grep -q "without CUDA support" /tmp/cairo_run.out; then \
		echo "  PASS: --run reports missing CUDA support"; \
	else \
		echo "  FAIL"; exit 1; \
	fi
	@echo "All Cairo tests passed."

clean:
	rm -f $(TARGET) *.o *.out
