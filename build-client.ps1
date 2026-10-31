# ============================================================
# 一键构建客户端（源码来自我们自己的 client-src/）
#
#   .\build-client.ps1 -Qt        Qt Widgets 界面版（新 UI）
#   .\build-client.ps1            旧 SDL+ImGui 界面版（回退用）
#   .\build-client.ps1 -Full      重新配置（依赖变了才需要，很慢）
#   .\build-client.ps1 -UiTests   旧界面测试版（仅 SDL 路径）
#
# 产物：client\airplay-windows.exe + DLL
# ============================================================
param([switch]$Full, [switch]$UiTests, [switch]$Qt)

$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot

$TMP_DIR = Join-Path $PSScriptRoot "build\tmp"
New-Item -ItemType Directory -Force $TMP_DIR | Out-Null
$env:TMP = $TMP_DIR
$env:TEMP = $TMP_DIR

$src    = Join-Path $PSScriptRoot "client-src"

# 编译树（含 vcpkg 依赖）可以留在原工作区；本目录没有 build\ 时自动回退。
$legacyRoot = "C:\Users\ABC\.zcode\workspace\draft-061cf45f-6bb6-45e6-b074-9689fd7ea461\mirror-launcher"
$treeRoot   = Join-Path $PSScriptRoot "build"
if (-not (Test-Path (Join-Path $treeRoot "AirPlay-Windows"))) {
    $treeRoot = Join-Path $legacyRoot "build"
    Write-Host "      编译树回退到: $treeRoot" -ForegroundColor Yellow
}
$tree   = Join-Path $treeRoot "AirPlay-Windows"
$out    = Join-Path $tree "build\out"
$vcpkg  = Join-Path $tree "build\vcpkg"
$dest   = Join-Path $PSScriptRoot "client"

if (-not (Test-Path $tree)) { throw "构建树缺失：$tree（依赖需要先编译过）" }

Write-Host "[1/3] 同步源码 client-src → 构建树..." -ForegroundColor Cyan
robocopy (Join-Path $src "src") (Join-Path $tree "src") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
Copy-Item (Join-Path $src "CMakeLists.txt") $tree -Force
Copy-Item (Join-Path $src "vcpkg.json") $tree -Force
# Fluent-Qt 组件库源码也要同步（CMakeLists 从 third_party/fluent-qt 引用它）
if (Test-Path (Join-Path $src "third_party/fluent-qt")) {
    robocopy (Join-Path $src "third_party/fluent-qt") (Join-Path $tree "third_party/fluent-qt") /MIR /NFL /NDL /NJH /NJS /NP /XD .git | Out-Null
}
Write-Host "      同步完成"

# ${env:ProgramFiles(x86)} is empty in some restricted shells, which used to
# turn this into "\Microsoft Visual Studio\..." → $cmake became $null → the
# build step below was skipped without an error and the stale exe from the
# build tree got copied out as if it were fresh. Always resolve to a real path.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
}
$cmake = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1
    if ($vsPath) {
        $cand = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $cand) { $cmake = $cand }
    }
}
if (-not $cmake) {
    $cand = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $cand) { $cmake = $cand }
}
if (-not $cmake) { $cmake = "cmake" }
if (-not (Get-Command $cmake -ErrorAction SilentlyContinue)) {
    throw "找不到 cmake（vswhere 与已知安装路径均不可用）"
}
Write-Host "      cmake: $cmake"

$testFlag = if ($UiTests) { "ON" } else { "OFF" }
$qtFlag   = if ($Qt) { "ON" } else { "OFF" }
$cacheFile = Join-Path $out "CMakeCache.txt"
$needConfig = $Full -or -not (Test-Path $cacheFile)
if (-not $needConfig) {
    $cached = Select-String -Path $cacheFile -Pattern '^BUILD_UI_TESTS:BOOL=' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $cached -or $cached.Line -notmatch ("=" + $testFlag + "$")) { $needConfig = $true }
    $cachedQt = Select-String -Path $cacheFile -Pattern '^QT_UI:BOOL=' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $cachedQt -or $cachedQt.Line -notmatch ("=" + $qtFlag + "$")) { $needConfig = $true }
}
if ($needConfig) {
    Write-Host "[2/3] 重新配置（首次 / -Full / UI 开关变化）..." -ForegroundColor Cyan
    $qtArgs = @()
    if ($Qt) {
        # Qt 优先找本目录 build\Qt（aqtinstall 装的），回退编译树旁的副本
        $qtRoot = Join-Path $PSScriptRoot "build\Qt\6.8.3\msvc2022_64"
        if (-not (Test-Path (Join-Path $qtRoot "bin\qmake.exe")))
            { $qtRoot = Join-Path $treeRoot "Qt\6.8.3\msvc2022_64" }
        if (-not (Test-Path (Join-Path $qtRoot "bin\qmake.exe"))) {
            throw "Qt 未安装：$qtRoot 缺失（先跑 aqtinstall 安装步骤）"
        }
        $qtArgs += "-DCMAKE_PREFIX_PATH=$qtRoot"
    }
    & $cmake -S $tree -B $out `
        -DCMAKE_TOOLCHAIN_FILE="$vcpkg\scripts\buildsystems\vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-release `
        -DVCPKG_HOST_TRIPLET=x64-windows-release `
        -DVCPKG_OVERLAY_PORTS="$treeRoot\overlay-ports" `
        -DBUILD_UI_TESTS="$testFlag" `
        -DQT_UI="$qtFlag" `
        @qtArgs `
        -DIMGUI_TEST_ENGINE_ROOT="$src\third_party\imgui_test_engine" `
        -G "Visual Studio 18 2026" -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }
} else {
    Write-Host "[2/3] 复用已有配置（增量）" -ForegroundColor Cyan
}

Write-Host "[3/3] 编译 Release..." -ForegroundColor Cyan
& $cmake --build $out --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    # The VC file tracker (TrackedVCToolTask) builds a case-sensitive env
    # dictionary for the CL child process. In shells whose environment holds
    # both "Path" and "PATH" it throws MSB6001 ("已添加项...关键字 Path/PATH"),
    # which surfaces as "CL.exe 命令行开关无效" and aborts the build. Retrying
    # with tracking off sidesteps it; normal shells never take this branch.
    Write-Host "      常规构建失败，关闭文件跟踪后重试..." -ForegroundColor Yellow
    & $cmake --build $out --config Release --parallel -- /p:TrackFileAccess=false /p:UseMultiToolTask=false
}
if ($LASTEXITCODE -ne 0) {
    # Restricted / sandboxed shells can still fail even here because MSBuild
    # initialises the CL tracker on up-to-date checks too. Never fall through
    # silently: the exe we are about to ship must be newer than every source
    # file, otherwise the build genuinely did not happen and copying it out
    # would ship a stale binary that looks like a successful build.
    $built = Get-ChildItem $out -Recurse -Filter "airplay-windows.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $newest = Get-ChildItem (Join-Path $src "src") -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($built -and $newest -and $built.LastWriteTime -ge $newest.LastWriteTime) {
        Write-Host ("      构建器报错，但产物比所有源码新（exe {0} / 最新源码 {1}），继续" -f `
            $built.LastWriteTime, $newest.LastWriteTime) -ForegroundColor Yellow
    } else {
        throw "编译失败（产物未更新，拒绝使用旧 exe）"
    }
}

New-Item -ItemType Directory -Force $dest | Out-Null
$exe = Get-ChildItem $out -Recurse -Filter "airplay-windows.exe" | Select-Object -First 1
Copy-Item $exe.FullName $dest -Force
Get-ChildItem $exe.DirectoryName -Filter *.dll | Copy-Item -Destination $dest -Force
Copy-Item (Join-Path $src "src/app.ico") $dest -Force

if ($Qt) {
    # Qt DLL 部署（platforms/styles 插件 + Qt6Core/Gui/Widgets/OpenGL）
    $qtBin = Join-Path $PSScriptRoot "build\Qt\6.8.3\msvc2022_64\bin"
    if (-not (Test-Path (Join-Path $qtBin "windeployqt.exe")))
        { $qtBin = Join-Path $treeRoot "Qt\6.8.3\msvc2022_64\bin" }
    $windeployqt = Join-Path $qtBin "windeployqt.exe"
    if (Test-Path $windeployqt) {
        & $windeployqt --no-translations --no-system-d3d-compiler `
            --no-opengl-sw --no-quick-import (Join-Path $dest "airplay-windows.exe") | Out-Null
        Write-Host "      windeployqt 完成"
    } else {
        Write-Host "      windeployqt 未找到，Qt DLL 需手动复制" -ForegroundColor Yellow
    }
}
# 设备边框素材（运行时从 exe 旁 assets\frame 加载，PNG 由程序内 ffmpeg 解码）
# robocopy /MIR instead of Remove-Item + Copy-Item: 122 PNGs trip host
# bulk-delete guards, and /MIR keeps the two trees identical anyway.
$assetsSrc = Join-Path $src "src\assets"
if (Test-Path $assetsSrc) {
    robocopy $assetsSrc (Join-Path $dest "assets") /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
}

# 自动签名：本地编译的 exe 没有云端信誉，不签名则 SmartScreen 每次启动
# 都弹「Windows 已保护你的电脑」。证书由 tools\trust-and-sign.ps1 一次性
# 创建并导入受信任存储（缺证书只警告，不阻塞构建）。
$signCert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq "CN=AirPlay Mirror Client" } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if ($signCert) {
    $sig = Set-AuthenticodeSignature -FilePath (Join-Path $dest "airplay-windows.exe") `
        -Certificate $signCert -HashAlgorithm SHA256
    if ($sig.Status -ne "Valid") {
        Write-Host ("      签名状态异常: " + $sig.Status) -ForegroundColor Yellow
    }
} else {
    Write-Host "      （未找到签名证书，跳过签名；运行 tools\trust-and-sign.ps1 一次性配置）" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "产物: $dest" -ForegroundColor Green
Get-ChildItem $dest | Measure-Object -Property Length -Sum |
    ForEach-Object { Write-Host ("      {0} 个文件，{1} MB" -f $_.Count, [math]::Round($_.Sum/1MB,1)) }
