SOURCES = src/main.cpp src/shared.cpp src/libraries.cpp src/arguments.cpp src/binaries.cpp src/syscalls.cpp src/generators.cpp

VERSION := $(shell git rev-parse --short HEAD)
REQUIRED = -std=c++23 -lseccomp -lb2 -fuse-ld=lld -DVERSION="\"$(VERSION)\""
FAST = -march=native -O3 -ffast-math -flto=thin

LINK = -lexec
STATIC = src/third_party/libexec/src/exec.cpp

cpp: $(SOURCES)
	clang++ $(REQUIRED) $(LINK) $(FAST) $^ -o sb

static: $(SOURCES)
	clang++ $(REQUIRED) $(STATIC) $(FAST) $^ -o sb-static

generic: $(SOURCES)
	clang++ $(REQUIRED) $(LINK) $^ -o sb

generic-static: $(SOURCES)
	clang++ $(REQUIRED) $(STATIC) $^ -o sb-static

debug: $(SOURCES)
	clang++ -march=x86-64 -g -DPROFILE $(REQUIRED) $(LINK) $^ -o sb

pgo: $(SOURCES) src/third_party/libexec/src/exec.cpp
	clang++ -fprofile-instr-generate $(REQUIRED) $(LINK) $(FAST) $^ -o sb
	-./pgo-helper
	llvm-profdata merge *.profraw -output=sb.profdata
	clang++ -fprofile-instr-use=sb.profdata $(REQUIRED) $(LINK) $(FAST) $^ -o sb
	rm *.profraw *.profdata

bolt: $(SOURCES) src/third_party/libexec/src/exec.cpp
	clang++ $(REQUIRED) $(LINK) $(FAST) -Wl,--emit-relocs $^ -o sb
	-./bolt-helper
	merge-fdata *.fdata > combined.fdata
	llvm-bolt sb -o sb.bolt -data=combined.fdata -reorder-blocks=ext-tsp -reorder-functions=hfsort -split-functions -split-all-cold -split-eh -dyno-stats
	mv sb.bolt sb
	rm *.fdata *.data *.data.old

bolt-pgo: $(SOURCES) src/third_party/libexec/src/exec.cpp
	clang++ -fprofile-instr-generate $(REQUIRED) $(LINK) $(FAST) $^ -o sb
	-./pgo-helper
	llvm-profdata merge *.profraw -output=sb.profdata
	clang++ -fprofile-instr-use=sb.profdata $(REQUIRED) $(LINK) $(FAST) -Wl,--emit-relocs $^ -o sb
	-./bolt-helper
	merge-fdata *.fdata > combined.fdata
	llvm-bolt sb -o sb.bolt -data=combined.fdata -reorder-blocks=ext-tsp -reorder-functions=hfsort -split-functions -split-all-cold -split-eh -dyno-stats
	mv sb.bolt sb

	rm *.profraw *.profdata *.fdata *.data *.data.old

doxygen:
	doxygen config
