# VaultBox Installer v0.1.0
# Installs the VaultBox offline password manager browser extension

param(
    [switch]$Uninstall,
    [string]$InstallDir = "$env:LOCALAPPDATA\VaultBox"
)

# --- Auto-elevate ---
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName PresentationFramework

$releaseUrl = "https://github.com/SysAdminDoc/VaultBox/releases/latest/download"

function Write-Header {
    Clear-Host
    Write-Host ""
    Write-Host "  ======================================" -ForegroundColor Cyan
    Write-Host "    VaultBox - Offline Password Manager" -ForegroundColor White
    Write-Host "    Installer v0.1.0" -ForegroundColor DarkGray
    Write-Host "  ======================================" -ForegroundColor Cyan
    Write-Host ""
}

function Get-ChromePath {
    $paths = @(
        "${env:ProgramFiles}\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
        "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) { return $p }
    }
    try {
        $regPath = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe" -ErrorAction SilentlyContinue
        if ($regPath -and (Test-Path $regPath.'(default)')) { return $regPath.'(default)' }
    } catch {}
    return $null
}

function Get-BrowserPath {
    param([string]$Browser)

    switch ($Browser) {
        "Chrome" { return Get-ChromePath }
        "Edge" {
            $paths = @(
                "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
                "${env:ProgramFiles}\Microsoft\Edge\Application\msedge.exe",
                "$env:LOCALAPPDATA\Microsoft\Edge\Application\msedge.exe"
            )
            foreach ($p in $paths) { if (Test-Path $p) { return $p } }
        }
        "Brave" {
            $paths = @(
                "$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\Application\brave.exe",
                "${env:ProgramFiles}\BraveSoftware\Brave-Browser\Application\brave.exe"
            )
            foreach ($p in $paths) { if (Test-Path $p) { return $p } }
        }
        "Firefox" {
            $paths = @(
                "${env:ProgramFiles}\Mozilla Firefox\firefox.exe",
                "${env:ProgramFiles(x86)}\Mozilla Firefox\firefox.exe"
            )
            foreach ($p in $paths) { if (Test-Path $p) { return $p } }
        }
    }
    return $null
}

function Show-BrowserMenu {
    Write-Host "  Select your browser:" -ForegroundColor White
    Write-Host ""

    # Detect installed browsers
    $browsers = @()
    $index = 1

    $chromeExe = Get-BrowserPath "Chrome"
    if ($chromeExe) {
        $browsers += @{ Name = "Google Chrome"; Key = "Chrome"; Exe = $chromeExe; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "chrome://extensions" }
        Write-Host "    [$index] Google Chrome" -ForegroundColor Green -NoNewline
        Write-Host " (detected)" -ForegroundColor DarkGreen
    } else {
        $browsers += @{ Name = "Google Chrome"; Key = "Chrome"; Exe = $null; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "chrome://extensions" }
        Write-Host "    [$index] Google Chrome" -ForegroundColor Gray
    }
    $index++

    $edgeExe = Get-BrowserPath "Edge"
    if ($edgeExe) {
        $browsers += @{ Name = "Microsoft Edge"; Key = "Edge"; Exe = $edgeExe; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "edge://extensions" }
        Write-Host "    [$index] Microsoft Edge" -ForegroundColor Green -NoNewline
        Write-Host " (detected)" -ForegroundColor DarkGreen
    } else {
        $browsers += @{ Name = "Microsoft Edge"; Key = "Edge"; Exe = $null; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "edge://extensions" }
        Write-Host "    [$index] Microsoft Edge" -ForegroundColor Gray
    }
    $index++

    $braveExe = Get-BrowserPath "Brave"
    if ($braveExe) {
        $browsers += @{ Name = "Brave"; Key = "Brave"; Exe = $braveExe; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "brave://extensions" }
        Write-Host "    [$index] Brave" -ForegroundColor Green -NoNewline
        Write-Host " (detected)" -ForegroundColor DarkGreen
    } else {
        $browsers += @{ Name = "Brave"; Key = "Brave"; Exe = $null; Zip = "VaultBox-v0.1.0-chrome.zip"; ExtPage = "brave://extensions" }
        Write-Host "    [$index] Brave" -ForegroundColor Gray
    }
    $index++

    $firefoxExe = Get-BrowserPath "Firefox"
    if ($firefoxExe) {
        $browsers += @{ Name = "Firefox"; Key = "Firefox"; Exe = $firefoxExe; Zip = "VaultBox-v0.1.0-firefox.zip"; ExtPage = "about:debugging#/runtime/this-firefox" }
        Write-Host "    [$index] Firefox" -ForegroundColor Green -NoNewline
        Write-Host " (detected)" -ForegroundColor DarkGreen
    } else {
        $browsers += @{ Name = "Firefox"; Key = "Firefox"; Exe = $null; Zip = "VaultBox-v0.1.0-firefox.zip"; ExtPage = "about:debugging#/runtime/this-firefox" }
        Write-Host "    [$index] Firefox" -ForegroundColor Gray
    }

    Write-Host ""
    $choice = Read-Host "  Enter choice (1-$($browsers.Count))"

    $selected = [int]$choice - 1
    if ($selected -lt 0 -or $selected -ge $browsers.Count) {
        Write-Host "  [!] Invalid selection." -ForegroundColor Red
        return $null
    }

    return $browsers[$selected]
}

function Install-VaultBox {
    Write-Header

    $browser = Show-BrowserMenu
    if (-not $browser) {
        Read-Host "  Press Enter to exit"
        return
    }

    $browserName = $browser.Name
    $browserExe = $browser.Exe
    $zipName = $browser.Zip
    $extPage = $browser.ExtPage
    $isFirefox = $browser.Key -eq "Firefox"

    Write-Host ""
    Write-Host "  [*] Installing for $browserName..." -ForegroundColor Yellow

    $scriptDir = Split-Path -Parent $PSCommandPath
    $buildDir = Join-Path $scriptDir "apps\browser\build"

    # Priority 1: Local build output (repo clone)
    if (Test-Path (Join-Path $buildDir "manifest.json")) {
        Write-Host "  [*] Found local build output." -ForegroundColor Yellow
        Write-Host "  [*] Copying extension to $InstallDir ..." -ForegroundColor Yellow

        if (Test-Path $InstallDir) { Remove-Item -Path $InstallDir -Recurse -Force }
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
        Copy-Item -Path "$buildDir\*" -Destination $InstallDir -Recurse -Force
    }
    # Priority 2: Matching zip file next to installer
    elseif (Test-Path (Join-Path $scriptDir $zipName)) {
        $localZip = Join-Path $scriptDir $zipName
        Write-Host "  [*] Found local package: $zipName" -ForegroundColor Yellow
        Write-Host "  [*] Extracting to $InstallDir ..." -ForegroundColor Yellow

        if (Test-Path $InstallDir) { Remove-Item -Path $InstallDir -Recurse -Force }
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
        Expand-Archive -Path $localZip -DestinationPath $InstallDir -Force
    }
    # Priority 3: Download from GitHub
    else {
        Write-Host "  [*] Downloading $zipName from GitHub..." -ForegroundColor Yellow
        $zipPath = Join-Path $env:TEMP $zipName

        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri "$releaseUrl/$zipName" -OutFile $zipPath -UseBasicParsing
            Write-Host "  [OK] Downloaded ($([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB)" -ForegroundColor Green
        } catch {
            Write-Host "  [!] Download failed: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host ""
            Write-Host "  Manual download:" -ForegroundColor Yellow
            Write-Host "    $releaseUrl/$zipName" -ForegroundColor Gray
            Write-Host "  Place the zip next to this script and run again." -ForegroundColor Gray
            Write-Host ""
            Read-Host "  Press Enter to exit"
            return
        }

        Write-Host "  [*] Extracting to $InstallDir ..." -ForegroundColor Yellow
        if (Test-Path $InstallDir) { Remove-Item -Path $InstallDir -Recurse -Force }
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
        Expand-Archive -Path $zipPath -DestinationPath $InstallDir -Force
        Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
    }

    # Handle nested zip extraction
    $manifestPath = Join-Path $InstallDir "manifest.json"
    if (-not (Test-Path $manifestPath)) {
        $nested = Get-ChildItem -Path $InstallDir -Filter "manifest.json" -Recurse | Select-Object -First 1
        if ($nested) {
            $nestedDir = Split-Path -Parent $nested.FullName
            Get-ChildItem -Path $nestedDir | Move-Item -Destination $InstallDir -Force
        }
    }

    if (-not (Test-Path $manifestPath)) {
        Write-Host "  [!] manifest.json not found after install. Something went wrong." -ForegroundColor Red
        Read-Host "  Press Enter to exit"
        return
    }

    Write-Host "  [OK] Extension files installed." -ForegroundColor Green

    # Create uninstaller
    $uninstallScript = @"
Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`" -Uninstall" -Verb RunAs
"@
    Set-Content -Path (Join-Path $InstallDir "Uninstall-VaultBox.ps1") -Value $uninstallScript -Force

    # Create shortcuts (Chromium browsers only - Firefox doesn't support --load-extension)
    if ($browserExe -and -not $isFirefox) {
        $shell = New-Object -ComObject WScript.Shell

        # Desktop shortcut
        $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath("Desktop")) "VaultBox.lnk"))
        $shortcut.TargetPath = $browserExe
        $shortcut.Arguments = "--load-extension=`"$InstallDir`""
        $shortcut.Description = "Launch $browserName with VaultBox extension"
        $shortcut.IconLocation = "$browserExe,0"
        $shortcut.Save()
        Write-Host "  [OK] Desktop shortcut created." -ForegroundColor Green

        # Start Menu shortcut
        $sc2 = $shell.CreateShortcut((Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\VaultBox.lnk"))
        $sc2.TargetPath = $browserExe
        $sc2.Arguments = "--load-extension=`"$InstallDir`""
        $sc2.Description = "Launch $browserName with VaultBox extension"
        $sc2.IconLocation = "$browserExe,0"
        $sc2.Save()
        Write-Host "  [OK] Start Menu shortcut created." -ForegroundColor Green
    }

    Write-Host ""
    Write-Host "  ======================================" -ForegroundColor Green
    Write-Host "    Installation Complete!" -ForegroundColor White
    Write-Host "  ======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Extension installed to:" -ForegroundColor Gray
    Write-Host "    $InstallDir" -ForegroundColor White
    Write-Host ""

    if ($isFirefox) {
        Write-Host "  TO LOAD IN FIREFOX:" -ForegroundColor Yellow
        Write-Host "    1. Open Firefox -> about:debugging#/runtime/this-firefox" -ForegroundColor Gray
        Write-Host "    2. Click 'Load Temporary Add-on...'" -ForegroundColor Gray
        Write-Host "    3. Select any file in: $InstallDir" -ForegroundColor White
        Write-Host ""
        Write-Host "  Note: Firefox requires reloading the extension each session" -ForegroundColor DarkGray
        Write-Host "  until Mozilla signs the extension for permanent install." -ForegroundColor DarkGray
    } else {
        Write-Host "  TO LOAD IN $($browserName.ToUpper()):" -ForegroundColor Yellow
        Write-Host "    1. Open $browserName -> $extPage" -ForegroundColor Gray
        Write-Host "    2. Enable 'Developer mode' (top right)" -ForegroundColor Gray
        Write-Host "    3. Click 'Load unpacked'" -ForegroundColor Gray
        Write-Host "    4. Select: $InstallDir" -ForegroundColor White
        Write-Host ""
        Write-Host "  OR use the desktop shortcut 'VaultBox' to" -ForegroundColor Gray
        Write-Host "  auto-launch $browserName with the extension loaded." -ForegroundColor Gray
    }
    Write-Host ""

    if ($browserExe) {
        $openNow = [System.Windows.MessageBox]::Show(
            "VaultBox installed successfully for $browserName!`n`nOpen the extensions page now to finish setup?",
            "VaultBox Installer",
            "YesNo",
            "Question"
        )
        if ($openNow -eq "Yes") {
            Start-Process $browserExe $extPage
        }
    }

    Read-Host "  Press Enter to exit"
}

function Uninstall-VaultBox {
    Write-Header
    Write-Host "  [*] Uninstalling VaultBox..." -ForegroundColor Yellow

    if (Test-Path $InstallDir) {
        Remove-Item -Path $InstallDir -Recurse -Force
        Write-Host "  [OK] Removed $InstallDir" -ForegroundColor Green
    }

    $desktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "VaultBox.lnk"
    if (Test-Path $desktopLink) {
        Remove-Item $desktopLink -Force
        Write-Host "  [OK] Removed desktop shortcut." -ForegroundColor Green
    }

    $startLink = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\VaultBox.lnk"
    if (Test-Path $startLink) {
        Remove-Item $startLink -Force
        Write-Host "  [OK] Removed Start Menu shortcut." -ForegroundColor Green
    }

    Write-Host ""
    Write-Host "  [OK] VaultBox has been uninstalled." -ForegroundColor Green
    Write-Host "  Note: Remove the extension from your browser manually" -ForegroundColor DarkGray
    Write-Host "        if it was loaded there." -ForegroundColor DarkGray
    Write-Host ""
    Read-Host "  Press Enter to exit"
}

# --- Main ---
if ($Uninstall) {
    Uninstall-VaultBox
} else {
    Install-VaultBox
}
