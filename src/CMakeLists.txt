set(RUNE_WARNINGS
  -Wall -Wextra -Wpedantic -Walloca -Wcast-align -Wcast-qual
  -Wconversion -Wdouble-promotion -Werror -Wformat=2
  -Wimplicit-fallthrough -Winit-self -Wnon-virtual-dtor
  -Wold-style-cast -Woverloaded-virtual
  -Wredundant-decls -Wshadow -Wsign-conversion -Wsuggest-override
  -Wswitch-enum -Wundef -Wuninitialized -Wunused
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  list(APPEND RUNE_WARNINGS -Wno-restrict)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND RUNE_WARNINGS -Wno-double-promotion -Wno-unneeded-internal-declaration -Wno-unused-but-set-variable)
endif()

# Sanitizers
set(SANITIZER "none" CACHE STRING "Sanitizer to build with: none, asan, tsan")
set_property(CACHE SANITIZER PROPERTY STRINGS none asan tsan)

if(SANITIZER STREQUAL "asan")
  set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize=vptr)
elseif(SANITIZER STREQUAL "tsan")
  set(SANITIZER_FLAGS -fsanitize=thread,undefined -fno-sanitize=vptr)
elseif(SANITIZER STREQUAL "none")
  set(SANITIZER_FLAGS "")
else()
  message(FATAL_ERROR "SANITIZER must be one of: none, asan, tsan (got '${SANITIZER}')")
endif()

file(GLOB RUNE_ENGINE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cc
)
add_library(rune-engine STATIC ${RUNE_ENGINE_SOURCES})
target_include_directories(rune-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rune-engine PUBLIC miniaudio vorbis theoradec SDL2::SDL2-static)
if(WIN32)
  target_link_libraries(rune-engine PUBLIC ws2_32)
endif()

file(GLOB RUNE_RENDER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cc)
add_library(rune-render STATIC ${RUNE_RENDER_SOURCES})
target_include_directories(rune-render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rune-render PUBLIC rune-engine glad)

file(GLOB RUNE_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/*.cc)
add_executable(rune ${RUNE_GAME_SOURCES})
target_link_libraries(rune PRIVATE rune-render SDL2::SDL2-static SDL2::SDL2main)
if(EMSCRIPTEN)
  set(RUNE_WEB_EXCLUDE *.dll *.exe IpDrv.* IpServer.* D3DDrv.int Galaxy.int GlideDrv.int MeTaLDrv.int
    OpenGlDrv.int OpenGlDrv.ini SGLDrv.int SoftDrv.int WinDrv.int Window.int Setup.int Startup.int)
  list(TRANSFORM RUNE_WEB_EXCLUDE PREPEND "SHELL:--exclude-file */")
  target_compile_definitions(rune PRIVATE RUNE_GAME_DATA="/game")
  set_target_properties(rune PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
  set(RUNE_WEB_SHELL ${CMAKE_SOURCE_DIR}/cmake/emscripten/shell.html)
  set_property(TARGET rune APPEND PROPERTY LINK_DEPENDS ${RUNE_WEB_SHELL})
  target_link_options(rune PRIVATE
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=16MB
    -sEXPORTED_RUNTIME_METHODS=FS -lidbfs.js
    -sLZ4=1 --use-preload-cache
    --shell-file ${RUNE_WEB_SHELL}
    "SHELL:--preload-file ${RUNE_GAME_DATA}/System@/game/System"
    ${RUNE_WEB_EXCLUDE}
    "SHELL:--preload-file ${RUNE_GAME_DATA}/Maps@/game/Maps"
    "SHELL:--preload-file ${RUNE_GAME_DATA}/Meshes@/game/Meshes"
    "SHELL:--preload-file ${RUNE_GAME_DATA}/Music@/game/Music"
    "SHELL:--preload-file ${RUNE_GAME_DATA}/Sounds@/game/Sounds"
    "SHELL:--preload-file ${RUNE_GAME_DATA}/Textures@/game/Textures"
  )
  if(EXISTS ${RUNE_GAME_DATA}/Video)
    target_link_options(rune PRIVATE "SHELL:--preload-file ${RUNE_GAME_DATA}/Video@/game/Video")
  endif()
else()
  target_compile_definitions(rune PRIVATE RUNE_GAME_DATA="${RUNE_GAME_DATA}")
endif()

set(RUNE_TARGETS rune-engine rune-render rune)

file(GLOB RUNE_COMPILER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.cc)
add_library(rune-compiler STATIC ${RUNE_COMPILER_SOURCES})
target_include_directories(rune-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rune-compiler PUBLIC rune-engine)
list(APPEND RUNE_TARGETS rune-compiler)

if(NOT EMSCRIPTEN)
  file(GLOB RUNE_UCC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ucc/*.cc)
  add_executable(ucc ${RUNE_UCC_SOURCES})
  target_link_libraries(ucc PRIVATE rune-compiler SDL2::SDL2-static)
  target_compile_definitions(ucc PRIVATE RUNE_GAME_DATA="${RUNE_GAME_DATA}")
  target_link_options(ucc PRIVATE -static-libstdc++ -static-libgcc)
  list(APPEND RUNE_TARGETS ucc)
endif()

set(RUNE_DEMO_DATA "")
if(EXISTS ${RUNE_GAME_DATA}/System/Manifest.ini)
  file(STRINGS ${RUNE_GAME_DATA}/System/Manifest.ini RUNE_DEMO_DATA REGEX "^MasterProduct=Rune Demo")
endif()
if(NOT RUNE_DEMO_DATA)
  file(GLOB RUNE_EXTRA_CLASSES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/script/RuneExtra/Classes/*.uc)
  set(RUNE_EXTRA_PACKAGE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/System/RuneExtra.u)
  set(RUNE_EXTRA_BUNDLE_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/RuneExtraBundle.cmake)
  if(EXISTS ${RUNE_GAME_DATA}/System/RMenu.u)
    if(CMAKE_CROSSCOMPILING)
      include(ExternalProject)
      set(RUNE_PACKAGE_UCC ${CMAKE_BINARY_DIR}/host-ucc/bin/ucc)
      ExternalProject_Add(host-ucc
        SOURCE_DIR ${CMAKE_SOURCE_DIR}
        BINARY_DIR ${CMAKE_BINARY_DIR}/host-ucc
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=c++ -DSKIP_UT=ON -DSKIP_QA=ON -DRUNE_GAME_DATA=${RUNE_GAME_DATA}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ucc
        INSTALL_COMMAND ""
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS ${RUNE_PACKAGE_UCC}
      )
      set(RUNE_PACKAGE_UCC_TARGET host-ucc)
    else()
      set(RUNE_PACKAGE_UCC $<TARGET_FILE:ucc>)
      set(RUNE_PACKAGE_UCC_TARGET ucc)
    endif()
    add_custom_command(
      OUTPUT ${RUNE_EXTRA_PACKAGE}
      COMMAND ${CMAKE_COMMAND} -E env XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/ucc-home ${RUNE_PACKAGE_UCC} makepackage RuneExtra ${CMAKE_CURRENT_SOURCE_DIR}/script/RuneExtra/Classes ${RUNE_EXTRA_PACKAGE}
      DEPENDS ${RUNE_PACKAGE_UCC_TARGET} ${RUNE_EXTRA_CLASSES}
      COMMENT "Building RuneExtra.u"
    )
    add_custom_target(update-rune-extra
      COMMAND ${CMAKE_COMMAND} -DRUNE_EXTRA_BUILT_PACKAGE=${RUNE_EXTRA_PACKAGE} -P ${RUNE_EXTRA_BUNDLE_SCRIPT}
      DEPENDS rune-extra
      COMMENT "Updating bundled RuneExtra.u and source fingerprint"
    )
  else()
    include(${RUNE_EXTRA_BUNDLE_SCRIPT})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${RUNE_EXTRA_CLASSES} ${RUNE_EXTRA_MANIFEST} ${RUNE_EXTRA_BUNDLE})
    add_custom_command(
      OUTPUT ${RUNE_EXTRA_PACKAGE}
      COMMAND ${CMAKE_COMMAND} -P ${RUNE_EXTRA_BUNDLE_SCRIPT}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/System
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNE_EXTRA_BUNDLE} ${RUNE_EXTRA_PACKAGE}
      DEPENDS ${RUNE_EXTRA_BUNDLE} ${RUNE_EXTRA_MANIFEST} ${RUNE_EXTRA_CLASSES} ${RUNE_EXTRA_BUNDLE_SCRIPT}
      COMMENT "Copying bundled RuneExtra.u (no game data required)"
    )
  endif()
  add_custom_target(rune-extra ALL DEPENDS ${RUNE_EXTRA_PACKAGE})
  add_dependencies(rune rune-extra)
  if(EMSCRIPTEN)
    target_link_options(rune PRIVATE "SHELL:--preload-file ${RUNE_EXTRA_PACKAGE}@/System/RuneExtra.u")
    set_property(TARGET rune APPEND PROPERTY LINK_DEPENDS ${RUNE_EXTRA_PACKAGE})
  endif()
endif()

file(GLOB RUNE_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cc)
if(RUNE_EDITOR_SOURCES)
  add_executable(runeed ${RUNE_EDITOR_SOURCES})
  target_link_libraries(runeed PRIVATE rune-render imgui SDL2::SDL2-static SDL2::SDL2main)
  target_compile_definitions(runeed PRIVATE RUNE_GAME_DATA="${RUNE_GAME_DATA}")
  target_link_options(runeed PRIVATE -static-libstdc++ -static-libgcc)
  list(APPEND RUNE_TARGETS runeed)
endif()

foreach(tgt ${RUNE_TARGETS})
  target_compile_options(${tgt} PRIVATE ${RUNE_WARNINGS})

  if(SANITIZER_FLAGS)
    target_compile_options(${tgt} PRIVATE ${SANITIZER_FLAGS} -fno-omit-frame-pointer -g)
    target_link_options(${tgt} PRIVATE ${SANITIZER_FLAGS})
  endif()

  target_compile_options(${tgt} PRIVATE
    $<$<CONFIG:Debug>:-D_GLIBCXX_ASSERTIONS>
  )
endforeach()

if(NOT EMSCRIPTEN)
  target_link_options(rune PRIVATE -static-libstdc++ -static-libgcc)
endif()
