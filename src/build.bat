@echo off

REM Build for Visual Studio compiler. Run your copy of vcvarsall.bat to setup command-line compiler.
REM Check if build tool is on path
REM >nul, 2>nul will remove the output text from the where command
where /q cl.exe
if %errorlevel%==1 (
	echo MSVC CL not on path, please add it to path to build by command line.
	goto end
)

REM Build tags file if you have ctags in path
where /q ctags
if %errorlevel%==0 (
	ctags -R
)

where /q ctime
set HaveCTimeBinary=0
if %errorlevel%==0 (
	set HaveCTimeBinary=1
)

set ProjectName=AeroPoint
if %HaveCTimeBinary%==1 ctime -begin ..\src\%ProjectName%.ctm

IF NOT EXIST ..\bin mkdir ..\bin
pushd ..\bin

REM ////////////////////////////////////////////////////////////////////////////
REM Compile Switches
REM ////////////////////////////////////////////////////////////////////////////
REM EHa-   disable exception handling (currently it's on /EHsc since libraries need)
REM GR-    disable c runtime type information (we don't use)
REM MD     use dynamic runtime library
REM MT     use static runtime library, so build and link it into exe
REM Oi     enable intrinsics optimisation, let us use CPU intrinsics if there is one
REM        instead of generating a call to external library (i.e. CRT).
REM Zi     enables debug data, Z7 combines the debug files into one.
REM W4     warning level 4
REM WX     treat warnings as errors
REM wd4100 unused argument parameters
REM wd4201 nonstandard extension used: nameless struct/union
REM wd4189 local variable is initialised but not referenced
REM wd4505 unreferenced local function not used will be removed
set CompileFlags=-EHa- -GR- -MT -Oi -Z7 -W4 -wd4100 -wd4201 -wd4189 -wd4505 -FAsc /I..\src\external\

REM Link libraries
set LinkLibraries=..\src\external\curl\libcurl_a.lib user32.lib

REM incremental:no,   turn incremental builds off
REM opt:ref,          try to remove functions from libs that are not referenced at all
set LinkFlags=-incremental:no -opt:ref -subsystem:CONSOLE -machine:x64 -nologo

set DebugMode=1
if %DebugMode%==1 goto :DebugFlags
goto :ReleaseFlags

:DebugFlags
REM Od     disables optimisations
REM RTC1   runtime error checks, only possible with optimisations disabled
set CompileFlags=%CompileFlags% -Od -RTC1
goto compile

:ReleaseFlags
REM opt:icf,          COMDAT folding for debugging release build
REM DEBUG:[FULL|NONE] enforce debugging for release build
set CompileFlags=%CompileFlags% -O2 opt:icf

REM ////////////////////////////////////////////////////////////////////////////
REM Compile
REM ////////////////////////////////////////////////////////////////////////////
:compile
cl %CompileFlags% ..\src\UnityBuild.cpp /link %LinkLibraries% %LinkFlags% /out:%ProjectName%.exe

popd
set LastError=%ERRORLEVEL%
if %HaveCTimeBinary%==1 ctime -end %ProjectName%.ctm %LastError%

:end
exit /b %LastError%
