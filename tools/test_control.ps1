$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskGcc = (Get-Command gcc -ErrorAction Stop).Source
$taskInclude = Join-Path $taskRoot 'Car\Core\Inc'
$taskControl = Join-Path $taskRoot 'Car\Core\Src\car_control.c'
$taskControlTest = Join-Path $taskRoot 'tests\car_control_test.c'
$taskTestExe = Join-Path $PSScriptRoot 'car_control_test.exe'
$taskResults = @()
foreach ($taskUltra in @(0, 1)) {
    & $taskGcc -std=c99 -Wall -Wextra -Werror -pedantic -DCAR_POWERON_STANDBY=0 -DCAR_RACE_MARKERS_ENABLED=1 "-DCAR_ULTRASONIC_ENABLED=$taskUltra" -I $taskInclude $taskControlTest $taskControl -o $taskTestExe
    if ($LASTEXITCODE -ne 0) { throw 'Controller test compilation failed' }
    $taskResults += & $taskTestExe
    if ($LASTEXITCODE -ne 0) { throw 'Controller test failed' }
}
$taskFakeHal = Join-Path $taskRoot 'tests\fake_hal'
$taskApp = Join-Path $taskRoot 'Car\Core\Src\car_app.c'
$taskAppTest = Join-Path $taskRoot 'tests\car_app_test.c'
$taskAppExe = Join-Path $PSScriptRoot 'car_app_test.exe'
& $taskGcc -std=c99 -Wall -Wextra -Werror -pedantic -DCAR_POWERON_STANDBY=0 -DCAR_RACE_MARKERS_ENABLED=1 -DCAR_ULTRASONIC_ENABLED=1 -I $taskFakeHal -I $taskInclude $taskAppTest $taskApp $taskControl -o $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Firmware adapter test compilation failed' }
$taskResults += & $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Firmware adapter test failed' }
& $taskGcc -std=c99 -Wall -Wextra -Werror -pedantic -DCAR_POWERON_STANDBY=0 -DCAR_RACE_MARKERS_ENABLED=1 -DCAR_ULTRASONIC_ENABLED=0 -I $taskFakeHal -I $taskInclude $taskAppTest $taskApp $taskControl -o $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Line-only test compilation failed' }
$taskResults += & $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Line-only test failed' }
& $taskGcc -std=c99 -Wall -Wextra -Werror -pedantic -DCAR_POWERON_STANDBY=0 -DCAR_ULTRASONIC_ENABLED=0 -I $taskFakeHal -I $taskInclude $taskAppTest $taskApp $taskControl -o $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Marker-disabled test compilation failed' }
$taskResults += & $taskAppExe
if ($LASTEXITCODE -ne 0) { throw 'Marker-disabled test failed' }
foreach ($taskMotorMode in @(1, 2, 3)) {
    & $taskGcc -std=c99 -Wall -Wextra -Werror -pedantic -DCAR_POWERON_STANDBY=0 -DCAR_RACE_MARKERS_ENABLED=1 "-DCAR_MOTOR_TEST_MODE=$taskMotorMode" -I $taskFakeHal -I $taskInclude $taskAppTest $taskApp $taskControl -o $taskAppExe
    if ($LASTEXITCODE -ne 0) { throw 'Motor bench test compilation failed' }
    $taskResults += & $taskAppExe
    if ($LASTEXITCODE -ne 0) { throw 'Motor bench test failed' }
}
$taskResults | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'control_tests.log') -Encoding UTF8
$taskResults | Write-Output
