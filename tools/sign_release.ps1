# Signs a ShellWarden release on Windows so the in-app updater accepts it.
#
#   powershell -ExecutionPolicy Bypass -File tools\sign_release.ps1 -Folder dist\release [-Version 0.4.0]
#        [-CertThumbprint <code-signing certificate>]
#
# 1. (optional) Authenticode: with -CertThumbprint every .exe is signed with
#    signtool (Windows SDK) and a timestamp, so Windows shows the publisher
#    name instead of "Unknown publisher". Needs a code-signing certificate
#    (open source projects can get one for free from the SignPath Foundation).
# 2. SHA256SUMS + SHA256SUMS.minisig with minisign (the updater checks this).
#
# The secret key is looked up in this order: -Key, %MINISIGN_KEY%,
# Desktop\minisign-keys\minisign.key, %USERPROFILE%\.minisign\minisign.key.
# The matching public key is minisign.pub in the project root; builds embed it,
# so the app installs only updates signed with this key.
# minisign.exe: https://github.com/jedisct1/minisign/releases (or: winget install jedisct1.minisign)
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [string]$Version = "release",
    [string]$Key = "",
    [string]$CertThumbprint = ""
)
$ErrorActionPreference = "Stop"
if ($Key -eq "") {
    $candidates = @($env:MINISIGN_KEY,
                    (Join-Path ([Environment]::GetFolderPath("Desktop")) "minisign-keys\minisign.key"),
                    "$env:USERPROFILE\.minisign\minisign.key") | Where-Object { $_ }
    $Key = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Key) { throw "No minisign secret key found (looked in: $($candidates -join ', '))" }
}
$Key = (Resolve-Path $Key).Path
# minisign: PATH, the winget alias folder, or the winget package folder
$minisign = (Get-Command minisign -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $minisign) {
    $minisign = @("$env:LOCALAPPDATA\Microsoft\WinGet\Links\minisign.exe") +
                @(Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\jedisct1.minisign*" -Recurse -Filter minisign.exe -ErrorAction SilentlyContinue |
                  ForEach-Object { $_.FullName }) |
                Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $minisign) { throw "minisign is not installed (winget install jedisct1.minisign)" }
Set-Location $Folder
Write-Host "Signing with $Key (minisign asks for the key password)"

if ($CertThumbprint -ne "") {
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signtool) { throw "signtool.exe not found (install the Windows SDK)" }
    Get-ChildItem *.exe | ForEach-Object {
        & $signtool.FullName sign /sha1 $CertThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "signtool failed on $($_.Name)" }
    }
}

if (-not (Test-Path $Key)) { throw "No minisign secret key at $Key" }
Remove-Item SHA256SUMS, SHA256SUMS.minisig -ErrorAction SilentlyContinue
$lines = Get-ChildItem -File | Where-Object { $_.Name -notin @("SHA256SUMS", "SHA256SUMS.minisig") } | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
}
# LF line endings, no BOM: the same bytes on every system
[System.IO.File]::WriteAllText((Join-Path (Get-Location) "SHA256SUMS"), (($lines -join "`n") + "`n"))
& $minisign -S -s $Key -m SHA256SUMS -t "ShellWarden $Version $(Get-Date -Format yyyy-MM-dd)"
if ($LASTEXITCODE -ne 0) { throw "minisign failed" }
Get-Content SHA256SUMS
Write-Host "Upload to the release: all files + SHA256SUMS + SHA256SUMS.minisig"
