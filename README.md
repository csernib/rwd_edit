Simple command-line tool to read/write RWD files used by the *Kohan II: Kings of War* and *Axis & Allies* video games developed by TimeGate Studios.

Usage:
```
rwd_edit <mode> <mode_arguments>

Modes:
  list <rwd_file>
      lists the content of rwd_file

  pack <rwd_file> <directory>
      replaces the content of rwd_file with that in directory

  unpack <rwd_file> <empty_directory>
      extracts the content of rwd_file to empty_directory;
```

In pack mode, the program is not able to add or remove files to/from the RWD, only overwrite existing ones with those in the specified directory. The intent is to first unpack the RWD, mod the game files you want, then pack them back. The program takes care to repack files in the same order, thus minimizing the changes compared to the original RWD after modding.

The program comes with absolutely no warranty. Backup your RWD file before use. It seems to work fine for K2, but I never tried it with A&A. Performance and code style could be improved, but as this was just an afternoon project, I consider it fine as it is. PRs for possible bug fixes are welcome.

Building requires a C++ 23 capable compiler.

The implementation is based on the findings of [www.watto.org](https://www.watto.org/specs.html?specs=Archive_RWD_TGCK). Many thanks to them!

Happy modding! :)
