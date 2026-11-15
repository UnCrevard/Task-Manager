# Makefile pour Windows Task Manager en C (MinGW-w64)
# Compatible avec gcc / mingw32-make sous Windows

CC ?= gcc
WINDRES ?= windres
SRC_DIR = src
CFLAGS = -mwindows -O2 -s -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 -I$(SRC_DIR)
LIBS = -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32 -luxtheme -ldwmapi

TARGET = taskmgr.exe
SRC = $(SRC_DIR)/taskmgr.c
RC = $(SRC_DIR)/resource.rc
RES = $(SRC_DIR)/resource.o

all: $(TARGET)

$(RES): $(RC) $(SRC_DIR)/app.ico $(SRC_DIR)/taskmgr.manifest $(SRC_DIR)/resource.h $(SRC_DIR)/version.h
	$(WINDRES) --codepage=65001 -I $(SRC_DIR) $(RC) -O coff -o $(RES)

$(TARGET): $(SRC) $(RES) $(SRC_DIR)/resource.h $(SRC_DIR)/version.h
	-taskkill /IM $(TARGET) /F
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(RES) $(LIBS)
	@echo Compilation terminee avec succes : $(TARGET)

clean:
	-taskkill /IM $(TARGET) /F
	rm -f $(TARGET) $(RES)

.PHONY: all clean
