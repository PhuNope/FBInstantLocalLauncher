param(
    [Parameter(Mandatory=$true)][string]$ZipPath,
    [Parameter(Mandatory=$true)][string]$TargetDir,
    [Parameter(Mandatory=$true)][string]$ExeName,
    [Parameter(Mandatory=$true)][int]$WaitPid
)
$ErrorActionPreference = 'Stop'
$stage = Join-Path $env:TEMP ("FBInstantLocalLauncher-stage-" + [guid]::NewGuid().ToString('N'))
try {
    if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) { throw 'Downloaded ZIP not found.' }
    if ($ExeName -ne 'FBInstantLocalLauncher.exe') { throw 'Unexpected executable name.' }
    if (-not (Test-Path -LiteralPath $TargetDir -PathType Container)) { throw 'Install directory not found.' }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    # Validate archive paths before extracting to avoid directory traversal.
    Add-Type -AssemblyName System.IO.Compression
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            $relative = $entry.FullName.Replace('/', '\')
            if ($relative -match '(^[\\/]|^[A-Za-z]:|(^|[\\/])\.\.([\\/]|$))') { throw "Unsafe ZIP path: $relative" }
        }
    } finally { $archive.Dispose() }
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $stage -Force
    if (-not (Test-Path -LiteralPath (Join-Path $stage $ExeName) -PathType Leaf)) { throw 'Update archive has no expected executable.' }
    $old = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
    if ($old) { $old | Wait-Process -Timeout 40 -ErrorAction Stop }
    # App data lives in AppData, outside TargetDir. Do not delete TargetDir.
    Copy-Item -Path (Join-Path $stage '*') -Destination $TargetDir -Recurse -Force -ErrorAction Stop
    Remove-Item -LiteralPath $ZipPath -Force -ErrorAction SilentlyContinue
    Start-Process -FilePath (Join-Path $TargetDir $ExeName) -WorkingDirectory $TargetDir
} catch {
    $message = $_.Exception.Message
    Write-Error "Update failed: $message"
    Add-Type -AssemblyName PresentationFramework -ErrorAction SilentlyContinue
    try { [System.Windows.MessageBox]::Show("Update failed: $message", 'FB Instant Launcher') | Out-Null } catch {}
    exit 1
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
