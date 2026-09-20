param([Parameter(Mandatory=$true)][int]$VitaProcessId, [Parameter(Mandatory=$true)][string]$OutputPath, [int[]]$Keys, [int]$Delay=300, [int]$TouchX=-1, [int]$TouchY=-1, [ValidateRange(20,3000)][int]$Hold=180, [switch]$BackgroundCapture, [ValidateRange(1,600)][int]$FrameCount=1, [ValidateRange(1,5000)][int]$Interval=100)
$ErrorActionPreference = 'Stop'
if ($BackgroundCapture -and ($Keys.Count -gt 0 -or $TouchX -ge 0 -or $TouchY -ge 0)) { throw 'Background capture cannot inject input' }
if ($FrameCount -gt 1 -and -not $BackgroundCapture) { throw 'Sequences must use passive background capture' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class VitaWindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct Point { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public delegate bool EnumCallback(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr handle, StringBuilder title, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
}
'@
[void][VitaWindowCapture]::SetProcessDPIAware()
$process = Get-Process -Id $VitaProcessId
$script:gameHandle = [IntPtr]::Zero
[void][VitaWindowCapture]::EnumWindows({ param($windowHandle, $data)
    $windowProcess = [uint32]0
    [void][VitaWindowCapture]::GetWindowThreadProcessId($windowHandle, [ref]$windowProcess)
    if ($windowProcess -eq $VitaProcessId -and [VitaWindowCapture]::IsWindowVisible($windowHandle)) {
        $title = New-Object System.Text.StringBuilder 512
        [void][VitaWindowCapture]::GetWindowText($windowHandle, $title, 512)
        Write-Host "Window: $title"
        if ($title.ToString() -like '*VitaCybiko*') { $script:gameHandle = $windowHandle }
    }
    return $true
}, [IntPtr]::Zero)
$handle = $script:gameHandle
if ($handle -eq 0) { throw 'No Vita3K window handle' }
if (-not $BackgroundCapture) { [void][VitaWindowCapture]::SetForegroundWindow($handle) }
Start-Sleep -Milliseconds 300
if ($Keys.Count -gt 0) {
    if ([VitaWindowCapture]::GetForegroundWindow() -ne $handle) { throw 'Game not foreground' }
    try {
        foreach ($key in $Keys) { $flags=0; if ($key -ge 33 -and $key -le 40) {$flags=1}; [VitaWindowCapture]::keybd_event([byte]$key,[byte][VitaWindowCapture]::MapVirtualKey($key,0),$flags,[UIntPtr]::Zero) }
        Start-Sleep -Milliseconds $Hold
    } finally {
        foreach ($key in $Keys) { $flags=2; if ($key -ge 33 -and $key -le 40) {$flags=3}; [VitaWindowCapture]::keybd_event([byte]$key,[byte][VitaWindowCapture]::MapVirtualKey($key,0),$flags,[UIntPtr]::Zero) }
    }
}
Start-Sleep -Milliseconds $Delay
if (-not $BackgroundCapture -and [VitaWindowCapture]::GetForegroundWindow() -ne $handle) {
    throw 'Game is not foreground; refusing to capture unrelated desktop content'
}
$rect = New-Object VitaWindowCapture+Rect
if (-not [VitaWindowCapture]::GetClientRect($handle, [ref]$rect)) { throw 'No window rectangle' }
$origin = New-Object VitaWindowCapture+Point
if (-not [VitaWindowCapture]::ClientToScreen($handle, [ref]$origin)) { throw 'No client origin' }
if ($TouchX -ge 0 -and $TouchY -ge 0) {
    if ([VitaWindowCapture]::GetForegroundWindow() -ne $handle) { throw 'Game not foreground' }
    [void][VitaWindowCapture]::SetCursorPos($origin.X + $TouchX, $origin.Y + $TouchY)
    try { [VitaWindowCapture]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds $Hold }
    finally { [VitaWindowCapture]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
    Start-Sleep -Milliseconds $Delay
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -lt 100 -or $height -lt 100) { throw 'Window is minimized or too small' }
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $captures = @()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    for ($frame = 0; $frame -lt $FrameCount; ++$frame) {
    $currentTitle = New-Object System.Text.StringBuilder 512
    [void][VitaWindowCapture]::GetWindowText($handle, $currentTitle, 512)
    if ($currentTitle.ToString() -notlike '*VitaCybiko*') { throw 'VitaCybiko capture window closed' }
    $captureStart = $timer.Elapsed.TotalMilliseconds
    if ($BackgroundCapture) {
        $dc = $graphics.GetHdc()
        try {
            if (-not [VitaWindowCapture]::PrintWindow($handle, $dc, 3)) { throw 'Window capture failed' }
        } finally { $graphics.ReleaseHdc($dc) }
    } else {
        if ([VitaWindowCapture]::GetForegroundWindow() -ne $handle) { throw 'Game lost foreground before capture' }
        $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
    }
    $framePath = $OutputPath
    if ($FrameCount -gt 1) {
        $framePath = Join-Path ([IO.Path]::GetDirectoryName($OutputPath)) (([IO.Path]::GetFileNameWithoutExtension($OutputPath)) + ('-{0:D4}.png' -f $frame))
    }
    $bitmap.Save($framePath, [System.Drawing.Imaging.ImageFormat]::Png)
    $captures += [pscustomobject]@{frame=$frame;start_ms=$captureStart;end_ms=$timer.Elapsed.TotalMilliseconds;path=$framePath}
    if ($frame + 1 -lt $FrameCount) { Start-Sleep -Milliseconds $Interval }
    }
    if ($FrameCount -gt 1) { $captures | Export-Csv -NoTypeInformation -Path ($OutputPath + '.csv') }
    $process | Select-Object Id,Responding,MainWindowTitle
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}
