SOURCES = src/main.cpp src/shared.cpp src/libraries.cpp src/arguments.cpp src/binaries.cpp src/syscalls.cpp src/generators.cpp
REQUIRED = -std=c++23 -lseccomp

cpp: $(SOURCES)
	clang++ $(REQUIRED) -march=native -O3 -ffast-math -flto $^ -o sb

generic: $(SOURCES)
	clang++ $(REQUIRED) $^ -o sb

debug: $(SOURCES)
	clang++ -march=x86-64 -g $(REQUIRED) $^ -o sb

profile: $(SOURCES)
	g++ -march=x86-64 -pg $(REQUIRED) $^ -o sb

pgo: $(SOURCES)
	clang++ -fprofile-instr-generate $(REQUIRED) -march=native -O3 -ffast-math -flto	$^ -o sb

	-./pgo-helper

	llvm-profdata merge *.profraw -output=sb.profdata
	clang++ -fprofile-instr-use=sb.profdata $(REQUIRED) -march=native -O3 -ffast-math -flto $^ -o sb

	rm *.profraw *.profdata
