{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell{
  nativeBuildInputs = with pkgs; [];
  
  buildInputs = with pkgs; [ 
    ncurses 
    sqlite 
  ];
}
