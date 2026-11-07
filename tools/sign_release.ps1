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
# Once, before the first release:
#   minisign -G -p minisign.pub -s %USERPROFILE%\.minisign\minisign.key
#   build with -DAPP_UPDATE_PUBKEY="RWQ…(2nd line of minisign.pub)"
# minisign.exe: https://github.com/jedisct1/minisign/releases (or: winget install jedisct1.minisign)
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [string]$Version = "release",
    [string]$Key = "$env:USERPROFILE\.minisign\minisign.key",
    [string]$CertThumbprint = ""
)
$ErrorActionPreference = "Stop"
Set-Location $Folder

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
& minisign -S -s $Key -m SHA256SUMS -t "ShellWarden $Version $(Get-Date -Format yyyy-MM-dd)"
if ($LASTEXITCODE -ne 0) { throw "minisign failed" }
Get-Content SHA256SUMS
Write-Host "Upload to the release: all files + SHA256SUMS + SHA256SUMS.minisig"
