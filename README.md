mdlinks - given a list of directories/files, prints URLs found in
markdown documents, except for naked urls.

Multithreaded, written in C using the CommonMark library.

Install like this:

```bash
brew install gcc cmark libyaml # on macOS

make
./mdlinks somefile.md
```
