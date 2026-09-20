param(
    [string]$Vita3KPath = 'D:\Vita3k\Vita3K.exe',
    [ValidateSet('classic-v1', 'classic-v2', 'xtreme')][string]$Model = 'classic-v1',
    [ValidateRange(1,3600)][int]$Seconds = 45,
    [string]$ConfigPath,
    [switch]$MenuTrace
)
$ErrorActionPreference = 'Stop'
if (Get-Process Vita3K -ErrorAction SilentlyContinue) {
    throw 'Close the existing Vita3K instance before an isolated validation run.'
}
# Caller must first back up/stage the application and model data. This starts
# the ordinary ARM executable through documented app arguments, not synthetic
# background key messages. The app exits normally and flushes its diagnostics.
$arguments = '-r VCYB00001 -Z "--boot-model, ' + $Model + ', --run-seconds, ' + $Seconds + '"'
if ($MenuTrace) {
    if (-not $ConfigPath) { throw 'Menu traces require an explicit isolated configuration.' }
    $arguments = '-r VCYB00001 -Z "--boot-model, ' + $Model + ', --run-seconds, ' + $Seconds + ', --validate-menu"'
}
$workingDirectory = Split-Path $Vita3KPath
if ($ConfigPath) {
    $ConfigPath = (Resolve-Path -LiteralPath $ConfigPath).Path
    if ($ConfigPath.Contains('"')) { throw 'Invalid configuration path' }
    $arguments = '-c "' + $ConfigPath + '" ' + $arguments
    $workingDirectory = Split-Path $ConfigPath
}
$process = Start-Process -FilePath $Vita3KPath -WorkingDirectory $workingDirectory -ArgumentList $arguments -PassThru
$process | Select-Object Id, StartTime
