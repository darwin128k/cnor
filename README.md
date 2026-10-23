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

## Use

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
