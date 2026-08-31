@echo off
REM VS install path differs per PC (2022 / 2026, Community / Professional).
REM Hardcoding one path made this script silently fail on other machines,
REM leaving a stale Server.exe behind. Try the known paths in order.
setlocal
set "VSDEVCMD="
for %%P in (
  "C:\Program Files\Microsoft Visual Studio\18\Community"
  "C:\Program Files\Microsoft Visual Studio\18\Professional"
  "C:\Program Files\Microsoft Visual Studio\2022\Community"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
) do if not defined VSDEVCMD if exist "%%~P\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%~P\Common7\Tools\VsDevCmd.bat"

if not defined VSDEVCMD (
  echo [ERROR] Visual Studio C++ toolset not found. Install the "Game development with C++" workload.
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64
cd /d "%~dp0"
if not exist Server_Build mkdir Server_Build

cl /nologo /std:c++17 /EHsc /utf-8 /O2 /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_DQS=0 /DSQLITE_DEFAULT_MEMSTATUS=0 ^
  /Fe:Server_Build\Server.exe /Fo:Server_Build\ ^
  Server\Server.cpp Server\ChatLog.cpp Server\Accounts.cpp Server\Crypto.cpp Server\Rooms.cpp Server\Session.cpp ^
  Server\Friends.cpp Server\DirectMessages.cpp Server\NatPortMapping.cpp Server\UdpRelay.cpp ^
  Shared\Framing.cpp ThirdParty\sqlite\sqlite3.c ^
  /IShared /IServer /IThirdParty\sqlite ^
  ws2_32.lib
if errorlevel 1 (
  echo [ERROR] Build failed. Server.exe was NOT updated.
  exit /b 1
)
echo [OK] Server_Build\Server.exe updated.
