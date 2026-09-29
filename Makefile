# Makefile for ER Patient Management System
# ------------------------------------------
# Targets:
#   all       - build the main application (er_patient_mgmt / er_patient_mgmt.exe)
#   run       - build then run the application
#   test      - build and run the automated test suite
#   clean     - remove all compiled binaries
#   valgrind  - run the test suite under valgrind (Linux/macOS only)
#
# Compiler flags (from problem spec):
#   -Wall -Wextra -Wpedantic -std=c11

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11

# Detect Windows (MinGW) vs Unix
ifeq ($(OS),Windows_NT)
    EXE_SUFFIX = .exe
    RM         = del /Q
else
    EXE_SUFFIX =
    RM         = rm -f
endif

# Common source files (shared between main and test)
COMMON_SRC = patient.c cqueue.c stack.c ermanager.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

# Main application
MAIN_SRC   = main.c
MAIN_BIN   = er_patient_mgmt$(EXE_SUFFIX)

# Test executable
TEST_SRC   = test.c
TEST_BIN   = run_tests$(EXE_SUFFIX)

# ---------------------------------------------------------------------------
.PHONY: all run test clean valgrind

all: $(MAIN_BIN)

$(MAIN_BIN): $(COMMON_OBJ) main.o
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build OK: $(MAIN_BIN)"

$(TEST_BIN): $(COMMON_OBJ) test.o
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build OK: $(TEST_BIN)"

# Pattern rule for all .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(MAIN_BIN)
	./$(MAIN_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

valgrind: $(TEST_BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_BIN)

clean:
	$(RM) *.o $(MAIN_BIN) $(TEST_BIN) 2>/dev/null || true
