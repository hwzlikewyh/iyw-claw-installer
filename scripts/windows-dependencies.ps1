param([Parameter(Mandatory=$true)][string]$WxSource,
      [ValidateSet('Win32','x64')][string]$Architecture='Win32')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$root=$root.Replace('\','/')
$WxSource=$WxSource.Replace('\','/')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools are required'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$major=[int]((& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Split('.')[0])
$toolset=if($major -ge 18){'v145'}else{'v143'}
$year=if($major -ge 18){'vs2026'}else{'vs2022'}
$deps=Join-Path $root ".cache/windows-deps-$Architecture"
$deps=$deps.Replace('\','/')
New-Item -ItemType Directory -Force -Path $deps | Out-Null
$sodium=Join-Path $root '.cache/libsodium-1.0.22'
& $msbuild "$sodium/builds/msvc/$year/libsodium/libsodium.vcxproj" /m:3 /nologo /verbosity:minimal /p:Configuration=ReleaseLIB "/p:Platform=$Architecture" "/p:PlatformToolset=$toolset"
if($LASTEXITCODE -ne 0){throw 'libsodium static build failed'}
$lib=Get-ChildItem -LiteralPath "$sodium/bin" -Filter libsodium.lib -Recurse | Where-Object {$_.FullName -like "*$Architecture*Release*$toolset*static*"} | Select-Object -First 1
if(-not $lib){throw 'libsodium static library missing'}
Copy-Item -LiteralPath $lib.FullName -Destination "$deps/sodium.lib" -Force
$zlib=Join-Path $deps 'zlib'
$zlib=$zlib.Replace('\','/')
& $cmake -S "$WxSource/src/zlib" -B $zlib -A $Architecture '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded' -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
if($LASTEXITCODE -ne 0){throw 'zlib configure failed'}
& $cmake --build $zlib --config Release --target zlibstatic --parallel 3
if($LASTEXITCODE -ne 0){throw 'zlib build failed'}
Copy-Item -LiteralPath "$WxSource/src/zlib/zlib.h" -Destination "$zlib/zlib.h" -Force
$options=@()
foreach($feature in 'OPENSSL','MBEDTLS','NETTLE','LIBB2','LZ4','LZO','LZMA','ZSTD','BZip2','LIBXML2','EXPAT','BSDXML','WIN32_XMLLITE','PCREPOSIX','PCRE2POSIX','ICONV','ACL','XATTR','TAR','CPIO','CAT','UNZIP','TEST','INSTALL'){$options+="-DENABLE_$feature=OFF"}
& $cmake -S "$root/.cache/libarchive-3.8.11" -B "$deps/archive" -A $Architecture -DBUILD_SHARED_LIBS=OFF '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded' -DENABLE_ZLIB=ON "-DZLIB_INCLUDE_DIR=$zlib" "-DZLIB_LIBRARY=$zlib/Release/zlibstatic.lib" @options
if($LASTEXITCODE -ne 0){throw 'libarchive configure failed'}
& $cmake --build "$deps/archive" --config Release --target archive_static --parallel 3
if($LASTEXITCODE -ne 0){throw 'libarchive build failed'}
