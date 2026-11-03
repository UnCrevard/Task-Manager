# Makefile pour Windows Task Manager en C (MinGW-w64)
# Compatible avec gcc / mingw32-make sous Windows

CC ?= gcc
WINDRES ?= windres
CFLAGS = -mwindows -O2 -s -static -Wall -Wextra
LIBS = -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32 -luxtheme -ldwmapi

TARGET = taskmgr.exe
SRC = taskmgr.c
RC = resource.rc
RES = resource.o

all: $(TARGET)

$(RES): $(RC) app.ico taskmgr.manifest resource.h
	$(WINDRES) $(RC) -O coff -o $(RES)

$(TARGET): $(SRC) $(RES) resource.h
	-taskkill /IM $(TARGET) /F
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(RES) $(LIBS)
	@echo Compilation terminee avec succes : $(TARGET)

clean:
	-taskkill /IM $(TARGET) /F
	rm -f $(TARGET) $(RES)

.PHONY: all clean
