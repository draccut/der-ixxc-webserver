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
    export CFLAGS="$CFLAGS $(pkg-config --cflags libcurl openssl) -O2 -Wall -Wextra"
    export LDFLAGS="$LDFLAGS $(pkg-config --libs libcurl openssl) -lpthread"
  '';
}
