#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "md4c::md4c" for configuration "Release"
set_property(TARGET md4c::md4c APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(md4c::md4c PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/md4c.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/md4c.dll"
  )

list(APPEND _cmake_import_check_targets md4c::md4c )
list(APPEND _cmake_import_check_files_for_md4c::md4c "${_IMPORT_PREFIX}/lib/md4c.lib" "${_IMPORT_PREFIX}/bin/md4c.dll" )

# Import target "md4c::md4c-html" for configuration "Release"
set_property(TARGET md4c::md4c-html APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(md4c::md4c-html PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/md4c-html.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/md4c-html.dll"
  )

list(APPEND _cmake_import_check_targets md4c::md4c-html )
list(APPEND _cmake_import_check_files_for_md4c::md4c-html "${_IMPORT_PREFIX}/lib/md4c-html.lib" "${_IMPORT_PREFIX}/bin/md4c-html.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
