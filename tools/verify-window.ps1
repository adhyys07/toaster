<#
.SYNOPSIS
Launches a built Toaster executable and checks that it actually draws a frame.

.DESCRIPTION
The test suite cannot catch a blank UI: --help returns before any window is
created, and nothing renders a frame. v0.2.0 shipped with the library menu
drawing its whole frame and never calling SDL_RenderPresent, so the window was
black, and every build and test still passed.

This captures the application's own window and counts distinct colours in it.
A window that was never presented is one flat colour; a drawn UI has dozens.

With -CleanPath the executable runs with only the Windows directories on PATH,
which also proves the tree carries every DLL it needs -- Windows resolves
imports before main() runs, so a missing DLL means no window at all.

.EXAMPLE
pwsh tools/verify-window.ps1 -Exe build/run/emu.exe -CleanPath
#>
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$TimeoutSeconds = 20,
    [int]$MinimumColors = 10,
    [switch]$CleanPath,
    [string]$SavePng
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ToasterWin {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out ClientRect r);
    [StructLayout(LayoutKind.Sequential)] public struct ClientRect { public int Left, Top, Right, Bottom; }
}
"@

$exePath = (Resolve-Path $Exe).Path
$workDir = Split-Path $exePath -Parent
Write-Host "verifying $exePath"

$savedPath = $env:PATH
$process = $null
try {
    if ($CleanPath) {
        # What Explorer hands a double-clicked executable: no toolchain.
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        Write-Host "  PATH reduced to the Windows directories"
    }

    $process = Start-Process -FilePath $exePath -WorkingDirectory $workDir -PassThru

    $handle = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) {
            throw "the process exited with code $($process.ExitCode) before showing a window"
        }
        if ($process.MainWindowHandle -ne 0) {
            $handle = $process.MainWindowHandle
            break
        }
    }
    if ($handle -eq [IntPtr]::Zero) {
        throw "no window appeared within $TimeoutSeconds s (a missing DLL shows a modal error instead)"
    }
    Write-Host "  window: '$($process.MainWindowTitle)'"

    # Let the first frame land before sampling.
    Start-Sleep -Seconds 2

    $rect = New-Object ToasterWin+ClientRect
    [void][ToasterWin]::GetClientRect($handle, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { throw "the window has no client area" }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    # Flag 1 is PW_CLIENTONLY: the frame, without the title bar.
    [void][ToasterWin]::PrintWindow($handle, $dc, 1)
    $graphics.ReleaseHdc($dc)

    $seen = @{}
    for ($x = 0; $x -lt $width; $x += 10) {
        for ($y = 0; $y -lt $height; $y += 10) {
            $c = $bitmap.GetPixel($x, $y)
            $seen["{0},{1},{2}" -f $c.R, $c.G, $c.B] = $true
        }
    }
    $distinct = $seen.Keys.Count

    if ($SavePng) {
        $bitmap.Save((New-Item -ItemType File -Path $SavePng -Force).FullName)
        Write-Host "  saved $SavePng"
    }
    $graphics.Dispose()
    $bitmap.Dispose()

    Write-Host "  client area: ${width}x${height}, distinct colours: $distinct"
    if ($distinct -lt $MinimumColors) {
        throw "the window drew only $distinct distinct colour(s); it is blank, which means the frame was never presented"
    }

    Write-Host "OK: the window renders."
}
finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    $env:PATH = $savedPath
}
