# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.10

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast

# Include any dependencies generated for this target.
include CMakeFiles/tx_rawsock.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/tx_rawsock.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/tx_rawsock.dir/flags.make

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o: CMakeFiles/tx_rawsock.dir/flags.make
CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o: tx_rawsock.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o   -c /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/tx_rawsock.c

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/tx_rawsock.dir/tx_rawsock.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/tx_rawsock.c > CMakeFiles/tx_rawsock.dir/tx_rawsock.c.i

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/tx_rawsock.dir/tx_rawsock.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/tx_rawsock.c -o CMakeFiles/tx_rawsock.dir/tx_rawsock.c.s

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.requires:

.PHONY : CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.requires

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.provides: CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.requires
	$(MAKE) -f CMakeFiles/tx_rawsock.dir/build.make CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.provides.build
.PHONY : CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.provides

CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.provides.build: CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o


CMakeFiles/tx_rawsock.dir/lib.c.o: CMakeFiles/tx_rawsock.dir/flags.make
CMakeFiles/tx_rawsock.dir/lib.c.o: lib.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building C object CMakeFiles/tx_rawsock.dir/lib.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/tx_rawsock.dir/lib.c.o   -c /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/lib.c

CMakeFiles/tx_rawsock.dir/lib.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/tx_rawsock.dir/lib.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/lib.c > CMakeFiles/tx_rawsock.dir/lib.c.i

CMakeFiles/tx_rawsock.dir/lib.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/tx_rawsock.dir/lib.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/lib.c -o CMakeFiles/tx_rawsock.dir/lib.c.s

CMakeFiles/tx_rawsock.dir/lib.c.o.requires:

.PHONY : CMakeFiles/tx_rawsock.dir/lib.c.o.requires

CMakeFiles/tx_rawsock.dir/lib.c.o.provides: CMakeFiles/tx_rawsock.dir/lib.c.o.requires
	$(MAKE) -f CMakeFiles/tx_rawsock.dir/build.make CMakeFiles/tx_rawsock.dir/lib.c.o.provides.build
.PHONY : CMakeFiles/tx_rawsock.dir/lib.c.o.provides

CMakeFiles/tx_rawsock.dir/lib.c.o.provides.build: CMakeFiles/tx_rawsock.dir/lib.c.o


CMakeFiles/tx_rawsock.dir/fec.c.o: CMakeFiles/tx_rawsock.dir/flags.make
CMakeFiles/tx_rawsock.dir/fec.c.o: fec.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building C object CMakeFiles/tx_rawsock.dir/fec.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/tx_rawsock.dir/fec.c.o   -c /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/fec.c

CMakeFiles/tx_rawsock.dir/fec.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/tx_rawsock.dir/fec.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/fec.c > CMakeFiles/tx_rawsock.dir/fec.c.i

CMakeFiles/tx_rawsock.dir/fec.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/tx_rawsock.dir/fec.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/fec.c -o CMakeFiles/tx_rawsock.dir/fec.c.s

CMakeFiles/tx_rawsock.dir/fec.c.o.requires:

.PHONY : CMakeFiles/tx_rawsock.dir/fec.c.o.requires

CMakeFiles/tx_rawsock.dir/fec.c.o.provides: CMakeFiles/tx_rawsock.dir/fec.c.o.requires
	$(MAKE) -f CMakeFiles/tx_rawsock.dir/build.make CMakeFiles/tx_rawsock.dir/fec.c.o.provides.build
.PHONY : CMakeFiles/tx_rawsock.dir/fec.c.o.provides

CMakeFiles/tx_rawsock.dir/fec.c.o.provides.build: CMakeFiles/tx_rawsock.dir/fec.c.o


CMakeFiles/tx_rawsock.dir/xxtea.c.o: CMakeFiles/tx_rawsock.dir/flags.make
CMakeFiles/tx_rawsock.dir/xxtea.c.o: xxtea.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Building C object CMakeFiles/tx_rawsock.dir/xxtea.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/tx_rawsock.dir/xxtea.c.o   -c /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/xxtea.c

CMakeFiles/tx_rawsock.dir/xxtea.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/tx_rawsock.dir/xxtea.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/xxtea.c > CMakeFiles/tx_rawsock.dir/xxtea.c.i

CMakeFiles/tx_rawsock.dir/xxtea.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/tx_rawsock.dir/xxtea.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/xxtea.c -o CMakeFiles/tx_rawsock.dir/xxtea.c.s

CMakeFiles/tx_rawsock.dir/xxtea.c.o.requires:

.PHONY : CMakeFiles/tx_rawsock.dir/xxtea.c.o.requires

CMakeFiles/tx_rawsock.dir/xxtea.c.o.provides: CMakeFiles/tx_rawsock.dir/xxtea.c.o.requires
	$(MAKE) -f CMakeFiles/tx_rawsock.dir/build.make CMakeFiles/tx_rawsock.dir/xxtea.c.o.provides.build
.PHONY : CMakeFiles/tx_rawsock.dir/xxtea.c.o.provides

CMakeFiles/tx_rawsock.dir/xxtea.c.o.provides.build: CMakeFiles/tx_rawsock.dir/xxtea.c.o


# Object files for target tx_rawsock
tx_rawsock_OBJECTS = \
"CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o" \
"CMakeFiles/tx_rawsock.dir/lib.c.o" \
"CMakeFiles/tx_rawsock.dir/fec.c.o" \
"CMakeFiles/tx_rawsock.dir/xxtea.c.o"

# External object files for target tx_rawsock
tx_rawsock_EXTERNAL_OBJECTS =

tx_rawsock: CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o
tx_rawsock: CMakeFiles/tx_rawsock.dir/lib.c.o
tx_rawsock: CMakeFiles/tx_rawsock.dir/fec.c.o
tx_rawsock: CMakeFiles/tx_rawsock.dir/xxtea.c.o
tx_rawsock: CMakeFiles/tx_rawsock.dir/build.make
tx_rawsock: CMakeFiles/tx_rawsock.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Linking C executable tx_rawsock"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/tx_rawsock.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/tx_rawsock.dir/build: tx_rawsock

.PHONY : CMakeFiles/tx_rawsock.dir/build

CMakeFiles/tx_rawsock.dir/requires: CMakeFiles/tx_rawsock.dir/tx_rawsock.c.o.requires
CMakeFiles/tx_rawsock.dir/requires: CMakeFiles/tx_rawsock.dir/lib.c.o.requires
CMakeFiles/tx_rawsock.dir/requires: CMakeFiles/tx_rawsock.dir/fec.c.o.requires
CMakeFiles/tx_rawsock.dir/requires: CMakeFiles/tx_rawsock.dir/xxtea.c.o.requires

.PHONY : CMakeFiles/tx_rawsock.dir/requires

CMakeFiles/tx_rawsock.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/tx_rawsock.dir/cmake_clean.cmake
.PHONY : CMakeFiles/tx_rawsock.dir/clean

CMakeFiles/tx_rawsock.dir/depend:
	cd /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast /mnt/d/ezwbc-libc0607/EZ-WifiBroadcast/root/wifibroadcast/CMakeFiles/tx_rawsock.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/tx_rawsock.dir/depend
