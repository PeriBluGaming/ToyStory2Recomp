# -DPSX_LAUNCHER=OFF
# -DPSX_RECOMP_UI=ON

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
cmake -S $Root -B (Join-Path $Root 'build') -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPSX_RECOMP_UI=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build (Join-Path $Root 'build') --config Debug --parallel
exit $LASTEXITCODE
