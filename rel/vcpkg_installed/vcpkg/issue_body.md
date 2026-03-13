Package: harfbuzz[core,freetype]:x64-windows@13.0.1

**Host Environment**

- Host: x64-windows
- Compiler: MSVC 19.44.35222.0
- CMake Version: 4.2.3
-    vcpkg-tool version: 2026-03-04-4b3e4c276b5b87a649e66341e11553e8c577459c
    vcpkg-scripts version: efa4634bd5 2026-03-12 (29 hours ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Found Python version '3.14.2 at X:/.tools/vcpkg/downloads/tools/python/python-3.14.2-x64-1/python.exe'
-- Using meson: X:/.tools/vcpkg/downloads/tools/meson-1.9.0-d1fcc2/meson.py
Downloading https://github.com/harfbuzz/harfbuzz/archive/13.0.1.tar.gz -> harfbuzz-harfbuzz-13.0.1.tar.gz
Successfully downloaded harfbuzz-harfbuzz-13.0.1.tar.gz
-- Extracting source X:/.tools/vcpkg/downloads/harfbuzz-harfbuzz-13.0.1.tar.gz
-- Applying patch fix-win32-build.patch
-- Using source at X:/.tools/vcpkg/buildtrees/harfbuzz/src/13.0.1-cadce58297.clean
-- Using cached msys2-mingw-w64-x86_64-pkgconf-1~2.5.1-1-any.pkg.tar.zst
-- Using cached msys2-msys2-runtime-3.6.5-1-x86_64.pkg.tar.zst
-- Using msys root at X:/.tools/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f
-- Configuring x64-windows-dbg
-- Getting CMake variables for x64-windows
-- Loading CMake variables from X:/.tools/vcpkg/buildtrees/harfbuzz/cmake-get-vars_C_CXX-x64-windows.cmake.log
-- Configuring x64-windows-dbg done
-- Configuring x64-windows-rel
-- Configuring x64-windows-rel done
-- Package x64-windows-dbg
CMake Error at scripts/cmake/vcpkg_execute_required_process.cmake:127 (message):
    Command failed: X:\\.tools\\vcpkg\\downloads\\tools\\ninja-1.13.2-windows\\ninja.exe install -v
    Working Directory: X:/.tools/vcpkg/buildtrees/harfbuzz/x64-windows-dbg
    Error code: 1
    See logs for more information:
      X:\.tools\vcpkg\buildtrees\harfbuzz\package-x64-windows-dbg-out.log

Call Stack (most recent call first):
  X:/rel/vcpkg_installed/x64-windows/share/vcpkg-tool-meson/vcpkg_install_meson.cmake:33 (vcpkg_execute_required_process)
  buildtrees/versioning_/versions/harfbuzz/8098a83e22c250a2838832a5c92d76812895e888/portfile.cmake:98 (vcpkg_install_meson)
  scripts/ports.cmake:206 (include)



```

<details><summary>X:\.tools\vcpkg\buildtrees\harfbuzz\package-x64-windows-dbg-out.log</summary>

```
[1/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-paint-bounded.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-paint-bounded.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-paint-bounded.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[2/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-map.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-map.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-map.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[3/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-draw.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-draw.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-draw.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[4/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-buffer-serialize.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-buffer-serialize.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-buffer-serialize.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[5/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-fallback-shape.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-fallback-shape.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-fallback-shape.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[6/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-number.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-number.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-number.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[7/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-buffer-verify.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-buffer-verify.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-buffer-verify.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[8/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-blob.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-blob.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-blob.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[9/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-paint-extents.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-paint-extents.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-paint-extents.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[10/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-common.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-common.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-common.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[11/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-buffer.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-buffer.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-buffer.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[12/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-paint.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-paint.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-paint.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[13/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-outline.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-outline.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-outline.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[14/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-ot-cff1-table.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-ot-cff1-table.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-ot-cff1-table.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[15/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-face-builder.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-face-builder.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-face-builder.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[16/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include" "-IX:/rel/vcpkg_installed/x64-windows/debug/../include/libpng16" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz.dll.p\hb-aat-map.cc.pdb" /Fosrc/harfbuzz.dll.p/hb-aat-map.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-aat-map.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
...
Skipped 521 lines
...
FAILED: [code=1] src/harfbuzz.cc 
"X:\.tools\vcpkg\downloads\tools\python\python-3.14.2-x64-1\python.exe" "C:\Users\danie\source\repos\daniel-medin\dawg\.tools\vcpkg\buildtrees\harfbuzz\src\13.0.1-cadce58297.clean\src\gen-harfbuzzcc.py" "src/harfbuzz.cc" "C:/Users/danie/source/repos/daniel-medin/dawg/.tools/vcpkg/buildtrees/harfbuzz/src/13.0.1-cadce58297.clean/src" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-ankr-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-bsln-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-feat-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-just-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-kerx-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-morx-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-opbd-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout-trak-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout.cc" "../src/13.0.1-cadce58297.clean/src/hb-aat-layout.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-ltag-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-aat-map.cc" "../src/13.0.1-cadce58297.clean/src/hb-aat-map.hh" "../src/13.0.1-cadce58297.clean/src/hb-algs.hh" "../src/13.0.1-cadce58297.clean/src/hb-alloc-pool.hh" "../src/13.0.1-cadce58297.clean/src/hb-array.hh" "../src/13.0.1-cadce58297.clean/src/hb-atomic.hh" "../src/13.0.1-cadce58297.clean/src/hb-bimap.hh" "../src/13.0.1-cadce58297.clean/src/hb-bit-page.hh" "../src/13.0.1-cadce58297.clean/src/hb-bit-set.hh" "../src/13.0.1-cadce58297.clean/src/hb-bit-set-invertible.hh" "../src/13.0.1-cadce58297.clean/src/hb-bit-vector.hh" "../src/13.0.1-cadce58297.clean/src/hb-blob.cc" "../src/13.0.1-cadce58297.clean/src/hb-blob.hh" "../src/13.0.1-cadce58297.clean/src/hb-buffer-serialize.cc" "../src/13.0.1-cadce58297.clean/src/hb-buffer-verify.cc" "../src/13.0.1-cadce58297.clean/src/hb-buffer.cc" "../src/13.0.1-cadce58297.clean/src/hb-buffer.hh" "../src/13.0.1-cadce58297.clean/src/hb-cache.hh" "../src/13.0.1-cadce58297.clean/src/hb-cff-interp-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-cff-interp-cs-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-cff-interp-dict-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-cff1-interp-cs.hh" "../src/13.0.1-cadce58297.clean/src/hb-cff2-interp-cs.hh" "../src/13.0.1-cadce58297.clean/src/hb-common.cc" "../src/13.0.1-cadce58297.clean/src/hb-config.hh" "../src/13.0.1-cadce58297.clean/src/hb-debug.hh" "../src/13.0.1-cadce58297.clean/src/hb-decycler.hh" "../src/13.0.1-cadce58297.clean/src/hb-dispatch.hh" "../src/13.0.1-cadce58297.clean/src/hb-draw.cc" "../src/13.0.1-cadce58297.clean/src/hb-draw.hh" "../src/13.0.1-cadce58297.clean/src/hb-geometry.hh" "../src/13.0.1-cadce58297.clean/src/hb-paint.cc" "../src/13.0.1-cadce58297.clean/src/hb-paint.hh" "../src/13.0.1-cadce58297.clean/src/hb-paint-bounded.cc" "../src/13.0.1-cadce58297.clean/src/hb-paint-bounded.hh" "../src/13.0.1-cadce58297.clean/src/hb-paint-extents.cc" "../src/13.0.1-cadce58297.clean/src/hb-paint-extents.hh" "../src/13.0.1-cadce58297.clean/src/hb-face.cc" "../src/13.0.1-cadce58297.clean/src/hb-face.hh" "../src/13.0.1-cadce58297.clean/src/hb-face-builder.cc" "../src/13.0.1-cadce58297.clean/src/hb-fallback-shape.cc" "../src/13.0.1-cadce58297.clean/src/hb-font.cc" "../src/13.0.1-cadce58297.clean/src/hb-font.hh" "../src/13.0.1-cadce58297.clean/src/hb-free-pool.hh" "../src/13.0.1-cadce58297.clean/src/hb-iter.hh" "../src/13.0.1-cadce58297.clean/src/hb-kern.hh" "../src/13.0.1-cadce58297.clean/src/hb-limits.hh" "../src/13.0.1-cadce58297.clean/src/hb-machinery.hh" "../src/13.0.1-cadce58297.clean/src/hb-map.cc" "../src/13.0.1-cadce58297.clean/src/hb-map.hh" "../src/13.0.1-cadce58297.clean/src/hb-meta.hh" "../src/13.0.1-cadce58297.clean/src/hb-ms-feature-ranges.hh" "../src/13.0.1-cadce58297.clean/src/hb-multimap.hh" "../src/13.0.1-cadce58297.clean/src/hb-mutex.hh" "../src/13.0.1-cadce58297.clean/src/hb-null.hh" "../src/13.0.1-cadce58297.clean/src/hb-number.cc" "../src/13.0.1-cadce58297.clean/src/hb-number.hh" "../src/13.0.1-cadce58297.clean/src/hb-object.hh" "../src/13.0.1-cadce58297.clean/src/hb-open-file.hh" "../src/13.0.1-cadce58297.clean/src/hb-open-type.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff1-std-str.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff1-table.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff1-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff2-table.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-cff2-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-cmap-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-color.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-face-table-list.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-face.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-face.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-font.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-gasp-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-glyf-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-hdmx-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-head-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-hhea-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-hmtx-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-kern-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-base-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-gdef-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-gpos-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-gsub-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-outline.hh" "../src/13.0.1-cadce58297.clean/src/hb-outline.cc" "../src/13.0.1-cadce58297.clean/src/OT/Color/CBDT/CBDT.hh" "../src/13.0.1-cadce58297.clean/src/OT/Color/COLR/COLR.hh" "../src/13.0.1-cadce58297.clean/src/OT/Color/CPAL/CPAL.hh" "../src/13.0.1-cadce58297.clean/src/OT/Color/sbix/sbix.hh" "../src/13.0.1-cadce58297.clean/src/OT/Color/svg/svg.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/glyf.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/glyf-helpers.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/loca.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/path-builder.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/Glyph.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/GlyphHeader.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/SimpleGlyph.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/CompositeGlyph.hh" "../src/13.0.1-cadce58297.clean/src/OT/glyf/SubsetGlyph.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/types.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/Common/Coverage.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/Common/CoverageFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/Common/CoverageFormat2.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/Common/RangeRecord.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GDEF/GDEF.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/AnchorFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/AnchorFormat2.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/AnchorFormat3.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/Anchor.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/AnchorMatrix.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/ChainContextPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/Common.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/ContextPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/CursivePosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/CursivePos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/ExtensionPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/GPOS.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/LigatureArray.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkArray.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkBasePosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkBasePos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkLigPosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkLigPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkMarkPosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkMarkPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/MarkRecord.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PairPosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PairPosFormat2.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PairPos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PairSet.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PairValueRecord.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PosLookup.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/PosLookupSubTable.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/SinglePosFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/SinglePosFormat2.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/SinglePos.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GPOS/ValueFormat.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/AlternateSet.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/AlternateSubstFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/AlternateSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/ChainContextSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/Common.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/ContextSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/ExtensionSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/GSUB.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/Ligature.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/LigatureSet.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/LigatureSubstFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/LigatureSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/MultipleSubstFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/MultipleSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/ReverseChainSingleSubstFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/ReverseChainSingleSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/Sequence.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/SingleSubstFormat1.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/SingleSubstFormat2.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/SingleSubst.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/SubstLookup.hh" "../src/13.0.1-cadce58297.clean/src/OT/Layout/GSUB/SubstLookupSubTable.hh" "../src/13.0.1-cadce58297.clean/src/OT/name/name.hh" "../src/13.0.1-cadce58297.clean/src/OT/Var/VARC/coord-setter.hh" "../src/13.0.1-cadce58297.clean/src/OT/Var/VARC/VARC.cc" "../src/13.0.1-cadce58297.clean/src/OT/Var/VARC/VARC.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-gsubgpos.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout-jstf-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-layout.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-map.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-map.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-math-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-math.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-maxp-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-meta-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-meta.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-metrics.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-metrics.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-name-language-static.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-name-language.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-name-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-name.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-os2-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-os2-unicode-ranges.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-post-macroman.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-post-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-post-table-v2subset.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic-fallback.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic-joining-list.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic-pua.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic-win1256.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-arabic.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-default.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-hangul.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-hebrew.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-indic-table.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-indic.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-indic.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-khmer.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-myanmar.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-syllabic.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-syllabic.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-thai.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-use-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-use.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-vowel-constraints.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper-vowel-constraints.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shaper.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape-fallback.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape-fallback.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape-normalize.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape-normalize.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-shape.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-stat-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-tag-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-tag.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-avar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-cvar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-fvar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-gvar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-hvar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-mvar-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var-varc-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ot-var.cc" "../src/13.0.1-cadce58297.clean/src/hb-ot-vorg-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-priority-queue.hh" "../src/13.0.1-cadce58297.clean/src/hb-repacker.hh" "../src/13.0.1-cadce58297.clean/src/hb-sanitize.hh" "../src/13.0.1-cadce58297.clean/src/hb-serialize.hh" "../src/13.0.1-cadce58297.clean/src/hb-set-digest.hh" "../src/13.0.1-cadce58297.clean/src/hb-set.cc" "../src/13.0.1-cadce58297.clean/src/hb-set.hh" "../src/13.0.1-cadce58297.clean/src/hb-shape-plan.cc" "../src/13.0.1-cadce58297.clean/src/hb-shape-plan.hh" "../src/13.0.1-cadce58297.clean/src/hb-shape.cc" "../src/13.0.1-cadce58297.clean/src/hb-shaper-impl.hh" "../src/13.0.1-cadce58297.clean/src/hb-shaper-list.hh" "../src/13.0.1-cadce58297.clean/src/hb-shaper.cc" "../src/13.0.1-cadce58297.clean/src/hb-shaper.hh" "../src/13.0.1-cadce58297.clean/src/hb-static.cc" "../src/13.0.1-cadce58297.clean/src/hb-string-array.hh" "../src/13.0.1-cadce58297.clean/src/hb-style.cc" "../src/13.0.1-cadce58297.clean/src/hb-ucd-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-ucd.cc" "../src/13.0.1-cadce58297.clean/src/hb-unicode-emoji-table.hh" "../src/13.0.1-cadce58297.clean/src/hb-unicode.cc" "../src/13.0.1-cadce58297.clean/src/hb-unicode.hh" "../src/13.0.1-cadce58297.clean/src/hb-utf.hh" "../src/13.0.1-cadce58297.clean/src/hb-vector.hh" "../src/13.0.1-cadce58297.clean/src/hb.hh" "../src/13.0.1-cadce58297.clean/src/hb-glib.cc" "../src/13.0.1-cadce58297.clean/src/hb-ft.cc" "../src/13.0.1-cadce58297.clean/src/hb-ft-colr.hh" "../src/13.0.1-cadce58297.clean/src/hb-graphite2.cc" "../src/13.0.1-cadce58297.clean/src/hb-uniscribe.cc" "../src/13.0.1-cadce58297.clean/src/hb-gdi.cc" "../src/13.0.1-cadce58297.clean/src/hb-directwrite.cc" "../src/13.0.1-cadce58297.clean/src/hb-directwrite.hh" "../src/13.0.1-cadce58297.clean/src/hb-directwrite-font.cc" "../src/13.0.1-cadce58297.clean/src/hb-directwrite-shape.cc" "../src/13.0.1-cadce58297.clean/src/hb-coretext.cc" "../src/13.0.1-cadce58297.clean/src/hb-coretext.hh" "../src/13.0.1-cadce58297.clean/src/hb-coretext-font.cc" "../src/13.0.1-cadce58297.clean/src/hb-coretext-shape.cc" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api.cc" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-blob.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-buffer.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-common.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-face.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-font.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-list.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-api-shape.hh" "../src/13.0.1-cadce58297.clean/src/hb-wasm-shape.cc"
Traceback (most recent call last):
  File "C:\Users\danie\source\repos\daniel-medin\dawg\.tools\vcpkg\buildtrees\harfbuzz\src\13.0.1-cadce58297.clean\src\gen-harfbuzzcc.py", line 17, in <module>
    f.write ("".join ('#include "{}"\n'.format (pathlib.Path( os.path.relpath (os.path.abspath (x), CURRENT_SOURCE_DIR) ).as_posix()) for x in sources if x.endswith (".cc")).encode ())
             ~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "C:\Users\danie\source\repos\daniel-medin\dawg\.tools\vcpkg\buildtrees\harfbuzz\src\13.0.1-cadce58297.clean\src\gen-harfbuzzcc.py", line 17, in <genexpr>
    f.write ("".join ('#include "{}"\n'.format (pathlib.Path( os.path.relpath (os.path.abspath (x), CURRENT_SOURCE_DIR) ).as_posix()) for x in sources if x.endswith (".cc")).encode ())
                                                              ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "<frozen ntpath>", line 763, in relpath
ValueError: path is on mount 'X:', start on mount 'C:'
[93/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-raster.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-raster.dll.p\hb-raster-svg-parse.cc.pdb" /Fosrc/harfbuzz-raster.dll.p/hb-raster-svg-parse.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-raster-svg-parse.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[94/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-vector.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-vector.dll.p\hb-vector-svg-path.cc.pdb" /Fosrc/harfbuzz-vector.dll.p/hb-vector-svg-path.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-vector-svg-path.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[95/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-raster.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-raster.dll.p\hb-raster-svg-use.cc.pdb" /Fosrc/harfbuzz-raster.dll.p/hb-raster-svg-use.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-raster-svg-use.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[96/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-vector.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-vector.dll.p\hb-vector-svg-paint.cc.pdb" /Fosrc/harfbuzz-vector.dll.p/hb-vector-svg-paint.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-vector-svg-paint.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[97/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-subset.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-subset.dll.p\hb-subset-table-var.cc.pdb" /Fosrc/harfbuzz-subset.dll.p/hb-subset-table-var.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-subset-table-var.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[98/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-vector.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-vector.dll.p\hb-vector-svg-utils.cc.pdb" /Fosrc/harfbuzz-vector.dll.p/hb-vector-svg-utils.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-vector-svg-utils.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[99/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-subset.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-subset.dll.p\hb-subset-table-other.cc.pdb" /Fosrc/harfbuzz-subset.dll.p/hb-subset-table-other.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-subset-table-other.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[100/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-vector.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-vector.dll.p\hb-vector-svg-subset.cc.pdb" /Fosrc/harfbuzz-vector.dll.p/hb-vector-svg-subset.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-vector-svg-subset.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[101/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-subset.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-subset.dll.p\hb-subset.cc.pdb" /Fosrc/harfbuzz-subset.dll.p/hb-subset.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-subset.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[102/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-subset.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-subset.dll.p\hb-subset-table-color.cc.pdb" /Fosrc/harfbuzz-subset.dll.p/hb-subset-table-color.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-subset-table-color.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[103/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-subset.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-subset.dll.p\hb-subset-table-layout.cc.pdb" /Fosrc/harfbuzz-subset.dll.p/hb-subset-table-layout.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-subset-table-layout.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[104/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-raster.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-raster.dll.p\hb-static.cc.pdb" /Fosrc/harfbuzz-raster.dll.p/hb-static.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-static.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
[105/109] "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-Isrc\harfbuzz-vector.dll.p" "-Isrc" "-I..\src\13.0.1-cadce58297.clean\src" "-I." "-I..\src\13.0.1-cadce58297.clean" "-IX:/rel/vcpkg_installed/x64-windows/include" "/MDd" "/nologo" "/showIncludes" "/utf-8" "/Zc:__cplusplus" "/W2" "/EHs-c-" "/std:c++14" "/permissive-" "/Zi" "/wd4244" "/bigobj" "/utf-8" "-DHAVE_CONFIG_H" "-nologo" "-DWIN32" "-D_WINDOWS" "-utf-8" "-GR" "-EHsc" "-MP" "-MDd" "-Z7" "-Ob0" "-Od" "-RTC1" "-DHB_DLL_EXPORT" "/Fdsrc\harfbuzz-vector.dll.p\hb-static.cc.pdb" /Fosrc/harfbuzz-vector.dll.p/hb-static.cc.obj "/c" ../src/13.0.1-cadce58297.clean/src/hb-static.cc
cl : Command line warning D9025 : overriding '/EHs' with '/EHs-'
cl : Command line warning D9025 : overriding '/EHc' with '/EHc-'
cl : Command line warning D9025 : overriding '/Z7' with '/Zi'
cl : Command line warning D9025 : overriding '/EHs-' with '/EHs'
cl : Command line warning D9025 : overriding '/EHc-' with '/EHc'
cl : Command line warning D9025 : overriding '/Zi' with '/Z7'
ninja: build stopped: subcommand failed.
```
</details>

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "dawg",
  "version-string": "0.1.0",
  "builtin-baseline": "efa4634bd526b87559684607d2cbbdeeec0f07d8",
  "dependencies": [
    "qtbase",
    "ffmpeg",
    {
      "name": "opencv4",
      "default-features": false,
      "features": [
        "ffmpeg"
      ]
    }
  ]
}

```
</details>
