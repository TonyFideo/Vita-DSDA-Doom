include_guard()

if(STRICT_FIND)
  set(dsda_strict_keyword REQUIRED)
endif()

add_library(dsda_dependencies INTERFACE IMPORTED)
add_library(dsda::dependencies ALIAS dsda_dependencies)

set(dsda_platform_libraries)

if(DSDA_ENABLE_OPENGL_RENDERER)
  # Make sure OpenGL.framework is found, XQuartz may show up first and only supports GL 1.4
  if(APPLE)
    set(find_framework_backup ${CMAKE_FIND_FRAMEWORK})
    set(CMAKE_FIND_FRAMEWORK ONLY)
  endif()

  set(OpenGL_GL_PREFERENCE LEGACY)
  find_package(OpenGL 2.0 REQUIRED)
  list(APPEND dsda_platform_libraries OpenGL::GL OpenGL::GLU)

  if(APPLE)
    set(CMAKE_FIND_FRAMEWORK ${find_framework_backup})
  endif()
endif()

if(VITA AND DSDA_VITA_PRESENT_VITAGL)
  find_path(VITAGL_INCLUDE_DIR
    NAMES vitaGL.h
    PATHS "$ENV{VITASDK}/arm-vita-eabi/include"
    REQUIRED
  )

  find_library(VITAGL_LIBRARY NAMES vitaGL REQUIRED)
  find_library(VITASHARK_LIBRARY NAMES vitashark REQUIRED)
  find_library(MATHNEON_LIBRARY NAMES mathneon REQUIRED)
  find_library(SHACCCG_EXT_LIBRARY NAMES SceShaccCgExt REQUIRED)
  find_library(TAIHEN_STUB_LIBRARY NAMES taihen_stub REQUIRED)
  find_library(SHACCCG_STUB_LIBRARY NAMES SceShaccCg_stub REQUIRED)
  find_library(KERNEL_DMAC_STUB_LIBRARY NAMES SceKernelDmacMgr_stub REQUIRED)
  find_library(GXM_STUB_LIBRARY NAMES SceGxm_stub REQUIRED)
  find_library(DISPLAY_STUB_LIBRARY NAMES SceDisplay_stub REQUIRED)
  find_library(APP_MGR_STUB_LIBRARY NAMES SceAppMgr_stub REQUIRED)
  find_library(COMMON_DIALOG_STUB_LIBRARY NAMES SceCommonDialog_stub REQUIRED)

  target_include_directories(dsda_dependencies INTERFACE "${VITAGL_INCLUDE_DIR}")

  list(APPEND dsda_platform_libraries
    "${VITAGL_LIBRARY}"
    "${VITASHARK_LIBRARY}"
    "${MATHNEON_LIBRARY}"
    "${SHACCCG_EXT_LIBRARY}"
    "${TAIHEN_STUB_LIBRARY}"
    "${SHACCCG_STUB_LIBRARY}"
    "${KERNEL_DMAC_STUB_LIBRARY}"
    "${GXM_STUB_LIBRARY}"
    "${DISPLAY_STUB_LIBRARY}"
    "${APP_MGR_STUB_LIBRARY}"
    "${COMMON_DIALOG_STUB_LIBRARY}"
  )
endif()

find_package(SDL2 2.0.12 CONFIG REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(SndFile 1.0.29 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libzip REQUIRED)

# ZLIB 1.3.2 static now requires explicit ZLIBSTATIC
if(NOT TARGET ZLIB::ZLIB AND TARGET ZLIB::ZLIBSTATIC)
  add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
endif()

if(SndFile_VERSION VERSION_GREATER_EQUAL "1.1.0")
   set(HAVE_SNDFILE_MPEG TRUE)
endif()

if(WITH_IMAGE)
  find_package(SDL2_image ${dsda_strict_keyword})
  if(SDL2_image_FOUND)
    set(HAVE_LIBSDL2_IMAGE TRUE)
  endif()
endif()

if(WITH_MAD)
  find_package(mad ${dsda_strict_keyword})
  if(mad_FOUND)
    set(HAVE_LIBMAD TRUE)
  endif()
endif()

if(WITH_FLUIDSYNTH)
  find_package(FluidSynth ${dsda_strict_keyword})
  if(FluidSynth_FOUND)
    set(HAVE_LIBFLUIDSYNTH TRUE)
  endif()
endif()

if(WITH_XMP)
  find_package(libxmp ${dsda_strict_keyword})
  if(libxmp_FOUND)
    set(HAVE_LIBXMP TRUE)
  endif()
endif()

if(WITH_VORBISFILE)
  find_package(Vorbis ${dsda_strict_keyword})
  if(Vorbis_File_FOUND)
    set(HAVE_LIBVORBISFILE TRUE)
  endif()
endif()

if(WITH_PORTMIDI)
  find_package(PortMidi ${dsda_strict_keyword})
  if(PortMidi_FOUND)
    set(HAVE_LIBPORTMIDI TRUE)
  endif()
endif()

# Before SDL 2.24.x, all SDL2::* targets were created in autotools builds, even if the underlying library wasn't built.
# So wrap all this in our interface library
add_library(dsda_SDL2 INTERFACE IMPORTED)
add_library(dsda::SDL2 ALIAS dsda_SDL2)

if(TARGET SDL2::SDL2main)
  get_target_property(sdl2_main_library SDL2::SDL2main LOCATION)
  if(EXISTS ${sdl2_main_library})
    target_link_libraries(dsda_SDL2 INTERFACE SDL2::SDL2main)
  endif()
endif()

if(TARGET SDL2::SDL2)
  get_target_property(sdl2_library SDL2::SDL2 LOCATION)
endif()
if(TARGET SDL2::SDL2-static)
  get_target_property(sdl2_static_library SDL2::SDL2-static LOCATION)
endif()

if(EXISTS ${sdl2_library})
  target_link_libraries(dsda_SDL2 INTERFACE SDL2::SDL2)
elseif(EXISTS ${sdl2_static_library})
  target_link_libraries(dsda_SDL2 INTERFACE SDL2::SDL2-static)
else()
  message(FATAL_ERROR "Neither '${sdl2_library}' or '${sdl2_static_library}' exist, your SDL installation may be broken.")
endif()

# TODO: Rework libxmp's find module to avoid this workaround
add_library(dsda_xmp INTERFACE IMPORTED)
add_library(dsda::xmp ALIAS dsda_xmp)

if(HAVE_LIBXMP)
  if(TARGET libxmp::xmp)
    target_link_libraries(dsda_xmp INTERFACE libxmp::xmp)
  else()
    target_link_libraries(dsda_xmp
      INTERFACE
      $<IF:$<TARGET_EXISTS:libxmp::xmp_shared>,libxmp::xmp_shared,libxmp::xmp_static>
    )
  endif()
endif()

target_link_libraries(dsda_dependencies
  INTERFACE
  ${dsda_platform_libraries}
  SndFile::sndfile
  libzip::zip
  ZLIB::ZLIB

  dsda::xmp
  $<$<BOOL:${HAVE_LIBMAD}>:mad::mad>
  $<$<BOOL:${HAVE_LIBFLUIDSYNTH}>:FluidSynth::libfluidsynth>
  $<$<BOOL:${HAVE_LIBVORBISFILE}>:Vorbis::vorbisfile>
  $<$<BOOL:${HAVE_LIBPORTMIDI}>:PortMidi::portmidi>

  $<$<BOOL:${HAVE_LIBSDL2_IMAGE}>:$<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>>
  $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>

  dsda::SDL2
)

if(WIN32)
  target_link_libraries(dsda_dependencies
    INTERFACE
    winmm
    comctl32
  )
endif()
