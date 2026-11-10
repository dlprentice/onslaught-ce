@echo off
rem Onslaught CE network test: host (player 1). Usage: host.cmd [password]
rem Uses this folder's own user folder (user\), never your normal Onslaught CE careers, and never checks for updates.
setlocal
if "%PORT%"=="" set PORT=27652
set "HERE=%~dp0"
set "PASS=%~1"
if "%PASS%"=="" set /p "PASS=Shared password (letters and digits; the other player types the same): "
if "%PASS%"=="" (echo A password is needed.& pause & exit /b 1)
call :secret
if not exist "%HERE%user" mkdir "%HERE%user"
echo Hosting on UDP port %PORT%. Tell the other player one of these addresses:
ipconfig | findstr /c:"IPv4"
echo   (over the internet: your public address, with UDP port %PORT% forwarded to this computer)
echo The other player must start join within 30 seconds of the level loading here.
echo Log: %HERE%user\onslaught-ce.log
set "BEA_USER_DIR=%HERE%user"
set "BEA_NET_HOST=0.0.0.0:%PORT%"
set "BEA_NET_SECRET=%SECRET%"
set BEA_NO_UPDATE_CHECK=1
"%HERE%onslaught-ce.exe" --no-update-check -level 852 -skipfmv
exit /b
:secret
rem The password's SHA-256, as the Linux scripts compute it (sha256sum of the same bytes, no newline).
<nul set /p "=%PASS%" > "%TEMP%\ce-nettest-pass.txt"
set "SECRET="
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%TEMP%\ce-nettest-pass.txt" SHA256') do if not defined SECRET set "SECRET=%%h"
del "%TEMP%\ce-nettest-pass.txt"
set "SECRET=%SECRET: =%"
exit /b
