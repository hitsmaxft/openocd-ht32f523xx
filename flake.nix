{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem ["aarch64-darwin"](system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        shellWithPkgs = packages: pkgs.mkShell {
          inherit packages;
        };
      in
      {
        devShells.default = shellWithPkgs [ ];
        packages.default = pkgs.callPackage ./package.nix {};
      });
}

