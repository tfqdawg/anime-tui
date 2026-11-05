{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell{
  nativeBuildInputs = with pkgs; [
		gcc
		pkg-config
	];
  
  buildInputs = with pkgs; [ 
    ncurses 
    sqlite 
  ];
}
