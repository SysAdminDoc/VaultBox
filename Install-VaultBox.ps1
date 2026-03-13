# VaultBox Installer v0.1.0
# Installs the VaultBox offline password manager Chrome extension

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

$extensionId = "vaultbox-offline"
$registryPath = "HKLM:\SOFTWARE\Google\Chrome\Extensions\$extensionId"
$chromeNMHPath = "HKLM:\SOFTWARE\Google\Chrome\NativeMessagingHosts"

function Write-Header {
    Clear-Host
    Write-Host ""
    Write-Host "  ======================================" -ForegroundColor Cyan
    Write-Host "    VaultBox - Offline Password Manager" -ForegroundColor White
    Write-Host "    Installer v0.1.0" -ForegroundColor DarkGray
    Write-Host "  ======================================" -ForegroundColor Cyan
    Write-Host ""
}

function Install-VaultBox {
    Write-Header

    # Find the build output
    $scriptDir = Split-Path -Parent $PSCommandPath
    $buildDir = Join-Path $scriptDir "apps\browser\build"

    if (-not (Test-Path (Join-Path $buildDir "manifest.json"))) {
        # Check if there's a dist zip next to the installer
        $zipFile = Get-ChildItem -Path $scriptDir -Filter "VaultBox-*.zip" | Select-Object -First 1
        if ($zipFile) {
            Write-Host "  [*] Found packaged extension: $($zipFile.Name)" -ForegroundColor Yellow
            Write-Host "  [*] Extracting to $InstallDir ..." -ForegroundColor Yellow

            if (Test-Path $InstallDir) {
                Remove-Item -Path $InstallDir -Recurse -Force
            }
            New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
            Expand-Archive -Path $zipFile.FullName -DestinationPath $InstallDir -Force
        } else {
            Write-Host "  [!] No build output found." -ForegroundColor Red
            Write-Host "  [!] Expected: $buildDir" -ForegroundColor Red
            Write-Host "  [!] Or a VaultBox-*.zip next to this installer." -ForegroundColor Red
            Write-Host ""
            Write-Host "  Build the extension first:" -ForegroundColor Yellow
            Write-Host "    cd apps/browser && npm run build:chrome" -ForegroundColor Gray
            Write-Host ""
            Read-Host "  Press Enter to exit"
            return
        }
    } else {
        Write-Host "  [*] Copying extension to $InstallDir ..." -ForegroundColor Yellow

        if (Test-Path $InstallDir) {
            Remove-Item -Path $InstallDir -Recurse -Force
        }
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
        Copy-Item -Path "$buildDir\*" -Destination $InstallDir -Recurse -Force
    }

    # Verify manifest exists in install dir
    $manifestPath = Join-Path $InstallDir "manifest.json"
    if (-not (Test-Path $manifestPath)) {
        # Might be nested one level deep from zip extraction
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
    $uninstallPath = Join-Path $InstallDir "Uninstall-VaultBox.ps1"
    Set-Content -Path $uninstallPath -Value $uninstallScript -Force

    # Create desktop shortcut to Chrome with extension loaded
    $desktopPath = [Environment]::GetFolderPath("Desktop")
    $shortcutPath = Join-Path $desktopPath "VaultBox.lnk"
    $chromeExe = Get-ChromePath

    if ($chromeExe) {
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $chromeExe
        $shortcut.Arguments = "--load-extension=`"$InstallDir`""
        $shortcut.Description = "Launch Chrome with VaultBox extension"
        $shortcut.IconLocation = "$chromeExe,0"
        $shortcut.Save()
        Write-Host "  [OK] Desktop shortcut created." -ForegroundColor Green
    }

    # Create Start Menu entry
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
    $startShortcut = Join-Path $startMenuDir "VaultBox.lnk"
    if ($chromeExe) {
        $shell2 = New-Object -ComObject WScript.Shell
        $sc2 = $shell2.CreateShortcut($startShortcut)
        $sc2.TargetPath = $chromeExe
        $sc2.Arguments = "--load-extension=`"$InstallDir`""
        $sc2.Description = "Launch Chrome with VaultBox extension"
        $sc2.IconLocation = "$chromeExe,0"
        $sc2.Save()
        Write-Host "  [OK] Start Menu shortcut created." -ForegroundColor Green
    }

    # Package into zip for distribution
    $version = "0.1.0"
    $zipOutput = Join-Path $scriptDir "VaultBox-v$version-chrome.zip"
    if (-not (Test-Path $zipOutput)) {
        Write-Host "  [*] Creating distribution zip..." -ForegroundColor Yellow
        Compress-Archive -Path "$InstallDir\*" -DestinationPath $zipOutput -Force
        Write-Host "  [OK] Distribution package: $zipOutput" -ForegroundColor Green
    }

    Write-Host ""
    Write-Host "  ======================================" -ForegroundColor Green
    Write-Host "    Installation Complete!" -ForegroundColor White
    Write-Host "  ======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Extension installed to:" -ForegroundColor Gray
    Write-Host "    $InstallDir" -ForegroundColor White
    Write-Host ""
    Write-Host "  TO LOAD IN CHROME:" -ForegroundColor Yellow
    Write-Host "    1. Open Chrome -> chrome://extensions" -ForegroundColor Gray
    Write-Host "    2. Enable 'Developer mode' (top right)" -ForegroundColor Gray
    Write-Host "    3. Click 'Load unpacked'" -ForegroundColor Gray
    Write-Host "    4. Select: $InstallDir" -ForegroundColor White
    Write-Host ""
    Write-Host "  OR use the desktop shortcut 'VaultBox' to" -ForegroundColor Gray
    Write-Host "  auto-launch Chrome with the extension loaded." -ForegroundColor Gray
    Write-Host ""

    # Open Chrome extensions page
    if ($chromeExe) {
        $openNow = [System.Windows.MessageBox]::Show(
            "VaultBox installed successfully!`n`nOpen Chrome extensions page now to finish setup?",
            "VaultBox Installer",
            "YesNo",
            "Question"
        )
        if ($openNow -eq "Yes") {
            Start-Process $chromeExe "chrome://extensions"
        }
    }

    Read-Host "  Press Enter to exit"
}

function Uninstall-VaultBox {
    Write-Header
    Write-Host "  [*] Uninstalling VaultBox..." -ForegroundColor Yellow

    # Remove install directory
    if (Test-Path $InstallDir) {
        Remove-Item -Path $InstallDir -Recurse -Force
        Write-Host "  [OK] Removed $InstallDir" -ForegroundColor Green
    }

    # Remove desktop shortcut
    $desktopPath = [Environment]::GetFolderPath("Desktop")
    $shortcutPath = Join-Path $desktopPath "VaultBox.lnk"
    if (Test-Path $shortcutPath) {
        Remove-Item $shortcutPath -Force
        Write-Host "  [OK] Removed desktop shortcut." -ForegroundColor Green
    }

    # Remove Start Menu shortcut
    $startShortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\VaultBox.lnk"
    if (Test-Path $startShortcut) {
        Remove-Item $startShortcut -Force
        Write-Host "  [OK] Removed Start Menu shortcut." -ForegroundColor Green
    }

    Write-Host ""
    Write-Host "  [OK] VaultBox has been uninstalled." -ForegroundColor Green
    Write-Host "  Note: Remove the extension from chrome://extensions manually" -ForegroundColor DarkGray
    Write-Host "        if it was loaded there." -ForegroundColor DarkGray
    Write-Host ""
    Read-Host "  Press Enter to exit"
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
    # Try registry
    try {
        $regPath = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe" -ErrorAction SilentlyContinue
        if ($regPath -and (Test-Path $regPath.'(default)')) { return $regPath.'(default)' }
    } catch {}
    return $null
}

# --- Main ---
if ($Uninstall) {
    Uninstall-VaultBox
} else {
    Install-VaultBox
}
