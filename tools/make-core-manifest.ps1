<#
.SYNOPSIS
Writes the manifest.txt a Toaster core server needs.

.DESCRIPTION
Toaster loads cores as native code, so it checks every download against a
manifest of SHA-256 checksums and refuses to install one that does not match.
The server needs a manifest.txt beside the cores; without it, nothing installs.

Generating it by hand is easy to get wrong. `sha256sum` defaults to binary
mode on Windows and writes "<hash> *name"; the asterisk then reads as part of
the filename, so the manifest parses but matches nothing and the install fails
with "manifest.txt does not list ...". This script always writes the plain
form, with LF endings and no byte-order mark.

Regenerate it whenever a core changes, or the old checksums will reject the
new files.

.PARAMETER Path
Folder holding the core DLLs. Defaults to the current directory.

.PARAMETER Filter
Which files to include. Defaults to *.dll.

.EXAMPLE
pwsh tools/make-core-manifest.ps1 -Path C:\cores

.EXAMPLE
# Check what a running server is serving, without trusting a local copy.
pwsh tools/make-core-manifest.ps1 -Url https://example.test/cores
#>
param(
    [string]$Path = ".",
    [string]$Filter = "*.dll",
    [string]$Url,
    [string]$OutFile = "manifest.txt"
)

$ErrorActionPreference = "Stop"

# Toaster's own list. Anything else on the server is ignored by the installer.
$coreNames = @(
    "swanstation_libretro.dll",
    "ppsspp_libretro.dll",
    "pcsx2_libretro.dll"
)

$lines = [System.Collections.Generic.List[string]]::new()

if ($Url) {
    # Hashing what the server actually serves catches the case where the local
    # copy and the published one have drifted apart.
    $base = $Url.TrimEnd('/')
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("toaster-cores-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $temp | Out-Null
    try {
        foreach ($name in $coreNames) {
            $target = Join-Path $temp $name
            Write-Host "fetching $base/$name"
            try {
                Invoke-WebRequest -Uri "$base/$name" -OutFile $target -UseBasicParsing
            } catch {
                Write-Warning "  skipped: $($_.Exception.Message)"
                continue
            }
            $hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLower()
            $lines.Add("$hash  $name")
        }
    } finally {
        Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    $files = Get-ChildItem -Path $Path -Filter $Filter -File | Sort-Object Name
    if ($files.Count -eq 0) {
        throw "no files matching $Filter in $Path"
    }
    foreach ($file in $files) {
        $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLower()
        $lines.Add("$hash  $($file.Name)")
        Write-Host "$hash  $($file.Name)"
    }
}

if ($lines.Count -eq 0) {
    throw "nothing was hashed, so no manifest was written"
}

# LF endings and no BOM: the parser copes with CRLF, but other tooling may not.
$text = ($lines -join "`n") + "`n"
[System.IO.File]::WriteAllText(
    (Join-Path (Resolve-Path $Path) $OutFile),
    $text,
    (New-Object System.Text.UTF8Encoding $false))

Write-Host ""
Write-Host "wrote $OutFile with $($lines.Count) entr$(if ($lines.Count -eq 1) {'y'} else {'ies'})"
Write-Host "upload it beside the cores, so it resolves at <core-server>/manifest.txt"

$missing = $coreNames | Where-Object { $text -notmatch [regex]::Escape($_) }
if ($missing) {
    Write-Warning "not in the manifest: $($missing -join ', ')"
    Write-Warning "Toaster will refuse to install those."
}
