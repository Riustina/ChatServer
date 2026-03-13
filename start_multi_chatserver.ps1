$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root "x64\Debug\ChatServer.exe"
$config1 = Join-Path $root "config_8090.ini"
$config2 = Join-Path $root "config_8091.ini"

if (-not (Test-Path $exe)) {
    throw "未找到 ChatServer.exe: $exe"
}

if (-not (Test-Path $config1)) {
    throw "未找到配置文件: $config1"
}

if (-not (Test-Path $config2)) {
    throw "未找到配置文件: $config2"
}

Start-Process -FilePath $exe -ArgumentList $config1 -WorkingDirectory $root
Start-Process -FilePath $exe -ArgumentList $config2 -WorkingDirectory $root

Write-Host "已启动两个 ChatServer 实例:"
Write-Host "  127.0.0.1:8090"
Write-Host "  127.0.0.1:8091"
