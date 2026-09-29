<#
.SYNOPSIS
Installs a native Windows Infuse-IoT development environment with the ARM compiler toolchain.

.DESCRIPTION
This script installs the Infuse-IoT SDK and its Windows development dependencies:
  - Optionally installs host dependencies with winget.
  - Creates a Python 3.12 virtual environment.
  - Installs west.
  - Creates/updates an Infuse-IoT west workspace using minimal Git history.
  - Installs the Infuse-IoT build dependencies and Python Tools.
  - Exports the underlying Zephyr CMake package.
  - Installs the required SDK with only the arm-zephyr-eabi compiler.

Infuse-IoT-controlled content is kept under -InstallRoot:
  - <InstallRoot>\infuse-iot
  - <InstallRoot>\.venv
  - <InstallRoot>\sdk
  - <InstallRoot>\Use-Infuse-IoT.ps1
  - <InstallRoot>\install-infuse-iot.log

Host tools installed by winget are managed by Windows and may not live below -InstallRoot.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InstallRoot,

    [ValidateNotNullOrEmpty()]
    [ValidateScript({
        $pathSeparators = [char[]]@("\", "/")
        if ($_ -in @(".", "..") -or
            [System.IO.Path]::IsPathRooted($_) -or
            $_.IndexOfAny($pathSeparators) -ge 0) {
            throw "WorkspaceName must be a single directory name without path separators."
        }

        return $true
    })]
    [string]$WorkspaceName = "infuse-iot",

    [ValidateNotNullOrEmpty()]
    [string]$ManifestUrl = "https://github.com/Embeint/infuse-sdk",

    [ValidateNotNullOrEmpty()]
    [string]$Toolchain = "arm-zephyr-eabi",

    [switch]$InstallHostDependencies,

    [switch]$SkipWestUpdate,

    [switch]$FullWestHistory,

    [switch]$PersistUserEnvironment
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw "This installer only supports Windows."
}

function Resolve-FullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $expanded = [Environment]::ExpandEnvironmentVariables($Path)
    if ([System.IO.Path]::IsPathRooted($expanded)) {
        return [System.IO.Path]::GetFullPath($expanded)
    }

    return [System.IO.Path]::GetFullPath((Join-Path -Path (Get-Location) -ChildPath $expanded))
}

function ConvertTo-PowerShellSingleQuotedLiteral {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Value
    )

    return "'{0}'" -f $Value.Replace("'", "''")
}

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [scriptblock]$ScriptBlock
    )

    Write-Information "" -InformationAction Continue
    Write-Information "==> $Name" -InformationAction Continue
    & $ScriptBlock
}

function Test-Command {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    return $null -ne (Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue)
}

function Invoke-ExternalCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$Arguments = @(),

        [int[]]$SuccessExitCodes = @(0)
    )

    # This function validates native exit codes itself, including command-specific
    # non-zero success codes. Prevent PowerShell 7.3+ from throwing before that check.
    $PSNativeCommandUseErrorActionPreference = $false
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -notin $SuccessExitCodes) {
        throw "Command failed with exit code ${exitCode}: $FilePath $($Arguments -join ' ')"
    }
}

function Update-ProcessPathFromRegistry {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param()

    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $pathEntries = New-Object System.Collections.Generic.List[string]

    foreach ($pathValue in @($env:Path, $machinePath, $userPath)) {
        if (-not $pathValue) {
            continue
        }

        foreach ($pathEntry in ($pathValue -split ";")) {
            if ($pathEntry -and $pathEntry -notin $pathEntries) {
                $pathEntries.Add($pathEntry)
            }
        }
    }

    if ($PSCmdlet.ShouldProcess("the current process", "Refresh PATH from the registry")) {
        $env:Path = $pathEntries -join ";"
    }
}

function Find-7ZipExecutable {
    $command = Get-Command "7z.exe" -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($programFilesDirectory in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $programFilesDirectory) {
            continue
        }

        $candidate = Join-Path -Path $programFilesDirectory -ChildPath "7-Zip\7z.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    return $null
}

function Find-JLinkExecutable {
    $seggerRoots = @(
        (Join-Path -Path $env:ProgramFiles -ChildPath "SEGGER"),
        (Join-Path -Path ${env:ProgramFiles(x86)} -ChildPath "SEGGER"),
        (Join-Path -Path $env:USERPROFILE -ChildPath "SEGGER")
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }

    foreach ($seggerRoot in $seggerRoots) {
        $installDirectories = Get-ChildItem -LiteralPath $seggerRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "JLink*" } |
            Sort-Object -Property Name -Descending

        foreach ($installDirectory in $installDirectories) {
            $candidate = Join-Path -Path $installDirectory.FullName -ChildPath "JLink.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    return $null
}

function Add-DirectoryToProcessPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    if (($env:Path -split ";") -notcontains $Directory) {
        $env:Path = "$Directory;$env:Path"
    }
}

function Test-ZephyrSdkInstallation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SdkDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedVersion,

        [Parameter(Mandatory = $true)]
        [string]$GnuToolchain
    )

    $versionFile = Join-Path -Path $SdkDirectory -ChildPath "sdk_version"
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
        return $false
    }

    $installedVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    if ($installedVersion -ne $ExpectedVersion) {
        return $false
    }

    $compilerName = "$GnuToolchain-gcc.exe"
    $compilerCandidates = @(
        (Join-Path -Path $SdkDirectory -ChildPath "gnu\$GnuToolchain\bin\$compilerName"),
        (Join-Path -Path $SdkDirectory -ChildPath "$GnuToolchain\bin\$compilerName")
    )
    $hasCompiler = $null -ne ($compilerCandidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1)

    $hasHostTools =
        (Test-Path -LiteralPath (Join-Path $SdkDirectory "hosttools") -PathType Container) -or
        (Test-Path -LiteralPath (Join-Path $SdkDirectory "sysroots") -PathType Container)

    return $hasCompiler -and $hasHostTools
}

function Get-RegisteredZephyrSdkDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExpectedVersion
    )

    # An empty SDK registry is an expected state on a first install. Windows
    # PowerShell surfaces native stderr as an error record, so temporarily
    # suppress it to let us inspect west's exit code ourselves.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "SilentlyContinue"
        $sdkListOutput = @(& $script:VenvPython -m west sdk list 2>$null)
        $sdkListExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($sdkListExitCode -ne 0) {
        return $null
    }

    $versionPattern = "^\s*{0}:\s*`$" -f [regex]::Escape($ExpectedVersion)
    $foundVersion = $false

    foreach ($line in $sdkListOutput) {
        if ($line -match $versionPattern) {
            $foundVersion = $true
            continue
        }

        if ($foundVersion -and $line -match '^\s+path:\s+(.+?)\s*$') {
            return $Matches[1]
        }

        if ($foundVersion -and $line -match '^\S') {
            break
        }
    }

    return $null
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Id
    )

    Write-Information "Ensuring the latest available $Id is installed" -InformationAction Continue
    $wingetSuccessExitCodes = @(
        0,
        -1978335189, # APPINSTALLER_CLI_ERROR_UPDATE_NOT_APPLICABLE
        -1978335135  # APPINSTALLER_CLI_ERROR_PACKAGE_ALREADY_INSTALLED
    )

    Invoke-ExternalCommand -FilePath "winget" -SuccessExitCodes $wingetSuccessExitCodes -Arguments @(
        "install",
        "--id", $Id,
        "--exact",
        "--source", "winget",
        "--accept-package-agreements",
        "--accept-source-agreements",
        "--disable-interactivity",
        "--silent"
    )
}

function Install-NrfUtilCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    Invoke-ExternalCommand -FilePath "nrfutil" -Arguments @("install", $Name)
    Invoke-ExternalCommand -FilePath "nrfutil" -Arguments @($Name, "--version")
}

function Invoke-Py312 {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    Invoke-ExternalCommand -FilePath "py" -Arguments (@("-3.12") + $Arguments)
}

function Invoke-VenvPython {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    Invoke-ExternalCommand -FilePath $script:VenvPython -Arguments $Arguments
}

function Get-PythonMajorMinorVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonPath
    )

    $versionOutput = @(Invoke-ExternalCommand -FilePath $PythonPath -Arguments @(
        "-c",
        "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    ))

    return ($versionOutput -join "").Trim()
}

function Test-PythonVirtualEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonPath
    )

    $isVirtualEnvironment = @(Invoke-ExternalCommand -FilePath $PythonPath -Arguments @(
        "-c",
        "import sys; print(sys.prefix != sys.base_prefix)"
    ))

    return ($isVirtualEnvironment -join "").Trim() -eq "True"
}

function Invoke-West {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    Invoke-VenvPython -Arguments (@("-m", "west") + $Arguments)
}

$Root = Resolve-FullPath -Path $InstallRoot
$UsingLegacyWorkspace = $false
if (-not $PSBoundParameters.ContainsKey("WorkspaceName")) {
    $preferredWestConfig = Join-Path -Path $Root -ChildPath "$WorkspaceName\.west\config"
    $legacyWorkspaceName = "zephyrproject"
    $legacyWestConfig = Join-Path -Path $Root -ChildPath "$legacyWorkspaceName\.west\config"
    if (-not (Test-Path -LiteralPath $preferredWestConfig) -and
        (Test-Path -LiteralPath $legacyWestConfig -PathType Leaf)) {
        $WorkspaceName = $legacyWorkspaceName
        $UsingLegacyWorkspace = $true
    }
}

$WorkspaceDir = Join-Path -Path $Root -ChildPath $WorkspaceName
$VenvDir = Join-Path -Path $Root -ChildPath ".venv"
$script:VenvPython = Join-Path -Path $VenvDir -ChildPath "Scripts\python.exe"
$script:NrfUtilDirectory = $null
$script:SevenZipDirectory = $null
$script:SdkInstallDirectory = $null
$ZephyrDir = Join-Path -Path $WorkspaceDir -ChildPath "zephyr"
$SdkBaseDir = Join-Path -Path $Root -ChildPath "sdk"
$EnvScript = Join-Path -Path $Root -ChildPath "Use-Infuse-IoT.ps1"
$LogPath = Join-Path -Path $Root -ChildPath "install-infuse-iot.log"

New-Item -ItemType Directory -Path $Root, $SdkBaseDir -Force | Out-Null
Start-Transcript -Path $LogPath -Append | Out-Null

try {
    Invoke-Step "Prepare install directories" {
        Write-Information "Install root: $Root" -InformationAction Continue
        Write-Information "Workspace:    $WorkspaceDir" -InformationAction Continue
        Write-Information "SDK base:     $SdkBaseDir" -InformationAction Continue
        Write-Information "Toolchain:    $Toolchain" -InformationAction Continue
        if ($UsingLegacyWorkspace) {
            Write-Information "Reusing legacy workspace directory: $WorkspaceDir" -InformationAction Continue
        }
    }

    if ($InstallHostDependencies) {
        Invoke-Step "Install Windows host dependencies with winget" {
            if (-not (Test-Command winget)) {
                throw "winget was not found. Install App Installer from Microsoft, then rerun this script."
            }

            $packages = @(
                "Kitware.CMake",
                "Ninja-build.Ninja",
                "oss-winget.gperf",
                "Python.Python.3.12",
                "Git.Git",
                "oss-winget.dtc",
                "JernejSimoncic.Wget",
                "7zip.7zip",
                "NordicSemiconductor.nrfutil"
            )

            foreach ($package in $packages) {
                Install-WingetPackage -Id $package
            }

            Update-ProcessPathFromRegistry
        }
    }

    Invoke-Step "Check required host commands" {
        $missing = New-Object System.Collections.Generic.List[string]

        $sevenZipExecutable = Find-7ZipExecutable
        if ($sevenZipExecutable) {
            $script:SevenZipDirectory = Split-Path -Path $sevenZipExecutable -Parent
            Add-DirectoryToProcessPath -Directory $script:SevenZipDirectory
        }
        else {
            $missing.Add("7z")
        }

        $nrfUtilCommand = Get-Command "nrfutil" -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $nrfUtilCommand) {
            $script:NrfUtilDirectory = Split-Path -Path $nrfUtilCommand.Source -Parent
            Add-DirectoryToProcessPath -Directory $script:NrfUtilDirectory
        }
        else {
            $missing.Add("nrfutil")
        }

        foreach ($command in @("git", "cmake", "ninja", "gperf", "dtc", "wget", "py")) {
            if (-not (Test-Command $command)) {
                $missing.Add($command)
            }
        }

        if ($missing.Count -gt 0) {
            $message = "Missing required command(s): {0}. Rerun with -InstallHostDependencies or install the Infuse-IoT host dependencies yourself." -f ($missing -join ", ")
            throw $message
        }

        Invoke-Py312 @("--version")
        Invoke-ExternalCommand -FilePath "git" -Arguments @("--version")
        Invoke-ExternalCommand -FilePath "cmake" -Arguments @("--version")
        Invoke-ExternalCommand -FilePath "ninja" -Arguments @("--version")
        Invoke-ExternalCommand -FilePath "nrfutil" -Arguments @("--version")
        Invoke-ExternalCommand -FilePath $sevenZipExecutable -Arguments @("i") | Out-Null
    }

    Invoke-Step "Install required nRF Util commands" {
        Install-NrfUtilCommand -Name "device"
        Install-NrfUtilCommand -Name "91"
    }

    Invoke-Step "Create or reuse Python virtual environment" {
        if (-not (Test-Path -LiteralPath $script:VenvPython)) {
            New-Item -ItemType Directory -Path $WorkspaceDir -Force | Out-Null
            Invoke-Py312 -m venv $VenvDir
        }

        $venvPythonVersion = Get-PythonMajorMinorVersion -PythonPath $script:VenvPython
        if ($venvPythonVersion -ne "3.12") {
            throw "The existing virtual environment at $VenvDir uses Python $venvPythonVersion; Python 3.12 is required. Remove $VenvDir and rerun this script."
        }

        if (-not (Test-PythonVirtualEnvironment -PythonPath $script:VenvPython)) {
            throw "The Python interpreter at $script:VenvPython is not running in a virtual environment. Remove $VenvDir and rerun this script."
        }

        Invoke-VenvPython -Arguments @("-m", "pip", "install", "--upgrade", "pip")
        Invoke-VenvPython -Arguments @("-m", "pip", "install", "--upgrade", "west")
    }

    Invoke-Step "Create or reuse west workspace" {
        $westConfig = Join-Path -Path $WorkspaceDir -ChildPath ".west\config"
        if (-not (Test-Path -LiteralPath $westConfig)) {
            Invoke-West -Arguments @("init", "-m", $ManifestUrl, $WorkspaceDir)
        }
        else {
            Write-Information "Found existing west workspace at $WorkspaceDir" -InformationAction Continue
        }

        if (-not $SkipWestUpdate) {
            Push-Location $WorkspaceDir
            try {
                if ($FullWestHistory) {
                    Write-Information "Updating west projects with full Git history." -InformationAction Continue
                    Invoke-West -Arguments @("update")
                }
                else {
                    Write-Information "Updating west projects with narrow, depth-one Git fetches." -InformationAction Continue
                    $westUpdateArguments = @("update", "--narrow", "--fetch-opt=--depth=1")
                    Invoke-West -Arguments $westUpdateArguments
                }
            }
            finally {
                Pop-Location
            }
        }
        else {
            Write-Information "Skipping west update because -SkipWestUpdate was supplied." -InformationAction Continue
        }
    }

    Invoke-Step "Install Infuse-IoT build dependencies" {
        if (-not (Test-Path -LiteralPath $ZephyrDir)) {
            throw "The Infuse-IoT Zephyr dependency was not found at $ZephyrDir. Rerun without -SkipWestUpdate or inspect the west workspace."
        }

        Push-Location $WorkspaceDir
        try {
            $requirements = @(Invoke-West -Arguments @("packages", "pip") | ForEach-Object {
                if ($_ -notmatch '^-r\s+(.+)$') {
                    throw "Unexpected output from 'west packages pip': $_"
                }

                $Matches[1]
            })

            $pipArguments = @("-m", "pip", "install")
            foreach ($requirement in $requirements) {
                $pipArguments += @("-r", $requirement)
            }

            Invoke-VenvPython -Arguments $pipArguments
            Invoke-VenvPython -Arguments @("-c", "import patoolib")
            Invoke-West -Arguments @("zephyr-export")
        }
        finally {
            Pop-Location
        }
    }

    Invoke-Step "Install Infuse-IoT Python Tools" {
        Invoke-VenvPython -Arguments @("-m", "pip", "install", "--upgrade", "infuse-iot")
        Invoke-VenvPython -Arguments @("-c", "import infuse_iot")
    }

    Invoke-Step "Install Infuse-IoT ARM compiler toolchain" {
        Push-Location $ZephyrDir
        try {
            $sdkVersionFile = Join-Path -Path $ZephyrDir -ChildPath "SDK_VERSION"
            if (-not (Test-Path -LiteralPath $sdkVersionFile -PathType Leaf)) {
                throw "The required compiler SDK version file was not found: $sdkVersionFile"
            }

            $sdkVersion = (Get-Content -LiteralPath $sdkVersionFile -Raw).Trim()
            $script:SdkInstallDirectory = Join-Path -Path $SdkBaseDir -ChildPath "zephyr-sdk-$sdkVersion"

            if (Test-ZephyrSdkInstallation `
                -SdkDirectory $script:SdkInstallDirectory `
                -ExpectedVersion $sdkVersion `
                -GnuToolchain $Toolchain) {
                $env:ZEPHYR_SDK_INSTALL_DIR = $script:SdkInstallDirectory
                Write-Information "Reusing compiler SDK $sdkVersion at $script:SdkInstallDirectory" -InformationAction Continue
            }
            else {
                $registeredSdkDirectory = Get-RegisteredZephyrSdkDirectory -ExpectedVersion $sdkVersion
                if ($registeredSdkDirectory -and (Test-ZephyrSdkInstallation `
                    -SdkDirectory $registeredSdkDirectory `
                    -ExpectedVersion $sdkVersion `
                    -GnuToolchain $Toolchain)) {
                    $script:SdkInstallDirectory = $registeredSdkDirectory
                    $env:ZEPHYR_SDK_INSTALL_DIR = $script:SdkInstallDirectory
                    Write-Information "Using registered compiler SDK $sdkVersion at $script:SdkInstallDirectory" -InformationAction Continue
                }
                else {
                    if ($registeredSdkDirectory) {
                        $script:SdkInstallDirectory = $registeredSdkDirectory
                        Write-Information "Completing registered compiler SDK $sdkVersion at $script:SdkInstallDirectory" -InformationAction Continue
                    }

                    $env:ZEPHYR_SDK_INSTALL_DIR = $script:SdkInstallDirectory
                    Invoke-West -Arguments @("sdk", "install", "--install-base", $SdkBaseDir, "--gnu-toolchains", $Toolchain)

                    $registeredSdkDirectory = Get-RegisteredZephyrSdkDirectory -ExpectedVersion $sdkVersion
                    if (-not $registeredSdkDirectory) {
                        throw "Zephyr SDK $sdkVersion was installed, but its registered location could not be determined."
                    }

                    $script:SdkInstallDirectory = $registeredSdkDirectory
                    $env:ZEPHYR_SDK_INSTALL_DIR = $script:SdkInstallDirectory
                    Write-Information "Using installed compiler SDK $sdkVersion at $script:SdkInstallDirectory" -InformationAction Continue
                }
            }
        }
        finally {
            Pop-Location
        }
    }

    Invoke-Step "Write environment activation script" {
        $rootLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value $Root
        $workspaceLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value $WorkspaceDir
        $sdkLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value $script:SdkInstallDirectory
        $nrfUtilLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value $script:NrfUtilDirectory
        $sevenZipLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value $script:SevenZipDirectory
        $venvActivateLiteral = ConvertTo-PowerShellSingleQuotedLiteral -Value (Join-Path -Path $VenvDir -ChildPath "Scripts\Activate.ps1")

        $envScriptContent = @"
`$InfuseRoot = $rootLiteral
`$InfuseWorkspace = $workspaceLiteral
`$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
`$env:ZEPHYR_SDK_INSTALL_DIR = $sdkLiteral
`$HostToolDirectories = @($nrfUtilLiteral, $sevenZipLiteral)
foreach (`$HostToolDirectory in `$HostToolDirectories) {
    if ((`$env:Path -split ";") -notcontains `$HostToolDirectory) {
        `$env:Path = "`$HostToolDirectory;`$env:Path"
    }
}
& $venvActivateLiteral
Set-Location -LiteralPath `$InfuseWorkspace
Write-Information "Infuse-IoT environment active." -InformationAction Continue
Write-Information "Workspace: `$InfuseWorkspace" -InformationAction Continue
Write-Information "SDK:       `$env:ZEPHYR_SDK_INSTALL_DIR" -InformationAction Continue
"@
        Set-Content -LiteralPath $EnvScript -Value $envScriptContent -Encoding UTF8
        Write-Information "Wrote $EnvScript" -InformationAction Continue
    }

    if ($PersistUserEnvironment) {
        Invoke-Step "Persist Infuse-IoT toolchain environment variables" {
            [Environment]::SetEnvironmentVariable("ZEPHYR_TOOLCHAIN_VARIANT", "zephyr", "User")
            [Environment]::SetEnvironmentVariable("ZEPHYR_SDK_INSTALL_DIR", $script:SdkInstallDirectory, "User")
            Write-Information "Persisted ZEPHYR_TOOLCHAIN_VARIANT and ZEPHYR_SDK_INSTALL_DIR for the current Windows user." -InformationAction Continue
        }
    }

    Invoke-Step "Verify Infuse-IoT development environment" {
        Push-Location $WorkspaceDir
        try {
            Invoke-West -Arguments @("--version")
            Invoke-West -Arguments @("sdk", "list")
        }
        finally {
            Pop-Location
        }
    }

    Invoke-Step "Verify SEGGER J-Link install" {
        $jLinkExecutable = Find-JLinkExecutable
        Write-Information "" -InformationAction Continue
        if ($jLinkExecutable) {
            Write-Information "SEGGER J-Link detected at $jLinkExecutable" -InformationAction Continue
        }
        else {
            Write-Information "SEGGER J-Link was not detected." -InformationAction Continue
            Write-Information "Download and install it from https://www.segger.com/downloads/jlink/" -InformationAction Continue
        }
    }

    Write-Information "" -InformationAction Continue
    Write-Information "Infuse-IoT ARM development environment setup complete." -InformationAction Continue
    Write-Information "To use it in a new PowerShell session:" -InformationAction Continue
    Write-Information "  powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -File `"$EnvScript`"" -InformationAction Continue
}
finally {
    Stop-Transcript | Out-Null
}
