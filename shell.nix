{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell{
  nativeBuildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config
  ];
  
  buildInputs = with pkgs; [ 
    ncurses 
    sqlite 
  ];
}
