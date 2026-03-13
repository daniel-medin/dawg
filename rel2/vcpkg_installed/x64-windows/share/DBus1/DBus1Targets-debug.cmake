#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dbus-1" for configuration "Debug"
set_property(TARGET dbus-1 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-1 PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/dbus-1.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/dbus-1-3.dll"
  )

list(APPEND _cmake_import_check_targets dbus-1 )
list(APPEND _cmake_import_check_files_for_dbus-1 "${_IMPORT_PREFIX}/debug/lib/dbus-1.lib" "${_IMPORT_PREFIX}/debug/bin/dbus-1-3.dll" )

# Import target "dbus-daemon" for configuration "Debug"
set_property(TARGET dbus-daemon APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-daemon PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-daemon.exe"
  )

list(APPEND _cmake_import_check_targets dbus-daemon )
list(APPEND _cmake_import_check_files_for_dbus-daemon "${_IMPORT_PREFIX}/tools/dbus/dbus-daemon.exe" )

# Import target "dbus-send" for configuration "Debug"
set_property(TARGET dbus-send APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-send PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-send.exe"
  )

list(APPEND _cmake_import_check_targets dbus-send )
list(APPEND _cmake_import_check_files_for_dbus-send "${_IMPORT_PREFIX}/tools/dbus/dbus-send.exe" )

# Import target "dbus-test-tool" for configuration "Debug"
set_property(TARGET dbus-test-tool APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-test-tool PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-test-tool.exe"
  )

list(APPEND _cmake_import_check_targets dbus-test-tool )
list(APPEND _cmake_import_check_files_for_dbus-test-tool "${_IMPORT_PREFIX}/tools/dbus/dbus-test-tool.exe" )

# Import target "dbus-update-activation-environment" for configuration "Debug"
set_property(TARGET dbus-update-activation-environment APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-update-activation-environment PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-update-activation-environment.exe"
  )

list(APPEND _cmake_import_check_targets dbus-update-activation-environment )
list(APPEND _cmake_import_check_files_for_dbus-update-activation-environment "${_IMPORT_PREFIX}/tools/dbus/dbus-update-activation-environment.exe" )

# Import target "dbus-launch" for configuration "Debug"
set_property(TARGET dbus-launch APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-launch PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-launch.exe"
  )

list(APPEND _cmake_import_check_targets dbus-launch )
list(APPEND _cmake_import_check_files_for_dbus-launch "${_IMPORT_PREFIX}/tools/dbus/dbus-launch.exe" )

# Import target "dbus-monitor" for configuration "Debug"
set_property(TARGET dbus-monitor APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-monitor PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-monitor.exe"
  )

list(APPEND _cmake_import_check_targets dbus-monitor )
list(APPEND _cmake_import_check_files_for_dbus-monitor "${_IMPORT_PREFIX}/tools/dbus/dbus-monitor.exe" )

# Import target "dbus-run-session" for configuration "Debug"
set_property(TARGET dbus-run-session APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dbus-run-session PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/dbus/dbus-run-session.exe"
  )

list(APPEND _cmake_import_check_targets dbus-run-session )
list(APPEND _cmake_import_check_files_for_dbus-run-session "${_IMPORT_PREFIX}/tools/dbus/dbus-run-session.exe" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
