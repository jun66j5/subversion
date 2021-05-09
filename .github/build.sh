#! /bin/bash

set -exo pipefail

target="$1"

case "$MATRIX_OS" in
ubuntu-*)
    pkgs="libapr1-dev libaprutil1-dev liblz4-dev libutf8proc-dev"
    case  "$target" in
    swig-py)
        case "$MATRIX_PYVER" in
            2.*) swig=swig3.0 ;;
            *)   swig=swig4.0 ;;
        esac
        ;;
    swig-*)
        swig=swig4.0
        ;;
    all|install)
        pkgs="$pkgs apache2-dev libserf-dev"
        ;;
    esac
    test -n "$swig" && pkgs="$pkgs $swig"
    sudo apt-get update -qq
    sudo apt-get install -qq -y $pkgs
    with_apr=/usr/bin/apr-1-config
    with_apr_util=/usr/bin/apu-1-config
    with_swig="/usr/bin/$swig"
    with_apxs=/usr/bin/apxs2
    parallel=3
    ;;
macos-*)
    pkgs="apr apr-util lz4 utf8proc $swig"
    case  "$target" in
    swig-py)
        case "$MATRIX_PYVER" in
            2.*) swig=swig@3 ;;
            *)   swig=swig   ;;
        esac
        ;;
    swig-*)
        swig=swig
        ;;
    all|install)
        pkgs="$pkgs httpd"
        ;;
    esac
    test -n "$swig" && pkgs="$pkgs $swig"
    brew update
    brew outdated $pkgs || brew upgrade $pkgs || :
    brew install $pkgs
    with_apr="$(brew --prefix apr)/libexec/bin/apr-1-config"
    with_apr_util="$(brew --prefix apr-util)/libexec/bin/apu-1-config"
    with_swig="$(brew --prefix "$swig")/bin/swig"
    with_apxs="$(brew --prefix httpd)/bin/apxs"
    parallel=4
    ;;
esac

cflags=
if [ "$target" = install ]; then
    ldflags="-Wl,-rpath,$HOME/svn/lib"
else
    ldflags="-L$HOME/svn/lib -Wl,-rpath,$HOME/svn/lib"
fi

case "$target" in
swig-py)
    opt_swig="--with-swig=$with_swig"
    opt_py3c="--with-py3c=$GITHUB_WORKSPACE/py3c"
    opt_apxs="--without-apxs"
    opt_javahl="--disable-javahl"
    opt_jdk="--without-jdk"
    opt_junit="--without-junit"
    use_installed_libs=y
    ;;
swig-rb)
    opt_swig="--with-swig=$with_swig"
    opt_py3c="--without-py3c"
    opt_apxs="--without-apxs"
    opt_javahl="--disable-javahl"
    opt_jdk="--without-jdk"
    opt_junit="--without-junit"
    use_installed_libs=y
    case "$MATRIX_OS" in
    macos-*)
        cflags="-fdeclspec"
        ldflags="$ldflags -L$(ruby -rrbconfig -W0 -e "print RbConfig::CONFIG['libdir']")"
        ;;
    esac
    ;;
swig-pl)
    opt_swig="--with-swig=$with_swig"
    opt_py3c="--without-py3c"
    opt_apxs="--without-apxs"
    opt_javahl="--disable-javahl"
    opt_jdk="--without-jdk"
    opt_junit="--without-junit"
    use_installed_libs=y
    ;;
javahl)
    opt_swig="--without-swig"
    opt_py3c="--without-py3c"
    opt_apxs="--without-apxs"
    opt_javahl="--enable-javahl"
    opt_jdk="--with-jdk=$JAVA_HOME"
    opt_junit="--with-junit=$PWD/junit4.jar"
    use_installed_libs=y
    ;;
all|install)
    opt_swig="--without-swig"
    opt_py3c="--without-py3c"
    opt_apxs="--with-apxs=$with_apxs"
    opt_javahl="--disable-javahl"
    opt_jdk="--without-jdk"
    opt_junit="--without-junit"
    use_installed_libs=n
    ;;
esac

mkdir -p subversion/bindings/swig/proxy || :
/bin/sh autogen.sh

if [ "$use_installed_libs" = y ]; then
    PATH="$HOME/svn/bin:$PATH"
    export PATH
    installed_libs="$(cd "$HOME/svn/lib" && \
                      echo libsvn_*.la | \
                      sed -e 's/-[^-]*\.la//g; s/ /,/g')"
    if [ "$target" = javahl ]; then
        curl -L -o junit4.jar 'https://search.maven.org/remotecontent?filepath=junit/junit/4.13.2/junit-4.13.2.jar'
        python gen-make.py "$opt_jdk" "$opt_junit" --installed-libs="$installed_libs"
    else
        python gen-make.py --installed-libs="$installed_libs"
    fi
fi

./configure --prefix="$HOME/svn" \
            --with-apr="$with_apr" --with-apr-util="$with_apr_util" \
            "$opt_swig" "$opt_py3c" "$opt_apxs" "$opt_javahl" "$opt_jdk" \
            "$opt_junit" \
            --without-doxygen --without-berkeley-db --without-gpg-agent \
            --without-gnome-keyring --without-kwallet \
            CFLAGS="$cflags" LDFLAGS="$ldflags"

case "$target" in
install)
    make -j"$parallel" all
    make install
    ;;
all)
    make -j"$parallel" all
    make check PARALLEL="$parallel"
    ;;
swig-*)
    make -j"$parallel" "$target"
    make check-"$target"
    ;;
javahl)
    make javahl check-javahl
    ;;
esac
