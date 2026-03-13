# VaultBox Installer v0.2.0
# One-click installer for VaultBox offline password manager
# No prerequisites required - installs everything automatically

param(
    [switch]$Uninstall
)

# --- Auto-elevate ---
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# --- Hide console ---
Add-Type -Name Win32 -Namespace Native -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
'@
[Native.Win32]::ShowWindow([Native.Win32]::GetConsoleWindow(), 0) | Out-Null

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$script:InstallDir = "$env:LOCALAPPDATA\VaultBox"
$script:ReleaseUrl = "https://github.com/SysAdminDoc/VaultBox/releases/latest/download"
$script:ScriptDir = Split-Path -Parent $PSCommandPath
$script:PythonVersion = "3.12.8"
$script:PythonEmbedUrl = "https://www.python.org/ftp/python/$($script:PythonVersion)/python-$($script:PythonVersion)-embed-amd64.zip"

# --- Browser Detection ---
function Get-BrowserExe([string]$key) {
    $searchPaths = @{
        Chrome = @(
            "${env:ProgramFiles}\Google\Chrome\Application\chrome.exe"
            "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
            "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
        )
        Edge = @(
            "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
            "${env:ProgramFiles}\Microsoft\Edge\Application\msedge.exe"
            "$env:LOCALAPPDATA\Microsoft\Edge\Application\msedge.exe"
        )
        Brave = @(
            "$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\Application\brave.exe"
            "${env:ProgramFiles}\BraveSoftware\Brave-Browser\Application\brave.exe"
        )
        Chromium = @(
            "$env:LOCALAPPDATA\Chromium\Application\chrome.exe"
            "${env:ProgramFiles}\Chromium\Application\chrome.exe"
            "${env:ProgramFiles(x86)}\Chromium\Application\chrome.exe"
            "$env:LOCALAPPDATA\ungoogled-chromium\chrome.exe"
        )
        Firefox = @(
            "${env:ProgramFiles}\Mozilla Firefox\firefox.exe"
            "${env:ProgramFiles(x86)}\Mozilla Firefox\firefox.exe"
        )
    }
    foreach ($p in $searchPaths[$key]) {
        if (Test-Path $p) { return $p }
    }
    if ($key -eq "Chrome") {
        try {
            $reg = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe" -ErrorAction SilentlyContinue
            if ($reg -and (Test-Path $reg.'(default)')) { return $reg.'(default)' }
        } catch {}
    }
    return $null
}

$script:BrowserDefs = @(
    @{ Key="Chrome";   Name="Google Chrome";       Zip="VaultBox-v0.1.0-chrome.zip";  IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Edge";     Name="Microsoft Edge";      Zip="VaultBox-v0.1.0-chrome.zip";  IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Brave";    Name="Brave";               Zip="VaultBox-v0.1.0-chrome.zip";  IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Chromium"; Name="Ungoogled Chromium";   Zip="VaultBox-v0.1.0-chrome.zip";  IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Firefox";  Name="Firefox";             Zip="VaultBox-v0.1.0-firefox.zip"; IsFirefox=$true;  Exe=$null; Detected=$false }
)

foreach ($b in $script:BrowserDefs) {
    $exe = Get-BrowserExe $b.Key
    $b.Exe = $exe
    $b.Detected = ($null -ne $exe)
}

$script:DetectedBrowsers = @($script:BrowserDefs | Where-Object { $_.Detected })

# --- Shared state ---
$script:LogQueue = [System.Collections.Concurrent.ConcurrentQueue[hashtable]]::new()
$script:WorkerBusy = $false

# --- XAML GUI ---
$detectedList = if ($script:DetectedBrowsers.Count -gt 0) {
    ($script:DetectedBrowsers | ForEach-Object { $_.Name }) -join ", "
} else {
    "None detected"
}

$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="VaultBox Installer" Width="520" Height="580"
        WindowStartupLocation="CenterScreen" ResizeMode="NoResize"
        Background="#0d0e1a" Foreground="#e2e8f0">
    <Window.Resources>
        <Style x:Key="AccentBtn" TargetType="Button">
            <Setter Property="Background" Value="#3b82f6"/>
            <Setter Property="Foreground" Value="White"/>
            <Setter Property="FontSize" Value="16"/>
            <Setter Property="FontWeight" Value="Bold"/>
            <Setter Property="Padding" Value="32,14"/>
            <Setter Property="BorderThickness" Value="0"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="8" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#2563eb"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#1e293b"/>
                                <Setter Property="Foreground" Value="#64748b"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="SecondaryBtn" TargetType="Button">
            <Setter Property="Background" Value="#1e293b"/>
            <Setter Property="Foreground" Value="#94a3b8"/>
            <Setter Property="FontSize" Value="12"/>
            <Setter Property="Padding" Value="16,8"/>
            <Setter Property="BorderThickness" Value="0"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="4" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#334155"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#0f1119"/>
                                <Setter Property="Foreground" Value="#475569"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
    </Window.Resources>
    <Grid Margin="32">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <!-- Header -->
        <StackPanel Grid.Row="0" HorizontalAlignment="Center" Margin="0,8,0,20">
            <TextBlock Text="VaultBox" FontSize="36" FontWeight="Bold" Foreground="#60a5fa"
                       HorizontalAlignment="Center"/>
            <TextBlock Text="Your passwords. Your device. Nobody else's."
                       FontSize="13" Foreground="#64748b" HorizontalAlignment="Center" Margin="0,4,0,0"/>
        </StackPanel>

        <!-- Info Card -->
        <Border Grid.Row="1" Background="#151825" CornerRadius="10" Padding="20,16" Margin="0,0,0,16"
                BorderBrush="#1e293b" BorderThickness="1">
            <StackPanel>
                <TextBlock Text="What VaultBox does:" FontSize="13" FontWeight="SemiBold"
                           Foreground="#cbd5e1" Margin="0,0,0,8"/>
                <TextBlock Text="  Stores all your passwords locally on this computer"
                           FontSize="12" Foreground="#94a3b8" Margin="0,2"/>
                <TextBlock Text="  Never sends anything to the internet"
                           FontSize="12" Foreground="#94a3b8" Margin="0,2"/>
                <TextBlock Text="  Auto-fills passwords in your browser"
                           FontSize="12" Foreground="#94a3b8" Margin="0,2"/>
                <TextBlock Text="  Generates strong passwords for you"
                           FontSize="12" Foreground="#94a3b8" Margin="0,2"/>
            </StackPanel>
        </Border>

        <!-- Detected Browsers -->
        <Border Grid.Row="2" Background="#151825" CornerRadius="8" Padding="16,12" Margin="0,0,0,16"
                BorderBrush="#1e293b" BorderThickness="1">
            <DockPanel>
                <TextBlock Text="Detected browsers:" FontSize="12" Foreground="#64748b"
                           DockPanel.Dock="Left" VerticalAlignment="Center"/>
                <TextBlock x:Name="txtBrowsers" FontSize="12" FontWeight="SemiBold" Foreground="#4ade80"
                           HorizontalAlignment="Right" VerticalAlignment="Center"/>
            </DockPanel>
        </Border>

        <!-- Install Button -->
        <Button Grid.Row="3" x:Name="btnInstall" Content="Install VaultBox"
                Style="{StaticResource AccentBtn}" HorizontalAlignment="Center" Margin="0,0,0,12"/>

        <!-- Log Output -->
        <Border Grid.Row="4" Background="#080911" CornerRadius="6" Margin="0,0,0,8" Padding="2"
                BorderBrush="#1e293b" BorderThickness="1">
            <ScrollViewer x:Name="logScroller" VerticalScrollBarVisibility="Auto">
                <TextBlock x:Name="txtLog" FontFamily="Consolas" FontSize="11" Foreground="#64748b"
                           Padding="12,8" TextWrapping="Wrap"/>
            </ScrollViewer>
        </Border>

        <!-- Bottom Buttons -->
        <DockPanel Grid.Row="5" Margin="0,4,0,0">
            <Button x:Name="btnUninstall" Content="Uninstall" Style="{StaticResource SecondaryBtn}"
                    DockPanel.Dock="Left"/>
            <Button x:Name="btnClose" Content="Close" Style="{StaticResource SecondaryBtn}"
                    HorizontalAlignment="Right"/>
        </DockPanel>
    </Grid>
</Window>
'@

$window = [System.Windows.Markup.XamlReader]::Parse($xaml)
$txtBrowsers   = $window.FindName("txtBrowsers")
$txtLog        = $window.FindName("txtLog")
$logScroller   = $window.FindName("logScroller")
$btnInstall    = $window.FindName("btnInstall")
$btnClose      = $window.FindName("btnClose")
$btnUninstall  = $window.FindName("btnUninstall")

$txtBrowsers.Text = $detectedList
$txtLog.Text = "Ready to install. Click the button above to get started."

if ($script:DetectedBrowsers.Count -eq 0) {
    $txtBrowsers.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(248, 113, 113))
    $txtBrowsers.Text = "No supported browser found"
    $btnInstall.IsEnabled = $false
}

# --- Log helpers ---
function Append-LogLine([string]$text, [string]$color) {
    $run = New-Object System.Windows.Documents.Run($text + "`r`n")
    switch ($color) {
        "Green"  { $run.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(74, 222, 128)) }
        "Yellow" { $run.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(250, 204, 21)) }
        "Red"    { $run.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(248, 113, 113)) }
        "Cyan"   { $run.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(96, 165, 250)) }
        default  { $run.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(148, 163, 184)) }
    }
    $txtLog.Inlines.Add($run)
    $logScroller.ScrollToEnd()
}

$logTimer = New-Object System.Windows.Threading.DispatcherTimer
$logTimer.Interval = [TimeSpan]::FromMilliseconds(100)
$logTimer.Add_Tick({
    $item = $null
    while ($script:LogQueue.TryDequeue([ref]$item)) {
        Append-LogLine $item.Text $item.Color
    }
})
$logTimer.Start()

# --- Install Worker ---
function Start-InstallWorker {
    $script:WorkerBusy = $true
    $btnInstall.IsEnabled = $false
    $btnUninstall.IsEnabled = $false
    $txtLog.Text = ""
    $txtLog.Inlines.Clear()

    $queue = $script:LogQueue
    $installDir = $script:InstallDir
    $releaseUrl = $script:ReleaseUrl
    $scriptDir = $script:ScriptDir
    $defs = $script:DetectedBrowsers
    $pythonEmbedUrl = $script:PythonEmbedUrl

    $ps = [PowerShell]::Create()
    $ps.AddScript({
        param($queue, $defs, $installDir, $releaseUrl, $scriptDir, $pythonEmbedUrl)

        function QLog($text, $color) { $queue.Enqueue(@{ Text=$text; Color=$color }) }

        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'

        QLog "VaultBox Installer v0.2.0" "Cyan"
        QLog "" ""

        # =================================================================
        # Step 1: Set up embedded Python + VaultBox Server
        # =================================================================
        QLog "[1/3] Setting up VaultBox Server..." "Cyan"

        $serverDir = Join-Path $installDir "server"
        $pythonDir = Join-Path $serverDir "python"

        if (-not (Test-Path $installDir)) { New-Item $installDir -ItemType Directory -Force | Out-Null }
        if (-not (Test-Path $serverDir)) { New-Item $serverDir -ItemType Directory -Force | Out-Null }

        # Copy or download server script
        $serverScript = Join-Path $serverDir "vaultbox_server.py"
        $localServer = Join-Path $scriptDir "server\vaultbox_server.py"

        if (Test-Path $localServer) {
            QLog "  Found local server files." ""
            Copy-Item $localServer $serverScript -Force
            $reqFile = Join-Path $scriptDir "server\requirements.txt"
            if (Test-Path $reqFile) { Copy-Item $reqFile (Join-Path $serverDir "requirements.txt") -Force }
        } else {
            QLog "  Downloading server..." "Yellow"
            try {
                Invoke-WebRequest -Uri "$releaseUrl/vaultbox_server.py" -OutFile $serverScript -UseBasicParsing
                Invoke-WebRequest -Uri "$releaseUrl/requirements.txt" -OutFile (Join-Path $serverDir "requirements.txt") -UseBasicParsing
                QLog "  Server downloaded." "Green"
            } catch {
                QLog "  ERROR: Could not download server files." "Red"
                QLog "  $($_.Exception.Message)" "Red"
                return
            }
        }

        # Find or install Python
        $pythonExe = $null

        # Check for bundled Python first
        $bundledPython = Join-Path $pythonDir "python.exe"
        if (Test-Path $bundledPython) {
            $pythonExe = $bundledPython
            QLog "  Using bundled Python." "Green"
        }

        # Check for system Python
        if (-not $pythonExe) {
            foreach ($cmd in @("python", "python3", "py")) {
                try {
                    $ver = & $cmd --version 2>&1
                    if ($ver -match "Python 3") {
                        $pythonExe = (Get-Command $cmd -ErrorAction SilentlyContinue).Source
                        if (-not $pythonExe) { $pythonExe = $cmd }
                        QLog "  Found system Python: $ver" "Green"
                        break
                    }
                } catch {}
            }
        }

        # Download embedded Python if not found
        if (-not $pythonExe) {
            QLog "  Python not found. Downloading portable Python..." "Yellow"
            try {
                if (-not (Test-Path $pythonDir)) { New-Item $pythonDir -ItemType Directory -Force | Out-Null }
                $pythonZip = Join-Path $env:TEMP "python-embed.zip"
                Invoke-WebRequest -Uri $pythonEmbedUrl -OutFile $pythonZip -UseBasicParsing
                Expand-Archive $pythonZip $pythonDir -Force
                Remove-Item $pythonZip -Force -ErrorAction SilentlyContinue

                $pythonExe = Join-Path $pythonDir "python.exe"

                # Enable pip in embedded Python: uncomment "import site" in python*._pth
                $pthFile = Get-ChildItem $pythonDir -Filter "python*._pth" | Select-Object -First 1
                if ($pthFile) {
                    $content = Get-Content $pthFile.FullName
                    $content = $content -replace '#import site', 'import site'
                    Set-Content $pthFile.FullName $content
                }

                # Install pip
                QLog "  Installing pip..." "Yellow"
                $getPip = Join-Path $env:TEMP "get-pip.py"
                Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $getPip -UseBasicParsing
                & $pythonExe $getPip --no-warn-script-location 2>&1 | Out-Null
                Remove-Item $getPip -Force -ErrorAction SilentlyContinue

                QLog "  Portable Python installed." "Green"
            } catch {
                QLog "  ERROR: Could not install Python." "Red"
                QLog "  $($_.Exception.Message)" "Red"
                QLog "  Please install Python 3.10+ from python.org and re-run." ""
                return
            }
        }

        # Install Python dependencies
        $reqFile = Join-Path $serverDir "requirements.txt"
        if (Test-Path $reqFile) {
            QLog "  Installing server dependencies..." "Yellow"
            try {
                & $pythonExe -m pip install -r $reqFile --break-system-packages -q --no-warn-script-location 2>&1 | Out-Null
                QLog "  Dependencies installed." "Green"
            } catch {
                QLog "  Warning: Some dependencies may not have installed." "Yellow"
            }
        }

        # Create startup batch file
        $batFile = Join-Path $serverDir "start-server.bat"
        $batContent = "@echo off`r`ncd /d `"$serverDir`"`r`nstart /B `"VaultBox Server`" `"$pythonExe`" `"$serverScript`""
        Set-Content $batFile $batContent -Encoding ASCII

        # Create startup shortcut (run on Windows login)
        try {
            $shell = New-Object -ComObject WScript.Shell
            $startupDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
            $lnk = $shell.CreateShortcut((Join-Path $startupDir "VaultBox Server.lnk"))
            $lnk.TargetPath = $pythonExe
            $lnk.Arguments = "`"$serverScript`""
            $lnk.WorkingDirectory = $serverDir
            $lnk.Description = "VaultBox Local Password Server"
            $lnk.WindowStyle = 7  # Minimized
            $lnk.Save()
            QLog "  Server set to auto-start on login." "Green"
        } catch {
            QLog "  Warning: Could not create startup shortcut." "Yellow"
        }

        # Start server now
        QLog "  Starting VaultBox Server..." "Yellow"
        try {
            Start-Process $pythonExe -ArgumentList "`"$serverScript`"" -WorkingDirectory $serverDir -WindowStyle Hidden
            Start-Sleep -Seconds 3

            try {
                Invoke-WebRequest -Uri "http://127.0.0.1:8787/alive" -UseBasicParsing -TimeoutSec 5 | Out-Null
                QLog "  Server running on http://127.0.0.1:8787" "Green"
            } catch {
                QLog "  Server started but not responding yet. It may need a moment." "Yellow"
            }
        } catch {
            QLog "  Warning: Could not start server." "Yellow"
        }

        QLog "" ""

        # =================================================================
        # Step 2: Install extension for all detected browsers
        # =================================================================
        QLog "[2/3] Installing browser extension..." "Cyan"

        $extDir = Join-Path $installDir "extension"

        # Get extension files
        $buildDir = Join-Path $scriptDir "apps\browser\build"
        $firefoxDef = $defs | Where-Object { $_.IsFirefox } | Select-Object -First 1
        $chromiumBrowsers = @($defs | Where-Object { -not $_.IsFirefox })

        if ($chromiumBrowsers.Count -gt 0) {
            $zipName = $chromiumBrowsers[0].Zip

            if (-not (Test-Path $extDir)) { New-Item $extDir -ItemType Directory -Force | Out-Null }

            if (Test-Path (Join-Path $buildDir "manifest.json")) {
                QLog "  Found local build output." ""
                Copy-Item "$buildDir\*" $extDir -Recurse -Force
            } elseif (Test-Path (Join-Path $scriptDir $zipName)) {
                QLog "  Found local extension package." ""
                Expand-Archive (Join-Path $scriptDir $zipName) $extDir -Force
            } else {
                QLog "  Downloading extension..." "Yellow"
                try {
                    $tmpZip = Join-Path $env:TEMP $zipName
                    Invoke-WebRequest -Uri "$releaseUrl/$zipName" -OutFile $tmpZip -UseBasicParsing
                    Expand-Archive $tmpZip $extDir -Force
                    Remove-Item $tmpZip -Force -ErrorAction SilentlyContinue
                    QLog "  Extension downloaded." "Green"
                } catch {
                    QLog "  ERROR: Could not download extension." "Red"
                    QLog "  $($_.Exception.Message)" "Red"
                    return
                }
            }

            # Fix nested extraction
            if (-not (Test-Path (Join-Path $extDir "manifest.json"))) {
                $nested = Get-ChildItem $extDir -Filter "manifest.json" -Recurse | Select-Object -First 1
                if ($nested) {
                    Get-ChildItem (Split-Path $nested.FullName) | Move-Item -Destination $extDir -Force
                }
            }

            if (-not (Test-Path (Join-Path $extDir "manifest.json"))) {
                QLog "  ERROR: Extension manifest not found." "Red"
                return
            }

            QLog "  Extension files installed." "Green"

            # Auto-install via enterprise policy for each Chromium browser
            foreach ($bdef in $chromiumBrowsers) {
                $bkey = $bdef.Key
                $browserName = $bdef.Name
                $browserExe = $bdef.Exe

                if (-not $browserExe) { continue }

                try {
                    $crxDir = Join-Path $installDir "_crx"
                    if (-not (Test-Path $crxDir)) { New-Item $crxDir -ItemType Directory -Force | Out-Null }

                    $keyFile = Join-Path $crxDir "vaultbox.pem"
                    $crxFile = Join-Path $crxDir "vaultbox.crx"

                    # Only pack CRX once (reuse for all Chromium browsers)
                    if (-not (Test-Path $crxFile)) {
                        QLog "  Packing extension..." "Yellow"
                        $packArgs = "--pack-extension=`"$extDir`""
                        if (Test-Path $keyFile) {
                            $packArgs += " --pack-extension-key=`"$keyFile`""
                        }
                        $packProc = Start-Process -FilePath $browserExe -ArgumentList $packArgs -PassThru -WindowStyle Hidden
                        $packProc | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue

                        $parentDir = Split-Path $extDir
                        $baseName = Split-Path $extDir -Leaf
                        $generatedCrx = Join-Path $parentDir "$baseName.crx"
                        $generatedPem = Join-Path $parentDir "$baseName.pem"

                        if (Test-Path $generatedCrx) {
                            Move-Item $generatedCrx $crxFile -Force
                            if (Test-Path $generatedPem) { Move-Item $generatedPem $keyFile -Force }
                            QLog "  Extension packed." "Green"
                        }
                    }

                    if (Test-Path $crxFile) {
                        $extId = $null
                        if (Test-Path $keyFile) {
                            $pemContent = Get-Content $keyFile -Raw
                            $pemB64 = ($pemContent -replace '-----[^-]+-----', '' -replace '\s', '').Trim()
                            $derBytes = [Convert]::FromBase64String($pemB64)
                            $sha = [System.Security.Cryptography.SHA256]::Create()
                            $hash = $sha.ComputeHash($derBytes)
                            $extId = -join ($hash[0..15] | ForEach-Object { [char]([int][char]'a' + ($_ -shr 4)); [char]([int][char]'a' + ($_ -band 0x0f)) })
                            $sha.Dispose()
                        }

                        if ($extId) {
                            $crxFileUri = "file:///" + ($crxFile -replace '\\', '/')
                            $updateXml = Join-Path $crxDir "updates.xml"
                            $xmlContent = @"
<?xml version='1.0' encoding='UTF-8'?>
<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>
  <app appid='$extId'>
    <updatecheck codebase='$crxFileUri' version='0.2.0' />
  </app>
</gupdate>
"@
                            Set-Content $updateXml $xmlContent -Encoding UTF8
                            $updateUri = "file:///" + ($updateXml -replace '\\', '/')

                            $policyPaths = @{
                                Chrome   = "HKLM:\SOFTWARE\Policies\Google\Chrome\ExtensionInstallForcelist"
                                Edge     = "HKLM:\SOFTWARE\Policies\Microsoft\Edge\ExtensionInstallForcelist"
                                Brave    = "HKLM:\SOFTWARE\Policies\BraveSoftware\Brave\ExtensionInstallForcelist"
                                Chromium = "HKLM:\SOFTWARE\Policies\Chromium\ExtensionInstallForcelist"
                            }

                            $regPath = $policyPaths[$bkey]
                            if ($regPath) {
                                if (-not (Test-Path $regPath)) {
                                    New-Item -Path $regPath -Force | Out-Null
                                }

                                $existing = Get-Item -LiteralPath $regPath -ErrorAction SilentlyContinue
                                $slot = 1
                                $alreadySet = $false
                                if ($existing) {
                                    $usedSlots = $existing.GetValueNames() | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ }
                                    if ($usedSlots) { $slot = ($usedSlots | Measure-Object -Maximum).Maximum + 1 }
                                    foreach ($v in $existing.GetValueNames()) {
                                        $val = $existing.GetValue($v)
                                        if ($val -like "$extId;*") { $alreadySet = $true; break }
                                    }
                                }

                                if (-not $alreadySet) {
                                    Set-ItemProperty -LiteralPath $regPath -Name "$slot" -Value "$extId;$updateUri" -Type String
                                }
                                QLog "  $browserName -- extension will auto-install on launch." "Green"
                            }
                        }
                    }

                    # Create desktop shortcut as backup
                    try {
                        $shell = New-Object -ComObject WScript.Shell
                        $desktopDir = [Environment]::GetFolderPath("Desktop")
                        $startDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"

                        $lnk = $shell.CreateShortcut((Join-Path $desktopDir "VaultBox ($bkey).lnk"))
                        $lnk.TargetPath = $browserExe
                        $lnk.Arguments = "--load-extension=`"$extDir`""
                        $lnk.Description = "Launch $browserName with VaultBox"
                        $lnk.IconLocation = "$browserExe,0"
                        $lnk.Save()

                        $lnk2 = $shell.CreateShortcut((Join-Path $startDir "VaultBox ($bkey).lnk"))
                        $lnk2.TargetPath = $browserExe
                        $lnk2.Arguments = "--load-extension=`"$extDir`""
                        $lnk2.Description = "Launch $browserName with VaultBox"
                        $lnk2.IconLocation = "$browserExe,0"
                        $lnk2.Save()
                    } catch {}

                } catch {
                    QLog "  $browserName -- could not auto-install: $($_.Exception.Message)" "Yellow"
                }
            }
        }

        if ($firefoxDef) {
            QLog "  Firefox detected -- requires manual add-on loading." "Yellow"
            QLog "  Open about:debugging -> Load Temporary Add-on -> select manifest.json" ""
        }

        QLog "" ""

        # =================================================================
        # Step 3: Done
        # =================================================================
        QLog "[3/3] Installation complete!" "Cyan"
        QLog "" ""
        QLog "How to get started:" "Green"
        QLog "  1. Open your browser (or use the VaultBox desktop shortcut)" ""
        QLog "  2. Click the VaultBox icon in your browser toolbar" ""
        QLog "  3. Click 'Create account' and set your master password" ""
        QLog "  4. Start adding passwords!" ""
        QLog "" ""
        QLog "Your vault is stored at: $env:LOCALAPPDATA\VaultBox\vault.db" ""
        QLog "Back it up regularly to a USB drive or external storage." ""

    }).AddArgument($queue).AddArgument($defs).AddArgument($installDir).AddArgument($releaseUrl).AddArgument($scriptDir).AddArgument($pythonEmbedUrl) | Out-Null

    $handle = $ps.BeginInvoke()

    $pollTimer = New-Object System.Windows.Threading.DispatcherTimer
    $pollTimer.Interval = [TimeSpan]::FromMilliseconds(300)
    $pollTimer.Add_Tick({
        if ($handle.IsCompleted) {
            $pollTimer.Stop()
            try { $ps.EndInvoke($handle) } catch {}
            $ps.Dispose()
            $script:WorkerBusy = $false
            $btnInstall.IsEnabled = $true
            $btnUninstall.IsEnabled = $true
        }
    }.GetNewClosure())
    $pollTimer.Start()
}

# --- Uninstall Worker ---
function Start-UninstallWorker {
    $script:WorkerBusy = $true
    $btnInstall.IsEnabled = $false
    $btnUninstall.IsEnabled = $false
    $txtLog.Text = ""
    $txtLog.Inlines.Clear()

    $queue = $script:LogQueue
    $installDir = $script:InstallDir

    $ps = [PowerShell]::Create()
    $ps.AddScript({
        param($queue, $installDir)
        function QLog($text, $color) { $queue.Enqueue(@{ Text=$text; Color=$color }) }

        QLog "Uninstalling VaultBox..." "Yellow"

        # Stop server
        try {
            $serverProcs = Get-Process -Name "python*" -ErrorAction SilentlyContinue | Where-Object {
                try { $_.CommandLine -like "*vaultbox_server*" } catch { $false }
            }
            if ($serverProcs) {
                $serverProcs | Stop-Process -Force
                QLog "Stopped VaultBox Server." "Green"
            }
        } catch {}

        # Remove startup shortcut
        $startupDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
        $startupLnk = Join-Path $startupDir "VaultBox Server.lnk"
        if (Test-Path $startupLnk) {
            Remove-Item $startupLnk -Force
            QLog "Removed server auto-start." "Green"
        }

        # Remove registry policies
        $policyPaths = @(
            "HKLM:\SOFTWARE\Policies\Google\Chrome\ExtensionInstallForcelist"
            "HKLM:\SOFTWARE\Policies\Microsoft\Edge\ExtensionInstallForcelist"
            "HKLM:\SOFTWARE\Policies\BraveSoftware\Brave\ExtensionInstallForcelist"
            "HKLM:\SOFTWARE\Policies\Chromium\ExtensionInstallForcelist"
        )
        foreach ($regPath in $policyPaths) {
            if (Test-Path -LiteralPath $regPath) {
                $regItem = Get-Item -LiteralPath $regPath
                foreach ($name in $regItem.GetValueNames()) {
                    $val = $regItem.GetValue($name)
                    if ($val -like "*vaultbox*") {
                        Remove-ItemProperty -LiteralPath $regPath -Name $name -Force
                        QLog "Removed browser policy: $regPath\$name" "Green"
                    }
                }
            }
        }

        # Remove install directory
        if (Test-Path $installDir) {
            Remove-Item $installDir -Recurse -Force
            QLog "Removed $installDir" "Green"
        }

        # Remove shortcuts
        $desktop = [Environment]::GetFolderPath("Desktop")
        $startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
        foreach ($dir in @($desktop, $startMenu)) {
            Get-ChildItem $dir -Filter "VaultBox*.lnk" -ErrorAction SilentlyContinue | ForEach-Object {
                Remove-Item $_.FullName -Force
                QLog "Removed: $($_.Name)" "Green"
            }
        }

        QLog "" ""
        QLog "VaultBox uninstalled." "Green"
        QLog "Restart your browser(s) to complete removal." ""
    }).AddArgument($queue).AddArgument($installDir) | Out-Null

    $handle = $ps.BeginInvoke()

    $pollTimer = New-Object System.Windows.Threading.DispatcherTimer
    $pollTimer.Interval = [TimeSpan]::FromMilliseconds(300)
    $pollTimer.Add_Tick({
        if ($handle.IsCompleted) {
            $pollTimer.Stop()
            try { $ps.EndInvoke($handle) } catch {}
            $ps.Dispose()
            $script:WorkerBusy = $false
            $btnInstall.IsEnabled = $true
            $btnUninstall.IsEnabled = $true
        }
    }.GetNewClosure())
    $pollTimer.Start()
}

# --- Event Handlers ---
$btnClose.Add_Click({ $window.Close() })

$btnInstall.Add_Click({
    if ($script:WorkerBusy) { return }
    Start-InstallWorker
})

$btnUninstall.Add_Click({
    if ($script:WorkerBusy) { return }
    Start-UninstallWorker
})

if ($Uninstall) {
    $window.Add_ContentRendered({
        Start-UninstallWorker
    })
}

$window.ShowDialog() | Out-Null
