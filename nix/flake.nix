# ~/dev/tact-fetch/nix/flake.nix
{
  description = "tact-fetch: polite CDN fetcher for CASC files a local WoW install never downloaded";

  inputs = {
    pins.url = "path:/home/luna/nix/pins";
    nixpkgs.follows = "pins/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    # CascLib is fetched via this flake input, not a git submodule --
    # single source of truth for both its pinned version (flake.lock) and
    # the local libcurl patch (vendor/patches/, applied on top of it).
    # A git submodule would need its own separately-tracked commit AND a
    # manually-reapplied patch every time it's reset/re-cloned, out of
    # sync with the flake input by construction; this flake input is
    # spliced into vendor/CascLib (then patched) both by
    # packages.default's postUnpack and by devShells.default's shellHook,
    # so `cmake --build` and `nix build` see identical CascLib source
    # either way. Same splicing mechanism as casc-tool's own
    # nix/flake.nix; see that repo's README "Why this needed more than
    # 'just cmake'" section for the fuller rationale on why a flake input
    # is needed at all (a plain `nix build` only sees git-tracked files).
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

        # Only what the packaged build actually needs: CMakeLists.txt + src/
        # + the CascLib patch (see vendor/patches/README.md -- applied to
        # the fetched casclib input below, not carried by the submodule
        # gitlink this fileset otherwise excludes). Deliberately excludes
        # tests/, build/, .git, vendor/CascLib itself -- none of those
        # should ever end up copied into the Nix store for this.
        appSource = lib.fileset.toSource {
          root = projectRoot;
          fileset = lib.fileset.unions [
            (projectRoot + "/CMakeLists.txt")
            (projectRoot + "/src")
            (projectRoot + "/vendor/patches")
          ];
        };

        # curl is what DESIGN.md #9's fetch-pipeline resolution builds on:
        # vendored CascLib's own HttpDownloadFile/RibbitDownloadFile are
        # locally patched (vendor/patches/casclib-libcurl-fetch.patch,
        # applied below) to fetch via libcurl instead of CascLib's raw
        # socket client, so its BLTE-decode/hash-verify pipeline keeps
        # working unmodified while getting real transport-level politeness.
        cpp = with pkgs; [
          cmake
          ninja
          pkg-config
          gcc
          gdb
          ccache
          doctest
          curl
          gnupatch
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
            pkgs.gnupatch
          ];

          buildInputs = [ pkgs.curl ];

          # vendor/CascLib doesn't exist in appSource (see above) -- drop in
          # the fetched CascLib source right after unpacking, before cmake
          # configure runs and hits add_subdirectory(vendor/CascLib), then
          # apply vendor/patches/ on top (vendor/patches/README.md) --
          # same two steps devShells.default's shellHook does for local
          # dev, kept in sync because both read from the same casclib
          # input and the same patch file.
          postUnpack = ''
            mkdir -p "$sourceRoot/vendor/CascLib"
            cp -r ${casclib}/. "$sourceRoot/vendor/CascLib/"
            chmod -R u+w "$sourceRoot/vendor/CascLib"
            patch -d "$sourceRoot/vendor/CascLib" -p1 \
              < "$sourceRoot/vendor/patches/casclib-libcurl-fetch.patch"
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

            # vendor/CascLib is materialized here from the pinned casclib
            # flake input + vendor/patches/, not a git submodule -- see
            # the inputs.casclib comment above. Re-copy/re-patch only when
            # the input or the patch actually changed (a marker file
            # tracks both), so a plain `direnv exec .` stays fast on
            # every call instead of redoing this each time.
            casclib_marker="vendor/CascLib/.tact-fetch-materialized-from"
            casclib_expected="${casclib}:$(sha256sum vendor/patches/casclib-libcurl-fetch.patch | cut -d' ' -f1)"
            if [ "$(cat "$casclib_marker" 2>/dev/null)" != "$casclib_expected" ]; then
              echo "  vendor/CascLib: materializing from casclib flake input + local patch..."
              rm -rf vendor/CascLib
              mkdir -p vendor/CascLib
              cp -r ${casclib}/. vendor/CascLib/
              chmod -R u+w vendor/CascLib
              patch -d vendor/CascLib -p1 < vendor/patches/casclib-libcurl-fetch.patch
              echo "$casclib_expected" > "$casclib_marker"
            fi
            echo "  vendor/CascLib: $(basename ${casclib}) (patched, see vendor/patches/)"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      }
    );
}
