TARGET   = bin\kestrel.exe

CC       = g++
WINDRES  = windres

SRC_DIR     = src
INCLUDE_DIR = include
OBJ_DIR     = obj
BIN_DIR     = bin

CXXFLAGS = -Wall -O2 -MMD -MP -I$(INCLUDE_DIR)
LDFLAGS  = -mwindows -static -static-libgcc -static-libstdc++

SRC = $(wildcard $(SRC_DIR)/*.cpp)
OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRC))
DEP = $(OBJ:.o=.d)

RC      = app.rc
RES_OBJ = $(OBJ_DIR)/app_rc.o

LIBS = -lwbemuuid -lole32 -loleaut32 -loleacc -luuid \
       -lgdi32 -lshell32 -lsetupapi -ld2d1 -ldwrite \
       -ldwmapi -lpdh -liphlpapi -ladvapi32 -lwinpthread -lpsapi \
       -ltaskschd -lshlwapi -luser32

all: $(TARGET)

$(TARGET): $(OBJ) $(RES_OBJ) | $(BIN_DIR)
	$(CC) $(OBJ) $(RES_OBJ) -o $(TARGET) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(RES_OBJ): $(RC) app.manifest $(INCLUDE_DIR)/resource_bi.h $(INCLUDE_DIR)/app_identity_bi.h assets/kestrel.ico | $(OBJ_DIR)
	$(WINDRES) -I$(INCLUDE_DIR) -I. $(RC) -O coff -o $@

$(OBJ_DIR):
	@if not exist "$(OBJ_DIR)" md "$(OBJ_DIR)"

$(BIN_DIR):
	@if not exist "$(BIN_DIR)" md "$(BIN_DIR)"

clean:
	@if exist "$(OBJ_DIR)" rmdir /S /Q "$(OBJ_DIR)"
	@if exist "$(BIN_DIR)" rmdir /S /Q "$(BIN_DIR)"

-include $(DEP)

.PHONY: all clean
