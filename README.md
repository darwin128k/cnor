# cnor

A small script language for embedded hosts, built on [lh](https://github.com/darwin128k/lh).

cnor is a compiler and an interpreter: source text is compiled into a portable
`.tbc` image, and the VM loads, verifies and runs the image with memory sized
up front. See [docs/lang-spec.md](docs/lang-spec.md) for the language and the
image format, and [docs/architecture.md](docs/architecture.md) for how the code
is organised and how to extend it.

## Layout

- `include/cnor/` - public headers
- `src/cnor/` - implementation (headers here are library-private)
- `cli/` - the `cnor` command line (`CNOR_BUILD_CLI`)
- `tests/` - tests (`CNOR_BUILD_TESTS`)
- `bench/` - benchmarks (`CNOR_BUILD_BENCH`), see [bench/README.md](bench/README.md)
- `docs/` - language specification and architecture notes
- `lib/lh/` - lh, as a git submodule
- `licenses/` - third-party license texts

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
```

With tests:

```sh
cmake -S . -B build -DCNOR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Benchmarks (use an optimised build):

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DCNOR_BUILD_BENCH=ON
cmake --build build-bench
./build-bench/bench/cnor_bench
```

The machine without the compiler (`-DCNOR_BUILD_COMPILER=OFF`): the library
holds the machine, the reader of an image and the verifier, and nothing that
turns a source into an image. The command line runs images only (`exec`); the
script is compiled on the desktop, and the image is what the two halves
exchange. The library is 45% smaller that way.

## Command line

The build also makes `cnor` (`build/cli/cnor`, `cnor.exe` on Windows) when
cnor is the top-level project; `-DCNOR_BUILD_CLI=OFF` leaves it out, `ON`
builds it when cnor is a subdirectory of another project.

```sh
cnor run script.cn                 # compile and run; prints the result unless it is none
cnor script.cn                     # the same
cnor run - < script.cn             # the source from the standard input
cnor run script.cn --max-steps 100000 --stack 1024 --depth 64 --pool 65536
cnor compile script.cn -o script.tbc   # write the image (default: script.tbc)
cnor compile script.cn --no-lines      # without the line table
cnor run app.cn -I lib -I vendor   # look for imported files in lib/ and vendor/ too
cnor exec script.tbc               # load and run an image
cnor --version
cnor --help
```

| Option (`run`, `exec`) | Default | |
|---|---|---|
| `--max-steps N` | 0 (unlimited) | instructions per run |
| `--stack N` | 65536 | value stack, in values |
| `--depth N` | 4096 | nested calls |
| `--pool N` | 16777216 | string pool, in bytes |

| Option (`run`, `compile`) | |
|---|---|
| `-I DIR`, `--import-path DIR` | look for imported files in `DIR` after the importing file's directory; repeatable, up to 16 |

`import "path"` lines at the top of a script name other files: they are read
from the file system relative to the importing file, then in each `-I`
directory, and compiled into the same image (`compile` writes one `.tbc` that
needs none of the sources).

Scripts get four natives, without declaring them: `print(...)` and
`println(...)` write their arguments to the standard output separated by one
space (`println` ends the line), `eprint(...)` and `eprintln(...)` do the
same on the standard error. Values are written as `none`, `true` / `false`,
decimal integers, reals in their shortest exact form (`0.1`, `2.0`), and
strings as their bytes.

Errors go to the standard error as `file:line: message`, for example
`script.cn:3: unknown name: missing`, `script.cn:4: type mismatch: expected
i32, found str` or `lib/math.cn:12: division by zero`, naming the file the
failing code is in; an image keeps the source names and line table, so `exec`
reports the same.
Exit status: 0 done, 1 runtime error (including `--max-steps`), 2 bad command
line, 3 compile error or bad image, 4 file error. File names are UTF-8 on every
system; a leading UTF-8 byte order mark in a source file is skipped.

```sh
$ cat hello.cn
fn fib(n: i32) -> i32:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
println("fib(20) =", fib(20))
$ cnor hello.cn
fib(20) = 6765
```

## Use

Types are checked at compile time, globals are initialized in source order and
files are combined with `import`: see sections 6.3 to 6.5 of the
[language specification](docs/lang-spec.md). A host that compiles scripts with
imports gives the compiler a loader (`cnor_compiler_config_t.load` and
`release`, plus `search_paths`): the library never reads files itself.

```c
#include <cnor/compiler.h>
#include <cnor/vm.h>

cnor_image_t image;
cnor_diag_t diag;
cnor_vm_t vm;

if (cnor_compiler_compile_source(source, lh_null, &image, &diag))
{
    cnor_vm_init(&vm, lh_null);
    if (cnor_vm_load(&vm, image.data, image.size) && cnor_vm_run(&vm))
    {
        lh_value_t result = cnor_vm_get_result(&vm);
        /* ... */
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&image);
}
```

## License

MIT, see [LICENSE.md](LICENSE.md).
