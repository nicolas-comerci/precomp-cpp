@echo off
%~d0
cd "%~dp0"
if [%1]==[] goto :eof
:loop
:: CHANGE YOUR SETTINGS HERE!
precomp.exe -e -intense -brute -comfort %1
shift
if not [%1]==[] goto loop
pause