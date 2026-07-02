@echo off
setlocal

set "VS_INSIDERS=C:\Program Files\Microsoft Visual Studio\18\Insiders"
set "VCVARS=%VS_INSIDERS%\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE_EXE=%VS_INSIDERS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "%VCVARS%" (
    echo Visual Studio Insiders vcvars64.bat was not found:
    echo %VCVARS%
    exit /b 1
)

if not exist "%CMAKE_EXE%" (
    echo Visual Studio Insiders CMake was not found:
    echo %CMAKE_EXE%
    exit /b 1
)

call "%VCVARS%" || exit /b 1
"%CMAKE_EXE%" -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
"%CMAKE_EXE%" --build build-release || exit /b 1

echo.
echo Built Mix Teacher VST3 under build-release\MixTeacher_artefacts\Release\VST3
