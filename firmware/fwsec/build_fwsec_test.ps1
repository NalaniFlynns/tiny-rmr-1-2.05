# build_fwsec_test.ps1 - compile fwsec CLI self-test
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$gcc = 'C:\Qt\Tools\mingw1310_64\bin\gcc.exe'
$gxx = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
Set-Location $root
# compile mlkem C sources
& $gcc -O2 -c (Get-ChildItem mlkem\*.c | ForEach-Object { $_.FullName })
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objs = @(Get-ChildItem -Filter '*.o' | ForEach-Object { $_.FullName })
$args = @('fwsec_test.cpp','fwsec_sha256.cpp','fwsec_blake2b.cpp','fwsec_aead.cpp','fwsec_argon2.cpp') + $objs + @('-I.', '-lbcrypt')
& $gxx -std=c++17 -O2 -o fwsec_test.exe @args
exit $LASTEXITCODE
