@echo off
rem Host-side test runner: PID unit tests + param codec tests
rem Explicit MSVC/SDK paths (no vcvars64, works without reg.exe)
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64
cd /d "%~dp0"
set FAILED=0

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /W4 /I..\App\Inc test_pid.c /Fe:test_pid.exe > cl_build_pid.log 2>&1
if errorlevel 1 goto pid_build_fail
test_pid.exe > pid_test.log 2>&1
if errorlevel 1 set FAILED=1
type pid_test.log

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /W4 /I..\App\Inc test_param.c /Fe:test_param.exe > cl_build_param.log 2>&1
if errorlevel 1 goto param_build_fail
test_param.exe > param_test.log 2>&1
if errorlevel 1 set FAILED=1
type param_test.log

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /W4 /I stubs /I ..\App\Inc /I ..\Hardware test_control.c /Fe:test_control.exe > cl_build_control.log 2>&1
if errorlevel 1 goto control_build_fail
test_control.exe > control_test.log 2>&1
if errorlevel 1 set FAILED=1
type control_test.log

if "%FAILED%"=="1" (
  echo HOST TESTS: SOME FAILED
  exit /b 1
)
echo HOST TESTS: ALL PASS
exit /b 0

:pid_build_fail
echo PID BUILD FAILED
type cl_build_pid.log
exit /b 1
:control_build_fail
echo CONTROL BUILD FAILED
type cl_build_control.log
exit /b 1
:param_build_fail
echo PARAM BUILD FAILED
type cl_build_param.log
exit /b 1
