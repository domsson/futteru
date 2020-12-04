# futteru 

Inspired by [sontek's snowmachine](https://github.com/sontek/snowmachine), 
I decided to quickly adapt [fakesteak](https://github.com/domsson/fakesteak)'s 
code to implement something similar in C. 

## Overview 

Simply makes a couple different 'snow like' characters fall down your 
terminal in two layers. No Unicode characters, just extended ASCII. 

Customization can be done by adjusting the characters and/or colors 
right in the source.

Code is still a bit messy, not polished yet. Should work though.
Hopefully. :-)

## Dependencies / Requirements

- Terminal that supports 256 colors ([8 bit color mode](https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit))
- Requires `TIOCGWINSZ` to be supported (to query the terminal size)

## Building / Running

You can compile it with the provided `build` script.

    chmod +x ./build
    ./build
    ./bin/futteru

## Usage

    futteru [OPTIONS...]

Options:

  - `-b`: use black background color
  - `-d`: density factor ([1..100], default is 10)
  - `-h`: print help text and exit
  - `-r`: seed for the random number generator
  - `-s`: speed factor ([1..100], default is 10)
  - `-V`: print version information and exit

## Support

[![ko-fi](https://www.ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/L3L22BUD8)

