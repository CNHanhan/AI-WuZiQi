<#
.SYNOPSIS
  AI-WuZiQi 本地数据库一键部署脚本
.DESCRIPTION
  幂等地完成：建库 ai_wuziqi(utf8mb4) -> 导入 schema.sql/init_data.sql -> 验证 6 张表与商品数据 -> 后台启动 Redis 并 PING 验证。
  所有命令均来自实际验证成功的操作，可重复运行。密码写入临时 defaults 文件，避免命令行警告。
.PARAMETER MySqlExe
  mysql.exe 路径，默认 MySQL 8.0 安装位置。
.PARAMETER DbUser / DbPassword
  数据库账号，默认 root/root（服务端直接用 root/root，不建专用用户）。
.PARAMETER RedisDir
  Redis 移植版目录（含 redis-server.exe / redis-cli.exe），默认项目 tools/redis。
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\setup_db.ps1
#>
param(
    [string]$MySqlExe   = "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe",
    [string]$DbHost     = "localhost",
    [string]$DbUser     = "root",
    [string]$DbPassword = "root",
    [string]$RedisDir   = (Join-Path $PSScriptRoot "redis"),
    [int]   $RedisPort  = 6379
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

# ---- 写临时 MySQL 配置文件，避免 "password on command line" 警告 ----
$ini = Join-Path $env:TEMP "aiwzq_my_$PID.cnf"
$iniLines = @(
    "[client]"
    "user=$DbUser"
    "password=$DbPassword"
    "host=$DbHost"
    "default-character-set=utf8mb4"
)
$iniLines | Set-Content -Path $ini -Encoding ASCII
$common = @("--defaults-extra-file=$ini")

function Invoke-Mysql {
    param([string]$SqlFile = '', [string]$Sql = '')
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($SqlFile) {
            # cmd 重定向保留 UTF-8 中文，绕开 PowerShell 管道拆字
            $line = '"' + $MySqlExe + '" ' + ($common | ForEach-Object { '"' + $_ + '"' }) -join ' ' + ' < "' + $SqlFile + '"'
            cmd /c $line 2>$null | Out-Null
        } else {
            & $MySqlExe @common -e $Sql 2>$null
        }
    } finally { $ErrorActionPreference = $oldEap }
}

try {
    Write-Host "========== [1/4] 检查 MySQL =========="
    if (-not (Test-Path $MySqlExe)) { throw "mysql.exe 未找到: $MySqlExe" }
    $ver = (Invoke-Mysql -Sql "SELECT VERSION();" | Select-Object -Last 1)
    Write-Host "MySQL 版本: $ver"

    Write-Host "========== [2/4] 建库 + 导表 + 导数据 =========="
    $schema = Join-Path $ProjectRoot "sql\schema.sql"
    $init   = Join-Path $ProjectRoot "sql\init_data.sql"
    foreach ($f in @($schema, $init)) {
        if (-not (Test-Path $f)) { throw "SQL 脚本缺失: $f" }
        Write-Host "导入: $f"
        Invoke-Mysql -SqlFile $f
    }

    Write-Host "========== [3/4] 验证库表与商品数据 =========="
    Invoke-Mysql -Sql "USE ai_wuziqi; SHOW TABLES; SELECT COUNT(*) AS shop_count FROM t_shop_item;"

    Write-Host "========== [4/4] 启动 Redis 并验证 =========="
    $rdExe = Join-Path $RedisDir "redis-server.exe"
    $rcExe = Join-Path $RedisDir "redis-cli.exe"
    if (-not (Test-Path $rdExe)) { throw "redis-server.exe 未找到: $rdExe （请先下载 tporadowski/redis 解压到 $RedisDir）" }

    $conf = Join-Path $RedisDir "redis.test.conf"
    $confLines = @(
        "bind 127.0.0.1"
        "port $RedisPort"
        'save ""'
        "appendonly no"
        "daemonize no"
        "dir $RedisDir"
    )
    $confLines | Set-Content -Path $conf -Encoding ASCII

    $listener = Get-NetTCPConnection -LocalPort $RedisPort -State Listen -ErrorAction SilentlyContinue
    if ($listener) {
        Write-Host "Redis 端口 $RedisPort 已在监听 (pid=$($listener.OwningProcess))，跳过启动"
    } else {
        $proc = Start-Process -FilePath $rdExe -ArgumentList "`"$conf`"" -WorkingDirectory $RedisDir -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 2
        Write-Host "Redis 已启动 pid=$($proc.Id)"
    }

    $oldEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $pong = (& $rcExe -h 127.0.0.1 -p $RedisPort PING 2>&1).Trim()
    $ErrorActionPreference = $oldEap
    Write-Host "Redis PING => $pong"
    if ($pong -ne 'PONG') { throw "Redis PING 失败: $pong" }

    Write-Host "========== 全部完成：MySQL(ai_wuziqi 6表) + Redis(:$RedisPort PONG) =========="
}
finally {
    # 清理临时配置文件
    if (Test-Path $ini) { Remove-Item $ini -Force -ErrorAction SilentlyContinue }
}
