@echo off
rem Builds build\llamatron.exe (Release, clang, Ninja) inside the VS x64 environment.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%"
if not exist build\build.ninja cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
cmake --build "%~dp0build" %*
