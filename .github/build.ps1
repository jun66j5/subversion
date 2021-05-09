$LocalAppData = $env:LocalAppData
$ProgramData = $env:ProgramData
$workspace = $env:GITHUB_WORKSPACE
$arch = $env:MATRIX_ARCH
$pythonLocation = $env:pythonLocation
$vcpkg_root = 'C:\vcpkg'
$vcpkg_triplet = "$arch-windows"
$vcpkg_dir = "$vcpkg_root\installed\$vcpkg_triplet"
$sqlite_name = 'sqlite-amalgamation-3081101'
$sqlite_url = "https://www.sqlite.org/2015/$sqlite_name.zip"
$sqlite_arc = "$workspace\$sqlite_name.zip"
$java_home = $env:JAVA_HOME
$junit_url = 'https://search.maven.org/remotecontent?filepath=junit/junit/4.13.2/junit-4.13.2.jar'
$junit_file = "$workspace\junit4.jar"
$python = "$pythonLocation\python.exe"

& choco install -y --no-progress swig
& vcpkg install "--triplet=$vcpkg_triplet" apr apr-util zlib gettext
Invoke-WebRequest -Uri $sqlite_url -OutFile $sqlite_arc
Expand-Archive -LiteralPath $sqlite_arc -DestinationPath "$workspace"
Invoke-WebRequest -Uri $junit_url -OutFile $junit_file

New-Item -Force -ItemType Directory `
         -Path "subversion\bindings\swig\proxy"
$rc = 0
& $python gen-make.py `
          --vsnet-version=2019 `
          --enable-nls `
          "--with-apr=$vcpkg_dir" `
          "--with-apr-util=$vcpkg_dir" `
          "--with-zlib=$vcpkg_dir" `
          "--with-libintl=$vcpkg_dir" `
          "--with-sqlite=$workspace\$sqlite_name" `
          "--with-swig=$ProgramData\chocolatey\bin" `
          "--with-py3c=$workspace\py3c" `
          "--with-jdk=$java_home" `
          "--with-junit=$junit_file"
& msbuild subversion_vcnet.sln `
          /nologo /v:q /m:3 /fl /flp:logfile=msbuild.log `
          "/t:__MORE__;__SWIG_PYTHON__;__SWIG_PERL__;__JAVAHL__;__JAVAHL_TESTS__" `
          "/p:Configuration=Release;Platform=$arch"
if (-not $?) {
    Write-Warning "Exited with $LASTEXITCODE"
    $rc = 1
}
$env:PATH = "$vcpkg_dir\bin;$($env:PATH)"
foreach ($args in @('--parallel',
                    '--swig=python',
                    '--swig=perl',
                    '--javahl'))
{
    & $python win-tests.py -cr $args
    if (-not $?) {
        Write-Warning "Exited with $LASTEXITCODE"
        $rc = 1
    }
}
$host.SetShouldExit($rc)
