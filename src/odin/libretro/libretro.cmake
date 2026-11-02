# The libretro core: cmake -B build -DODIN_LIBRETRO=ON -DODIN_NATIVE=OFF
#
# odin_libretro.so (.dll, .dylib): the board, the OpenGL renderer and
# libretro_odin.cpp, without the program's window, menu and SDL. Included
# from the top CMakeLists.txt in place of the program.

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)

# jansson from 3rdparty/, static and position independent
set(JANSSON_BUILD_SHARED_LIBS OFF)
set(JANSSON_EXAMPLES OFF)
set(JANSSON_WITHOUT_TESTS ON)
set(JANSSON_BUILD_DOCS OFF)
set(JANSSON_INSTALL OFF)
add_subdirectory(3rdparty/jansson EXCLUDE_FROM_ALL)

# GLEW built here as for OSMesa: it asks OSMesaGetProcAddress, which the
# core answers with the frontend's get_proc_address (shim/GL/osmesa.h), so
# it links neither GLX nor EGL
add_library(odin_lr_glew STATIC 3rdparty/glew/src/glew.c)
target_include_directories(odin_lr_glew PUBLIC 3rdparty/glew/include)
target_include_directories(odin_lr_glew PRIVATE src/odin/libretro/shim)
target_compile_definitions(odin_lr_glew PUBLIC GLEW_STATIC GLEW_NO_GLU PRIVATE GLEW_OSMESA)
target_compile_options(odin_lr_glew PRIVATE -O2)

set(LR src/odin)
file(GLOB_RECURSE LR_MACHINE CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/odin/machine/*.cpp)
file(GLOB LR_RENDERER CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/src/odin/host/renderer/hikaru/*.cpp
	${CMAKE_SOURCE_DIR}/src/odin/host/renderer/hikaru/gl/*.cpp)
set(LR_SOURCES
	${LR_MACHINE} ${LR_RENDERER}
	${LR}/common/cabinet_settings.cpp ${LR}/common/wav_file.cpp
	${LR}/host/gfx/gl_common.cpp ${LR}/host/gfx/program_cache.cpp
	${LR}/host/video/picture_filter_gl.cpp ${LR}/host/video/picture_preset.cpp
	${LR}/host/ui/loading_screen.cpp
	${LR}/host/input/roles.cpp ${LR}/host/input/sockets.cpp
	${LR}/host/app/games.cpp ${LR}/host/app/rom_files.cpp ${LR}/host/app/user_files.cpp
	${LR}/host/app/settings.cpp ${LR}/host/app/zip_crc.cpp
	${LR}/libretro/libretro_odin.cpp)

set(LR_ARCHIVE 7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zFile.c 7zStream.c
	Bcj2.c Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c)
list(TRANSFORM LR_ARCHIVE PREPEND src/lzma/)
list(APPEND LR_ARCHIVE src/miniz/miniz.c)
add_library(odin_lr_archive OBJECT ${LR_ARCHIVE})
target_compile_options(odin_lr_archive PRIVATE -O2)
set_source_files_properties(src/miniz/miniz.c PROPERTIES COMPILE_DEFINITIONS USE_EXTERNAL_MZCRC)
set_source_files_properties(src/odin/host/app/zip_crc.cpp PROPERTIES
	COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/src/miniz")
set_source_files_properties(src/odin/host/app/rom_files.cpp PROPERTIES
	COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/src/miniz;-isystem;${CMAKE_SOURCE_DIR}/src/lzma")

# The shaders' texts and the game list, in the binary
set(LR_GFX ${CMAKE_SOURCE_DIR}/src/odin/host/gfx)
file(GLOB LR_GLSL_FILES CONFIGURE_DEPENDS ${LR_GFX}/glsl/*)
list(FILTER LR_GLSL_FILES EXCLUDE REGEX "\\.spv$")
set(LR_EMBEDDED ${LR_GFX}/glsl_texts.S ${CMAKE_SOURCE_DIR}/src/odin/libretro/games_json.S)
set_source_files_properties(${LR_GFX}/glsl_texts.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${LR_GFX}" OBJECT_DEPENDS "${LR_GLSL_FILES}")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/odin/libretro/games_json.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${CMAKE_SOURCE_DIR}" OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/odin-games.json")

add_library(odin_libretro SHARED ${LR_SOURCES} ${LR_EMBEDDED} $<TARGET_OBJECTS:odin_lr_archive>)
set_target_properties(odin_libretro PROPERTIES PREFIX "" OUTPUT_NAME odin_libretro
	CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
	CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(odin_libretro PRIVATE src src/odin/libretro src/odin/libretro/shim)
target_compile_definitions(odin_libretro PRIVATE ODIN_LIBRETRO)
if(NOT ODIN_VERSION STREQUAL "")
	target_compile_definitions(odin_libretro PRIVATE ODIN_LIBRETRO_VERSION="${ODIN_VERSION}")
endif()
target_compile_options(odin_libretro PRIVATE -pthread ${OPT_FLAGS})
target_link_options(odin_libretro PRIVATE -pthread ${OPT_FLAGS})
target_link_libraries(odin_libretro PRIVATE odin_lr_glew jansson Threads::Threads)

if(WIN32)
	target_link_libraries(odin_libretro PRIVATE opengl32 ws2_32)
	target_link_options(odin_libretro PRIVATE -static -static-libgcc -static-libstdc++)
elseif(APPLE)
	target_link_libraries(odin_libretro PRIVATE "-framework OpenGL")
else()
	# libOpenGL (glvnd): the OpenGL functions alone, no GLX and no X11; the
	# frontend's context, made with EGL or GLX, is the current one
	find_package(OpenGL REQUIRED COMPONENTS OpenGL)
	target_link_libraries(odin_libretro PRIVATE OpenGL::OpenGL m)
	# Only the libretro API is exported, and the core's own symbols are not
	# taken over by the frontend's
	target_link_options(odin_libretro PRIVATE -Wl,-Bsymbolic -Wl,--version-script=${CMAKE_SOURCE_DIR}/src/odin/libretro/link.T -Wl,--no-undefined)
endif()
