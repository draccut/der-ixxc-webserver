{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "ixxc-build-env";

  buildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config
    curl.dev
    openssl.dev
    valgrind
  ];

  shellHook = ''
    echo "ixxc Test-Umgebung geladen"
    echo "  gcc:     $(gcc --version | head -n1)"
    echo "  curl:    $(pkg-config --modversion libcurl 2>/dev/null || echo 'nicht gefunden')"
    echo "  openssl: $(pkg-config --modversion openssl 2>/dev/null || echo 'nicht gefunden')"
    
    # Setze Compiler- und Linker-Flags
    export CFLAGS="$CFLAGS $(pkg-config --cflags libcurl openssl) -O2 -Wall -Wextra"
    export LDFLAGS="$LDFLAGS $(pkg-config --libs libcurl openssl) -lpthread"
    
    echo ""
    echo "Zum Kompilieren verwende:"
    echo "    ./build_and_test.sh"
    echo ""
  '';
}

