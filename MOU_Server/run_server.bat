@echo off
REM ===========================================================================
REM  Start the MOU login/chat server on this PC.
REM
REM  Why this file exists:
REM    Running Server.exe bare hides *why* remote players cannot connect --
REM    stale exe, wrong public IP, or missing port forward all fail silently.
REM    This checks those three first, then starts the server.
REM
REM  ASCII ONLY. cmd parses .bat using the console codepage, which differs per
REM  machine (949 vs 65001). Non-ASCII text here breaks the script itself, not
REM  just its output. Korean docs: MOU_Server/SERVER_INTEGRATION.md
REM ===========================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "EXE=Server_Build\Server.exe"
set "PORT=9000"
set "RELAY_PORTS=10000-10127"

if not exist "%EXE%" (
  echo [ERROR] %EXE% not found. Run build_server.bat first.
  pause
  exit /b 1
)

REM Verify this is a post-relay build. An old binary can start normally but
REM silently ignores the relay flags, so catch that before opening a room.
"%EXE%" 2>&1 | findstr /C:"--relay-ports" >nul
if errorlevel 1 (
  echo [ERROR] %EXE% is an old build without UDP relay support. Re-run build_server.bat.
  pause
  exit /b 1
)

echo ===================================================================
echo  MOU server - preflight
echo ===================================================================

REM Do NOT escape the pipe as ^^| here. It sits inside double quotes, so cmd
REM already passes it through literally; adding ^ leaks the caret into
REM PowerShell and breaks the command.
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' } | Select-Object -First 1).IPAddress"`) do set "LANIP=%%i"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-NetRoute -DestinationPrefix '0.0.0.0/0' | Sort-Object RouteMetric | Select-Object -First 1).NextHop"`) do set "GWIP=%%i"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "try { (Invoke-RestMethod -Uri 'https://api.ipify.org?format=json' -TimeoutSec 10).ip } catch { '' }"`) do set "PUBIP=%%i"

echo   LAN IP    : !LANIP!
echo   Gateway   : !GWIP!
echo   Public IP : !PUBIP!
echo.

REM Compare the shared config against the real public IP. Home lines get a new
REM public IP on router reboot, so without this check you get a silent
REM "it worked yesterday" failure.
set "INI=..\TeamProject_MOU\Config\DefaultGame.ini"
set "CFGHOST="
if exist "%INI%" (
  for /f "usebackq tokens=2 delims==" %%i in (`findstr /B /C:"ServerHost=" "%INI%"`) do set "CFGHOST=%%i"
)

if defined CFGHOST (
  echo   DefaultGame.ini ServerHost = !CFGHOST!
  if /I "!CFGHOST!"=="!PUBIP!" (
    echo     OK - remote teammates should connect to !CFGHOST!:%PORT%
  ) else (
    echo.
    echo   [WARN] Shared config does not match this PC's public IP.
    echo          Remote teammates will NOT connect.
    echo          Set ServerHost=!PUBIP! in DefaultGame.ini and commit it.
  )
)

echo.
echo   [REQUIRED] Port forward on the router at !GWIP! :
echo                external TCP+UDP %PORT%  -^>  !LANIP!:%PORT%
echo                external UDP %RELAY_PORTS%  -^>  !LANIP!:%RELAY_PORTS%
echo.
echo   [ALSO] If YOU host a game room from this PC, the listen server port
echo          must be forwarded too, or remote players hang on "traveling to":
echo                external UDP 7777  -^>  !LANIP!:7777
echo.
echo   [NOTE] If PIE on THIS PC cannot reach the public IP (no NAT hairpin),
echo          run in the Unreal console:  MOU.Chat.SetServer 127.0.0.1 %PORT%
echo ===================================================================
echo.

REM Extra args pass through, e.g. run_server.bat --upnp.
REM Use --upnp only if the router supports it; this router answered
REM NoGatewayFound, so a manual port forward is required here.
REM
REM --public-ip makes the server record the PUBLIC address for rooms hosted
REM from inside this LAN. Without it the room stores a private address (or the
REM gateway IP when the host hairpins in), and remote players hang forever on
REM "traveling to <private ip>:7777".
REM --relay is on by default. Direct UE connection and hole punching still run
REM first; relay is used only when those paths fail. --relay-lan-ip gives
REM players on this server's LAN a private relay address and avoids WAN hairpin.
if defined PUBIP (
  if defined LANIP (
    "%EXE%" %PORT% Server_Build\chat_log.db --public-ip !PUBIP! --relay --relay-lan-ip !LANIP! --relay-ports %RELAY_PORTS% %*
  ) else (
    echo [WARN] LAN IP lookup failed; relay LAN hairpin avoidance is disabled.
    "%EXE%" %PORT% Server_Build\chat_log.db --public-ip !PUBIP! --relay --relay-ports %RELAY_PORTS% %*
  )
) else (
  echo [WARN] Public IP lookup failed; starting without --public-ip.
  echo        Relay will remain disabled unless you pass --relay-public-ip manually.
  "%EXE%" %PORT% Server_Build\chat_log.db --relay --relay-ports %RELAY_PORTS% %*
)

echo.
echo [STOPPED] Server exited.
pause
