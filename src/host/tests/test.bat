rem test primitives
echo off
forth < primitives.f
if %errorlevel% equ 0 (echo PASSED) else (echo FAILED with error code %errorlevel%)
