{ pkgs ? import <nixpkgs> { } }:
let 
  #pythonEnv = pkgs.python3.withPackages(ps: with ps; [ requests intelhex ]);
in pkgs.mkShell {
  buildInputs = with pkgs ; [
    #pkgs.adafruit-nrfutil
    gcc-arm-embedded  # ARM 编译器
    glibtool
    libtool
    autoconf
    pkg-config
    automake
    libusb1
    #pythonEnv            # Python 环境
    #openocd
    #telnet
  ];

  # 你也可以把 SDK 路径设置为环境变量让构建系统找到
  shellHook = ''
  '';
}
