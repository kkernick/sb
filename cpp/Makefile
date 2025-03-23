SOURCES = src/main.cpp src/shared.cpp src/libraries.cpp src/arguments.cpp src/binaries.cpp src/syscalls.cpp src/generators.cpp

VERSION := $(shell git rev-parse --short HEAD)
REQUIRED = -std=c++23 -lseccomp -lb2 -fuse-ld=lld -DVERSION="\"$(VERSION)\""
FAST = -march=native -O3 -ffast-math -flto=thin

cpp: $(SOURCES) src/third_party/libexec/src/exec.cpp
	clang++ $(REQUIRED) $(FAST) $^ -o sb

link: $(SOURCES)
	clang++ $(REQUIRED) -lexec $(FAST) $^ -o sb

generic: $(SOURCES)
	clang++ $(REQUIRED) $^ -o sb

debug: $(SOURCES)
	clang++ -march=x86-64 -g -DPROFILE $(REQUIRED) $^ -o sb

pgo: $(SOURCES)
	clang++ -fprofile-instr-generate $(REQUIRED) $(FAST) $^ -o sb

	-./pgo-helper

	llvm-profdata merge *.profraw -output=sb.profdata
	clang++ -fprofile-instr-use=sb.profdata $(REQUIRED) $(FAST) $^ -o sb

	rm *.profraw *.profdata

bolt: $(SOURCES)
	clang++ $(REQUIRED) $(FAST) -Wl,--emit-relocs $^ -o sb
	-./bolt-helper

	merge-fdata *.fdata > combined.fdata
	llvm-bolt sb -o sb.bolt -data=combined.fdata -reorder-blocks=ext-tsp -reorder-functions=hfsort -split-functions -split-all-cold -split-eh -dyno-stats
	mv sb.bolt sb

	rm *.fdata *.data *.data.old

bolt-pgo: $(SOURCES)
	clang++ -fprofile-instr-generate $(REQUIRED) $(FAST) $^ -o sb
	-./pgo-helper
	llvm-profdata merge *.profraw -output=sb.profdata
	clang++ -fprofile-instr-use=sb.profdata $(REQUIRED) $(FAST) -Wl,--emit-relocs $^ -o sb

	-./bolt-helper

	merge-fdata *.fdata > combined.fdata
	llvm-bolt sb -o sb.bolt -data=combined.fdata -reorder-blocks=ext-tsp -reorder-functions=hfsort -split-functions -split-all-cold -split-eh -dyno-stats
	mv sb.bolt sb

	rm *.profraw *.profdata *.fdata *.data *.data.old

doxygen:
	doxygen config
