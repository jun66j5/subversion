$LocalAppData = $Env:LocalAppData
$ProgramData = $Env:ProgramData
$workspace = $Env:GITHUB_WORKSPACE
$arch = $Env:MATRIX_ARCH
$pythonLocation = $Env:pythonLocation
$vcpkg_root = 'C:\vcpkg'
$vcpkg_triplet = "$arch-windows"
$vcpkg_dir = "$vcpkg_root\installed\$vcpkg_triplet"
$sqlite_name = 'sqlite-amalgamation-3081101'
$sqlite_url = "https://www.sqlite.org/2015/$sqlite_name.zip"
$sqlite_arc = "$workspace\$sqlite_name.zip"
$java_home = $Env:JAVA_HOME
$junit_url = 'https://search.maven.org/remotecontent?filepath=junit/junit/4.13.2/junit-4.13.2.jar'
$junit_file = "$workspace\junit4.jar"
$swig3_name = 'swigwin-3.0.12'
$swig3_url = "https://sourceforge.net/projects/swig/files/swigwin/$swig3_name/$swig3_name.zip/download"
$swig3_arc = "$workspace\$swig3_name.zip"
$python = ($pythonLocation -eq $null) ? 'python.exe' : "$pythonLocation\python.exe"

& vcpkg install "--triplet=$vcpkg_triplet" apr apr-util zlib gettext
Invoke-WebRequest -Uri $sqlite_url -OutFile $sqlite_arc
Expand-Archive -LiteralPath $sqlite_arc -DestinationPath $workspace

switch -Exact ($args[0]) {
    'core' {
        $genmake_opts = @()
        $build_targets = '__MORE__'
        $test_targets = @('--parallel')
    }
    'bindings' {
        $genmake_opts = @("--with-swig=$ProgramData\chocolatey\bin",
                          "--with-py3c=$workspace\py3c",
                          "--with-jdk=$java_home",
                          "--with-junit=$junit_file")
        $build_targets = '__ALL__;__SWIG_PYTHON__;__SWIG_PERL__;__JAVAHL__;__JAVAHL_TESTS__'
        $test_targets = @('--swig=python', '--swig=perl', '--javahl')
        & choco install -y --no-progress swig
        Invoke-WebRequest -Uri $junit_url -OutFile $junit_file
    }
}

New-Item -Force -ItemType Directory -Path "subversion\bindings\swig\proxy"
$rc = 0
& $python gen-make.py `
          --vsnet-version=2019 `
          --enable-nls `
          "--with-apr=$vcpkg_dir" `
          "--with-apr-util=$vcpkg_dir" `
          "--with-zlib=$vcpkg_dir" `
          "--with-libintl=$vcpkg_dir" `
          "--with-sqlite=$workspace\$sqlite_name" `
          $genmake_opts
& msbuild subversion_vcnet.sln `
          /nologo /v:q /m:3 /fl /flp:logfile=msbuild.log `
          "/t:$build_targets" `
          "/p:Configuration=Release;Platform=$arch"
if (-not $?) {
    Write-Warning "Exited with $LASTEXITCODE"
    $rc = 1
}
$Env:PATH = "$vcpkg_dir\bin;$($Env:PATH)"
foreach ($test_args in $test_targets) {
    & $python win-tests.py -cr $test_args
    if (-not $?) {
        Write-Warning "Exited with $LASTEXITCODE"
        $rc = 1
    }
}
$host.SetShouldExit($rc)
