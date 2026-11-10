@echo off
rem Onslaught CE network test: join (player 2). Usage: join.cmd HOST-ADDRESS [password]
rem Uses this folder's own user folder (user\), never your normal Onslaught CE careers, and never checks for updates.
setlocal
if "%PORT%"=="" set PORT=27652
set "HERE=%~dp0"
set "HOST=%~1"
if "%HOST%"=="" set /p "HOST=Host's address (for example 192.168.1.20): "
set "PASS=%~2"
if "%PASS%"=="" set /p "PASS=Shared password (the same as the host's): "
if "%PASS%"=="" (echo A password is needed.& pause & exit /b 1)
call :secret
if not exist "%HERE%user" mkdir "%HERE%user"
echo Joining %HOST% on UDP port %PORT%. Log: %HERE%user\onslaught-ce.log
set "BEA_USER_DIR=%HERE%user"
set "BEA_NET_JOIN=%HOST%:%PORT%"
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
