@echo off
REM av 프로젝트 Windows 빌드 (Visual Studio 2022 + LLVM/clang-cl)
REM 사용법: script\build-windows.bat [Release|Debug]

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

echo ==> av Windows 빌드 시작 (config=%BUILD_TYPE%, toolset=ClangCL)

cmake -B build -G "Visual Studio 17 2022" -T ClangCL
if errorlevel 1 ( echo CMake configure 실패 & exit /b 1 )

cmake --build build --config %BUILD_TYPE% -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 ( echo 빌드 실패 & exit /b 1 )

echo ==> 빌드 성공: bin\av.exe
