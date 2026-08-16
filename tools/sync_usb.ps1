$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# 查找可移动磁盘(DriveType=2), 未插入则退出
$usb = Get-CimInstance Win32_LogicalDisk -Filter "DriveType=2" | Select-Object -First 1
if (-not $usb) { Write-Host "[ERROR] 未检测到U盘, 请插入后重试"; exit 1 }

$dest = Join-Path $usb.DeviceID ('RMR_Sync_' + (Get-Date -Format 'yyyyMMdd_HHmm'))
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# 固件 hex 六变体
$hexDir = Join-Path $dest 'hex'
New-Item -ItemType Directory -Force -Path $hexDir | Out-Null
Copy-Item (Join-Path $root 'firmware\RMR\hex\*.hex') $hexDir -Force

# 工具 exe
Copy-Item (Join-Path $root 'rmrdebuger.exe') $dest -Force
Copy-Item (Join-Path $root 'rmrbuildtool.exe') $dest -Force

# fwsec 自测
Copy-Item (Join-Path $root 'firmware\fwsec\build\fwsec_container_test.exe') $dest -Force -ErrorAction SilentlyContinue

# 文档
Copy-Item (Join-Path $root 'README.md') $dest -Force -ErrorAction SilentlyContinue
Get-ChildItem $root -Filter '*.pdf' -Recurse -Depth 2 -ErrorAction SilentlyContinue | Copy-Item -Destination $dest -Force

Write-Host "[OK] 已同步到 $dest"
Get-ChildItem $dest -Recurse -File | Select-Object FullName, Length | Format-Table -AutoSize