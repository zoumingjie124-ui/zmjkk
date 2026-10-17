@echo off
rem PID offline unit test runner (host MSVC build, no STM32 involved)
rem Uses explicit MSVC/SDK paths so it works without vcvars64 (reg.exe-free).
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64
cd /d "%~dp0"
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /W4 /I..\App\Inc test_pid.c /Fe:test_pid.exe > cl_build.log 2>&1
if errorlevel 1 goto buildfail
test_pid.exe > pid_test.log 2>&1
set RET=%errorlevel%
type pid_test.log
exit /b %RET%
:buildfail
echo BUILD FAILED
type cl_build.log
exit /b 1
