{
  pkgs,
  nythera,
}:
pkgs.mkShell {
  inputsFrom = [ nythera ];

  nativeBuildInputs = with pkgs; [
    # Workflow & Hooks
    just
    lefthook

    # Build tools invoked directly by just recipes
    meson
    ninja
    pkg-config
    wayland-scanner

    # Formatting (required by justfile)
    llvmPackages_22.clang-tools
    llvmPackages_22.libclang
    gnugrep
    gnused
    findutils
    python3

    # Debugging
    gdb
  ];

  shellHook = ''
    # Point to local assets so binaries find resources without installation
    export NYTHERA_ASSETS_DIR="$PWD/assets"

    echo " Nythera dev-shell | 'just --list' to see available tasks"
  '';
}
