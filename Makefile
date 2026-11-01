# Makefile pour Windows Task Manager en C (MinGW-w64)
# Compatible avec gcc / mingw32-make sous Windows ou compilation croisée Linux

#  -DUNICODE -D_UNICODE

CC ?= gcc
CFLAGS = -mwindows -O2 -Wall -Wextra
LIBS = -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32

TARGET = taskmgr.exe
SRC = taskmgr.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)
	@echo Compilation terminee avec succes : $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
