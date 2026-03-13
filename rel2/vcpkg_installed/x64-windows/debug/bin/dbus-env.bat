:: environment setting for dbus clients
@echo off

:: session bus address
set DBUS_SESSION_BUS_ADDRESS=autolaunch:

:: system bus address
set DBUS_SYSTEM_BUS_DEFAULT_ADDRESS=unix:path=C:/dawg-short/.tools/vcpkg/packages/dbus_x64-windows/debug/var/run/dbus/system_bus_socket
