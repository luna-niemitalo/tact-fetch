# ~/dev/tact-fetch/nix/flake.nix
{
  description = "tact-fetch: polite CDN fetcher for CASC files a local WoW install never downloaded";

  inputs = {
    pins.url = "path:/home/luna/nix/pins";
    nixpkgs.follows = "pins/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    # CascLib is vendored as a git submodule under vendor/CascLib for local
    # dev (plain `cmake` builds against the on-disk checkout). A plain `nix
    # build` of this flake's own source wouldn't see that submodule content
    # (`self` only exposes git-tracked files; a submodule's files aren't part
    # of the parent repo's `git ls-files`, only the gitlink entry is) -- so
    # CascLib is fetched as its own flake input instead and spliced into
    # vendor/CascLib during the build. Same mechanism as casc-tool's own
    # nix/flake.nix; see that repo's README "Why this needed more than 'just
    # cmake'" section for the full rationale.
    casclib = {
      url = "github:ladislav-zezula/CascLib";
      flake = false;
    };
  };

  outputs =
    inputs@{
      self,
      pins,
      nixpkgs,
      flake-utils,
      casclib,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        projectRoot = ../.;

        # Only what the packaged build actually needs: CMakeLists.txt + src/.
        # Deliberately excludes tests/, build/, .git, vendor/ -- none of
        # those should ever end up copied into the Nix store for this.
        appSource = lib.fileset.toSource {
          root = projectRoot;
          fileset = lib.fileset.unions [
            (projectRoot + "/CMakeLists.txt")
            (projectRoot + "/src")
          ];
        };

        # curl provides the HTTP/2 client this project's own CDN fetcher
        # builds on (see DESIGN.md #9) -- CascLib is vendored strictly for
        # local manifest reads, never for the network fetch itself.
        cpp = with pkgs; [
          cmake
          ninja
          pkg-config
          gcc
          gdb
          ccache
          doctest
          curl
        ];
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "tact-fetch";
          version = "0.1.0";
          src = appSource;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];

          buildInputs = [ pkgs.curl ];

          # vendor/CascLib doesn't exist in appSource (see above) -- drop in
          # the fetched CascLib source right after unpacking, before cmake
          # configure runs and hits add_subdirectory(vendor/CascLib).
          postUnpack = ''
            mkdir -p "$sourceRoot/vendor/CascLib"
            cp -r ${casclib}/. "$sourceRoot/vendor/CascLib/"
            chmod -R u+w "$sourceRoot/vendor/CascLib"
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp tact-fetch $out/bin/
          '';
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/tact-fetch";
        };

        devShells.default = pkgs.mkShell {
          packages = cpp;

          CCACHE_DIR = "/media/luna/cache/ccache";

          shellHook = ''
            echo "tact-fetch dev shell"
            echo "  cmake:   $(cmake --version | head -n1)"
            echo "  ccache:  $CCACHE_DIR"
            echo "  doctest: available via find_package(doctest)"
            echo "  curl:    $(curl --version | head -n1)"
            echo "  vendor/CascLib: $(git -C vendor/CascLib describe --always 2>/dev/null || echo 'run: git submodule update --init')"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      }
    );
}
