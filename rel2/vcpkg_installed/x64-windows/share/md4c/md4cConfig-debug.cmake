#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "md4c::md4c" for configuration "Debug"
set_property(TARGET md4c::md4c APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(md4c::md4c PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/md4c.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/md4c.dll"
  )

list(APPEND _cmake_import_check_targets md4c::md4c )
list(APPEND _cmake_import_check_files_for_md4c::md4c "${_IMPORT_PREFIX}/debug/lib/md4c.lib" "${_IMPORT_PREFIX}/debug/bin/md4c.dll" )

# Import target "md4c::md4c-html" for configuration "Debug"
set_property(TARGET md4c::md4c-html APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(md4c::md4c-html PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/md4c-html.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/md4c-html.dll"
  )

list(APPEND _cmake_import_check_targets md4c::md4c-html )
list(APPEND _cmake_import_check_files_for_md4c::md4c-html "${_IMPORT_PREFIX}/debug/lib/md4c-html.lib" "${_IMPORT_PREFIX}/debug/bin/md4c-html.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
