# VaultBox Installer v0.1.0
# Professional GUI installer for VaultBox offline password manager

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
    @{ Key="Chrome";   Name="Google Chrome";       Zip="VaultBox-v0.1.0-chrome.zip";  ExtPage="chrome://extensions";                      IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Edge";     Name="Microsoft Edge";       Zip="VaultBox-v0.1.0-chrome.zip";  ExtPage="edge://extensions";                        IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Brave";    Name="Brave";                Zip="VaultBox-v0.1.0-chrome.zip";  ExtPage="brave://extensions";                       IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Chromium"; Name="Ungoogled Chromium";   Zip="VaultBox-v0.1.0-chrome.zip";  ExtPage="chrome://extensions";                      IsFirefox=$false; Exe=$null; Detected=$false }
    @{ Key="Firefox";  Name="Firefox";              Zip="VaultBox-v0.1.0-firefox.zip"; ExtPage="about:debugging#/runtime/this-firefox";     IsFirefox=$true;  Exe=$null; Detected=$false }
)

foreach ($b in $script:BrowserDefs) {
    $exe = Get-BrowserExe $b.Key
    $b.Exe = $exe
    $b.Detected = ($null -ne $exe)
}

# --- Shared state for async ---
$script:LogQueue = [System.Collections.Concurrent.ConcurrentQueue[hashtable]]::new()
$script:WorkerBusy = $false

# --- XAML GUI ---
$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="VaultBox Installer v0.1.0" Width="560" Height="660"
        WindowStartupLocation="CenterScreen" ResizeMode="NoResize"
        Background="#0d0e1a" Foreground="#e2e8f0">
    <Window.Resources>
        <Style x:Key="AccentBtn" TargetType="Button">
            <Setter Property="Background" Value="#3b82f6"/>
            <Setter Property="Foreground" Value="White"/>
            <Setter Property="FontSize" Value="14"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="Padding" Value="24,10"/>
            <Setter Property="BorderThickness" Value="0"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="6" Padding="{TemplateBinding Padding}">
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
        <Style x:Key="BrowserCheck" TargetType="CheckBox">
            <Setter Property="Foreground" Value="#e2e8f0"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,5"/>
            <Setter Property="Cursor" Value="Hand"/>
        </Style>
    </Window.Resources>
    <Grid Margin="28">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <!-- Header -->
        <StackPanel Grid.Row="0" Margin="0,0,0,16">
            <TextBlock Text="VaultBox" FontSize="28" FontWeight="Bold" Foreground="#60a5fa"/>
            <TextBlock Text="Offline Password Manager" FontSize="13" Foreground="#64748b" Margin="0,2,0,0"/>
        </StackPanel>

        <!-- Separator -->
        <Border Grid.Row="1" Height="1" Background="#1e293b" Margin="0,0,0,16"/>

        <!-- Browser Selection -->
        <StackPanel Grid.Row="2" Margin="0,0,0,4">
            <DockPanel Margin="0,0,0,10">
                <TextBlock Text="Select browsers:" FontSize="14" FontWeight="SemiBold" Foreground="#cbd5e1"
                           VerticalAlignment="Center" DockPanel.Dock="Left"/>
                <Button x:Name="btnSelectAll" Content="Select All Detected" Style="{StaticResource SecondaryBtn}"
                        HorizontalAlignment="Right" DockPanel.Dock="Right"/>
            </DockPanel>
            <Border Background="#151825" CornerRadius="8" Padding="18,14" BorderBrush="#1e293b" BorderThickness="1">
                <StackPanel x:Name="browserList"/>
            </Border>
        </StackPanel>

        <!-- Install Path -->
        <StackPanel Grid.Row="3" Margin="0,12,0,4">
            <TextBlock Text="Install location:" FontSize="11" Foreground="#64748b" Margin="0,0,0,4"/>
            <Border Background="#151825" CornerRadius="4" Padding="10,7" BorderBrush="#1e293b" BorderThickness="1">
                <TextBlock x:Name="txtInstallPath" FontSize="11.5" FontFamily="Consolas" Foreground="#94a3b8" TextWrapping="NoWrap"/>
            </Border>
        </StackPanel>

        <!-- Log Output -->
        <Border Grid.Row="4" Background="#080911" CornerRadius="6" Margin="0,12,0,8" Padding="2"
                BorderBrush="#1e293b" BorderThickness="1">
            <ScrollViewer x:Name="logScroller" VerticalScrollBarVisibility="Auto">
                <TextBlock x:Name="txtLog" FontFamily="Consolas" FontSize="11" Foreground="#64748b"
                           Padding="12,8" TextWrapping="Wrap"/>
            </ScrollViewer>
        </Border>

        <!-- Buttons -->
        <DockPanel Grid.Row="5" Margin="0,4,0,0">
            <Button x:Name="btnUninstall" Content="Uninstall" Style="{StaticResource SecondaryBtn}"
                    DockPanel.Dock="Left"/>
            <StackPanel Orientation="Horizontal" HorizontalAlignment="Right">
                <Button x:Name="btnInstall" Content="Install" Style="{StaticResource AccentBtn}" Margin="0,0,10,0"/>
                <Button x:Name="btnClose" Content="Close" Style="{StaticResource SecondaryBtn}"/>
            </StackPanel>
        </DockPanel>
    </Grid>
</Window>
'@

$window = [System.Windows.Markup.XamlReader]::Parse($xaml)
$browserList    = $window.FindName("browserList")
$txtInstallPath = $window.FindName("txtInstallPath")
$txtLog         = $window.FindName("txtLog")
$logScroller    = $window.FindName("logScroller")
$btnInstall     = $window.FindName("btnInstall")
$btnClose       = $window.FindName("btnClose")
$btnUninstall   = $window.FindName("btnUninstall")
$btnSelectAll   = $window.FindName("btnSelectAll")

$txtInstallPath.Text = $script:InstallDir
$txtLog.Text = "Ready. Select browsers and click Install."

# --- Build browser checkboxes ---
$script:Checkboxes = @()
foreach ($b in $script:BrowserDefs) {
    $cb = New-Object System.Windows.Controls.CheckBox
    $cb.Style = $window.FindResource("BrowserCheck")
    $cb.Tag = $b.Key

    $sp = New-Object System.Windows.Controls.StackPanel
    $sp.Orientation = "Horizontal"

    $nameBlock = New-Object System.Windows.Controls.TextBlock
    $nameBlock.Text = $b.Name
    $nameBlock.VerticalAlignment = "Center"
    $sp.Children.Add($nameBlock) | Out-Null

    if ($b.Detected) {
        $badge = New-Object System.Windows.Controls.Border
        $badge.Background = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromArgb(40, 34, 197, 94))
        $badge.CornerRadius = New-Object System.Windows.CornerRadius(3)
        $badge.Padding = New-Object System.Windows.Thickness(6, 2, 6, 2)
        $badge.Margin = New-Object System.Windows.Thickness(10, 0, 0, 0)
        $badgeText = New-Object System.Windows.Controls.TextBlock
        $badgeText.Text = "Detected"
        $badgeText.FontSize = 10
        $badgeText.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(74, 222, 128))
        $badgeText.VerticalAlignment = "Center"
        $badge.Child = $badgeText
        $sp.Children.Add($badge) | Out-Null
        $cb.IsChecked = $true
    } else {
        $dim = New-Object System.Windows.Controls.TextBlock
        $dim.Text = "  not found"
        $dim.FontSize = 11
        $dim.Foreground = New-Object System.Windows.Media.SolidColorBrush ([System.Windows.Media.Color]::FromRgb(71, 85, 105))
        $dim.VerticalAlignment = "Center"
        $sp.Children.Add($dim) | Out-Null
    }

    $cb.Content = $sp
    $browserList.Children.Add($cb) | Out-Null
    $script:Checkboxes += $cb
}

# --- Log queue consumer (runs on UI thread via DispatcherTimer) ---
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

# --- Worker functions ---
function Start-InstallWorker($selectedDefs) {
    $script:WorkerBusy = $true
    $btnInstall.IsEnabled = $false
    $btnUninstall.IsEnabled = $false
    $txtLog.Text = ""
    $txtLog.Inlines.Clear()

    $queue = $script:LogQueue
    $installDir = $script:InstallDir
    $releaseUrl = $script:ReleaseUrl
    $scriptDir = $script:ScriptDir
    $defs = $selectedDefs

    $ps = [PowerShell]::Create()
    $ps.AddScript({
        param($queue, $defs, $installDir, $releaseUrl, $scriptDir)

        function QLog($text, $color) { $queue.Enqueue(@{ Text=$text; Color=$color }) }

        QLog "VaultBox Installer" "Cyan"
        QLog "Installing for $($defs.Count) browser(s)...`r`n" ""

        foreach ($bdef in $defs) {
            $browserName = $bdef.Name
            $zipName = $bdef.Zip
            $isFirefox = $bdef.IsFirefox
            $browserExe = $bdef.Exe
            $extPage = $bdef.ExtPage
            $bkey = $bdef.Key

            QLog "--- $browserName ---" "Cyan"

            $buildDir = Join-Path $scriptDir "apps\browser\build"

            try {
                if (Test-Path $installDir) { Remove-Item $installDir -Recurse -Force }
                New-Item $installDir -ItemType Directory -Force | Out-Null

                if (Test-Path (Join-Path $buildDir "manifest.json")) {
                    QLog "  Found local build output." "Yellow"
                    Copy-Item "$buildDir\*" $installDir -Recurse -Force
                } elseif (Test-Path (Join-Path $scriptDir $zipName)) {
                    QLog "  Found local package: $zipName" "Yellow"
                    Expand-Archive (Join-Path $scriptDir $zipName) $installDir -Force
                } else {
                    QLog "  Downloading $zipName..." "Yellow"
                    $tmpZip = Join-Path $env:TEMP $zipName
                    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                    $ProgressPreference = 'SilentlyContinue'
                    Invoke-WebRequest -Uri "$releaseUrl/$zipName" -OutFile $tmpZip -UseBasicParsing
                    $sizeMB = [math]::Round((Get-Item $tmpZip).Length / 1MB, 1)
                    QLog "  Downloaded ($sizeMB MB)" "Green"
                    Expand-Archive $tmpZip $installDir -Force
                    Remove-Item $tmpZip -Force -ErrorAction SilentlyContinue
                }

                # Fix nested extraction
                if (-not (Test-Path (Join-Path $installDir "manifest.json"))) {
                    $nested = Get-ChildItem $installDir -Filter "manifest.json" -Recurse | Select-Object -First 1
                    if ($nested) {
                        Get-ChildItem (Split-Path $nested.FullName) | Move-Item -Destination $installDir -Force
                    }
                }

                if (-not (Test-Path (Join-Path $installDir "manifest.json"))) {
                    QLog "  FAILED: manifest.json not found." "Red"
                    QLog "" ""
                    continue
                }

                QLog "  Extension installed to $installDir" "Green"
            } catch {
                QLog "  FAILED: $($_.Exception.Message)" "Red"
                QLog "" ""
                continue
            }

            # Shortcuts
            if ($browserExe -and -not $isFirefox) {
                try {
                    $shell = New-Object -ComObject WScript.Shell
                    $desktopDir = [Environment]::GetFolderPath("Desktop")
                    $startDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"

                    $lnk = $shell.CreateShortcut((Join-Path $desktopDir "VaultBox ($bkey).lnk"))
                    $lnk.TargetPath = $browserExe
                    $lnk.Arguments = "--load-extension=`"$installDir`""
                    $lnk.Description = "Launch $browserName with VaultBox"
                    $lnk.IconLocation = "$browserExe,0"
                    $lnk.Save()

                    $lnk2 = $shell.CreateShortcut((Join-Path $startDir "VaultBox ($bkey).lnk"))
                    $lnk2.TargetPath = $browserExe
                    $lnk2.Arguments = "--load-extension=`"$installDir`""
                    $lnk2.Description = "Launch $browserName with VaultBox"
                    $lnk2.IconLocation = "$browserExe,0"
                    $lnk2.Save()

                    QLog "  Desktop + Start Menu shortcuts created." "Green"
                } catch {
                    QLog "  Warning: Could not create shortcuts." "Yellow"
                }
            }

            if ($isFirefox) {
                QLog "  -> Open about:debugging -> Load Temporary Add-on" ""
            } else {
                QLog "  -> Open $extPage -> Developer mode -> Load unpacked" ""
                QLog "  -> Or use 'VaultBox ($bkey)' desktop shortcut" ""
            }

            if ($browserExe) {
                try { Start-Process $browserExe $extPage } catch {}
            }

            QLog "" ""
        }

        QLog "Installation complete!" "Green"
    }).AddArgument($queue).AddArgument($defs).AddArgument($installDir).AddArgument($releaseUrl).AddArgument($scriptDir) | Out-Null

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

        if (Test-Path $installDir) {
            Remove-Item $installDir -Recurse -Force
            QLog "Removed $installDir" "Green"
        } else {
            QLog "Install directory not found (already removed?)." "Yellow"
        }

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
        QLog "Remove the extension from your browser(s) manually if loaded." ""
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
$btnSelectAll.Add_Click({
    foreach ($cb in $script:Checkboxes) {
        $key = $cb.Tag
        $bdef = $script:BrowserDefs | Where-Object { $_.Key -eq $key }
        if ($bdef.Detected) { $cb.IsChecked = $true }
    }
})

$btnClose.Add_Click({ $window.Close() })

$btnInstall.Add_Click({
    if ($script:WorkerBusy) { return }

    $selected = @()
    foreach ($cb in $script:Checkboxes) {
        if ($cb.IsChecked) {
            $key = $cb.Tag
            $match = $script:BrowserDefs | Where-Object { $_.Key -eq $key }
            if ($match) { $selected += $match }
        }
    }

    if ($selected.Count -eq 0) {
        $txtLog.Text = ""
        $txtLog.Inlines.Clear()
        Append-LogLine "No browsers selected. Check at least one browser above." "Red"
        return
    }

    Start-InstallWorker $selected
})

$btnUninstall.Add_Click({
    if ($script:WorkerBusy) { return }
    Start-UninstallWorker
})

# --- Auto-uninstall if flag passed ---
if ($Uninstall) {
    $window.Add_ContentRendered({
        Start-UninstallWorker
    })
}

$window.ShowDialog() | Out-Null
