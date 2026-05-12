CXX := clang++-20
ROOT := ../..
BUILDDIR := $(ROOT)/build/edinburgh
LIBDIR := $(ROOT)/build/lib
BINDIR := $(ROOT)/build/bin
PLUGIN := $(LIBDIR)/libedinburgh_sched.so
LLVMINC := $(shell llvm-config-20 --includedir)
LLVMLDFLAGS := $(shell llvm-config-20 --ldflags --system-libs --libs codegen support)
CXXFLAGS := -std=c++23 -O2 -g -fPIC -DNDEBUG -Wall -Wextra -pedantic -stdlib=libstdc++ --gcc-toolchain=/usr

.PHONY: compile compare test clean

compile: $(PLUGIN)

$(PLUGIN): src/edinburgh_sched.cpp
	mkdir -p $(LIBDIR)
	$(CXX) $(CXXFLAGS) -shared $< -isystem $(LLVMINC) $(LLVMLDFLAGS) -o $@

test: $(PLUGIN)
	mkdir -p $(BUILDDIR)
	llc-20 -mtriple=x86_64-pc-linux-gnu -O2 -enable-misched -stop-after=machine-scheduler examples/tiny.ll -o $(BUILDDIR)/tiny.default.mir
	llc-20 -mtriple=x86_64-pc-linux-gnu -O2 -load $(PLUGIN) -enable-misched -misched=edinburgh -stop-after=machine-scheduler examples/tiny.ll -o $(BUILDDIR)/tiny.edinburgh.mir

compare: $(PLUGIN)
	scripts/compare-mir.sh $(INPUT)

clean:
	rm -f $(PLUGIN)
	rm -rf $(BUILDDIR)
