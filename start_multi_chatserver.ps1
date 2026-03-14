$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$debugDir = Join-Path $root "x64\\Debug"
$exe = Join-Path $debugDir "ChatServer.exe"
$config1 = Join-Path $root "config_8090.ini"
$config2 = Join-Path $root "config_8091.ini"

function Assert-FileExists {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw "Missing ${Label}: ${Path}"
    }
}

function Assert-ConfigLooksValid {
    param(
        [string]$Path
    )

    $content = Get-Content -Path $Path -Raw
    if ($content -notmatch "\[SelfServer\]") {
        throw "Missing [SelfServer] section: ${Path}"
    }
    if ($content -notmatch "Port\s*=") {
        throw "Missing SelfServer.Port: ${Path}"
    }
    if ($content -notmatch "GrpcPort\s*=") {
        throw "Missing SelfServer.GrpcPort: ${Path}"
    }
}

Assert-FileExists -Path $exe -Label "ChatServer.exe"
Assert-FileExists -Path $config1 -Label "config file"
Assert-FileExists -Path $config2 -Label "config file"

Assert-ConfigLooksValid -Path $config1
Assert-ConfigLooksValid -Path $config2

Start-Process -FilePath $exe `
    -ArgumentList @($config1) `
    -WorkingDirectory $debugDir

Start-Process -FilePath $exe `
    -ArgumentList @($config2) `
    -WorkingDirectory $debugDir

Write-Host "Started two ChatServer instances:"
Write-Host "  127.0.0.1:8090 (gRPC 9090)"
Write-Host "  127.0.0.1:8091 (gRPC 9091)"
