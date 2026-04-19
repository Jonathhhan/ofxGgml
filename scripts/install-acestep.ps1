param(
    [string]$Branch = "",
    [string]$RepositoryUrl = "https://github.com/ServeurpersoCom/acestep.cpp.git",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [int]$Jobs = 0,
    [switch]$Refetch,
    [switch]$Clean,
    [switch]$KeepArtifacts,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Get-CommandPathOrNull {
    param([string]$Name)
    try {
        return (Get-Command $Name -ErrorAction Stop).Source
    } catch {
        return $null
    }
}

function Invoke-LoggedCommand {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    if ($DryRun) {
        Write-Host "$Executable $($Arguments -join ' ')"
        return
    }

    & $Executable @Arguments | Out-Host
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $Executable $($Arguments -join ' ')"
    }
}

function Get-RemoteDefaultBranchOrNull {
    param(
        [string]$GitExecutable,
        [string]$RemoteUrl
    )

    if ($DryRun) {
        return $null
    }

    try {
        $output = & $GitExecutable 'ls-remote' '--symref' $RemoteUrl 'HEAD' 2>$null
        if ($LASTEXITCODE -ne 0) {
            return $null
        }

        foreach ($line in $output) {
            if ($line -match '^ref:\s+refs/heads/([^\s]+)\s+HEAD$') {
                return $Matches[1]
            }
        }
    } catch {
        return $null
    }

    return $null
}

function Get-BranchCandidates {
    param(
        [string]$GitExecutable,
        [string]$RemoteUrl,
        [string]$RequestedBranch
    )

    $candidates = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($RequestedBranch)) {
        $candidates.Add($RequestedBranch)
        return $candidates
    }

    $defaultBranch = Get-RemoteDefaultBranchOrNull -GitExecutable $GitExecutable -RemoteUrl $RemoteUrl
    if (-not [string]::IsNullOrWhiteSpace($defaultBranch)) {
        $candidates.Add($defaultBranch)
    }

    foreach ($candidate in @('main', 'master', 'dev', 'trunk')) {
        if (-not $candidates.Contains($candidate)) {
            $candidates.Add($candidate)
        }
    }

    return $candidates
}

function Clone-RepositoryWithBranchFallback {
    param(
        [string]$GitExecutable,
        [string]$RemoteUrl,
        [string]$RequestedBranch,
        [string]$CheckoutDir
    )

    $attemptErrors = New-Object System.Collections.Generic.List[string]
    $branchCandidates = Get-BranchCandidates -GitExecutable $GitExecutable -RemoteUrl $RemoteUrl -RequestedBranch $RequestedBranch

    foreach ($candidateBranch in $branchCandidates) {
        try {
            Write-Step "Cloning AceStep source (branch '$candidateBranch')"
            Invoke-LoggedCommand $GitExecutable @(
                'clone',
                '--depth', '1',
                '--branch', $candidateBranch,
                '--recurse-submodules',
                '--shallow-submodules',
                $RemoteUrl,
                $CheckoutDir
            )
            return $candidateBranch
        } catch {
            $attemptErrors.Add($_.Exception.Message)
            if (-not $DryRun -and (Test-Path -LiteralPath $CheckoutDir)) {
                Remove-Item -LiteralPath $CheckoutDir -Recurse -Force
            }
        }
    }

    $branchList = $branchCandidates -join ', '
    $errorList = $attemptErrors -join [Environment]::NewLine
    throw "Failed to clone AceStep. Tried branches: $branchList`n$errorList"
}

function Resolve-ExistingBranch {
    param(
        [string]$GitExecutable,
        [string]$RemoteUrl,
        [string]$RequestedBranch,
        [string]$CheckoutDir
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedBranch)) {
        return $RequestedBranch
    }

    if (-not $DryRun) {
        try {
            $currentBranch = (& $GitExecutable '-C' $CheckoutDir 'rev-parse' '--abbrev-ref' 'HEAD').Trim()
            if ($LASTEXITCODE -eq 0 -and
                -not [string]::IsNullOrWhiteSpace($currentBranch) -and
                $currentBranch -ne 'HEAD') {
                return $currentBranch
            }
        } catch {
        }
    }

    $defaultBranch = Get-RemoteDefaultBranchOrNull -GitExecutable $GitExecutable -RemoteUrl $RemoteUrl
    if (-not [string]::IsNullOrWhiteSpace($defaultBranch)) {
        return $defaultBranch
    }

    return 'main'
}

function Update-RepositorySubmodules {
    param(
        [string]$GitExecutable,
        [string]$CheckoutDir
    )

    Write-Step "Updating AceStep submodules"
    Invoke-LoggedCommand $GitExecutable @('-C', $CheckoutDir, 'submodule', 'sync', '--recursive')
    Invoke-LoggedCommand $GitExecutable @(
        '-C', $CheckoutDir,
        'submodule', 'update',
        '--init',
        '--recursive',
        '--depth', '1'
    )
}

function Get-DefaultRuntimeRoot {
    param([string]$AddonRoot)

    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not [string]::IsNullOrWhiteSpace($localAppData)) {
        return (Join-Path $localAppData 'ofxGgml\acestep')
    }
    return (Join-Path $AddonRoot '.runtime\acestep')
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path
$defaultRuntimeRoot = Get-DefaultRuntimeRoot -AddonRoot $addonRoot

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $defaultRuntimeRoot 'source'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $defaultRuntimeRoot 'build'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'libs\acestep\bin'
}
if ($Jobs -le 0) {
    $Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
}

$git = Get-CommandPathOrNull 'git.exe'
$cmake = Get-CommandPathOrNull 'cmake.exe'
if (-not $git) {
    throw "git.exe was not found in PATH."
}
if (-not $cmake) {
    throw "cmake.exe was not found in PATH."
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Step "Removing previous AceStep build directory"
    if (-not $DryRun) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}

if ($Refetch -and (Test-Path -LiteralPath $SourceDir)) {
    Write-Step "Removing existing AceStep source checkout"
    if (-not $DryRun) {
        Remove-Item -LiteralPath $SourceDir -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    $resolvedBranch = Clone-RepositoryWithBranchFallback -GitExecutable $git -RemoteUrl $RepositoryUrl -RequestedBranch $Branch -CheckoutDir $SourceDir
} else {
    Write-Step "Updating existing AceStep source checkout"
    $resolvedBranch = Resolve-ExistingBranch -GitExecutable $git -RemoteUrl $RepositoryUrl -RequestedBranch $Branch -CheckoutDir $SourceDir
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'fetch', 'origin', $resolvedBranch, '--depth', '1')
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'checkout', $resolvedBranch)
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'pull', '--ff-only', 'origin', $resolvedBranch)
}

Update-RepositorySubmodules -GitExecutable $git -CheckoutDir $SourceDir

$cmakeListsPath = Join-Path $SourceDir 'CMakeLists.txt'
if (-not $DryRun) {
    if (-not (Test-Path -LiteralPath $cmakeListsPath)) {
        throw "The AceStep checkout does not contain CMakeLists.txt at $cmakeListsPath. The repository layout may have changed, or the checkout did not complete successfully."
    }

    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
}

Write-Step "Configuring AceStep"
Invoke-LoggedCommand $cmake @(
    '-S', $SourceDir,
    '-B', $BuildDir
)

Write-Step "Building AceStep"
Invoke-LoggedCommand $cmake @(
    '--build', $BuildDir,
    '--config', 'Release',
    '--parallel', $Jobs
)

Write-Step "Installing AceStep runtime artifacts into $InstallDir"
if (-not $DryRun) {
    $artifactPatterns = @('*.exe', '*.dll')
    $copied = 0
    foreach ($pattern in $artifactPatterns) {
        Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter $pattern |
            Where-Object {
                $_.FullName -match '\\(Release|bin)\\' -or $_.DirectoryName -eq $BuildDir
            } |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
                $copied++
            }
    }

    if ($copied -eq 0) {
        Write-Warning "No AceStep runtime artifacts were found under $BuildDir. The project may build different target names on this platform."
    }

    if (-not $KeepArtifacts) {
        Write-Step "Pruning AceStep source/build artifacts"
        if ((Test-Path -LiteralPath $BuildDir) -and ($BuildDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
        }
        if ((Test-Path -LiteralPath $SourceDir) -and ($SourceDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $SourceDir -Recurse -Force
        }
    }
}

Write-Host ""
Write-Host "AceStep install helper complete."
Write-Host "  source:  $SourceDir"
Write-Host "  build:   $BuildDir"
Write-Host "  install: $InstallDir"
if (-not [string]::IsNullOrWhiteSpace($resolvedBranch)) {
    Write-Host "  branch:  $resolvedBranch"
}
if (-not $KeepArtifacts) {
    Write-Host "  cache:   pruned after install"
}
Write-Host "  server url default in ofxGgml: http://127.0.0.1:8085"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Start the AceStep server from the installed binaries or the build tree."
Write-Host "  2. In the GUI example, open Vision -> AceStep Music Backend."
Write-Host "  3. Click 'Check AceStep server' and confirm the configured URL is reachable."
