@echo off
set "CL_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64\cl.exe"
set "MSVC_INC=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include"
set "MSVC_LIB=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\lib\x64"
set "UCRT_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.18362.0\ucrt"
set "UCRT_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.18362.0\ucrt\x64"
set "UM_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.18362.0\um"
set "UM_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.18362.0\um\x64"
set "SHARED_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.18362.0\shared"

cd /d "C:\Users\user\Desktop\Transfo_Model"

"%CL_EXE%" /EHsc /std:c++17 /O2 /I "core\include" /I "%MSVC_INC%" /I "%UCRT_INC%" /I "%UM_INC%" /I "%SHARED_INC%" Tests\test_harrison_micpre.cpp /Fe:Tests\test_harrison_micpre.exe /link /LIBPATH:"%MSVC_LIB%" /LIBPATH:"%UCRT_LIB%" /LIBPATH:"%UM_LIB%"
