# Runtime tracebacks

`cnor run`, `cnor exec` and the REPL print saved script frames after an
uncaught runtime error. The first frame is the fault; the following frames
identify where each caller made its call:

```text
trace.cn:2: division by zero
  at divide (trace.cn:2)
  called from attack (trace.cn:4)
  called from <main> (trace.cn:7)
```

The original diagnostic and exit status remain unchanged. Compile errors and
image-load failures do not have a script trace. Successfully handled errors
do not print one. Native failures identify the script call site; native C
frames are not part of the script stack.

The VM reads function names, files and lines from its private copy of the
loaded `.cnb`; it never reads source files. Without a line table
(`cnor compile --no-lines`), names and files are still shown without line
numbers. Images without function names use `<function #N>` in the CLI;
the unnamed entry function is shown as `<main>`.

## C API

After `cnor_vm_run` or `cnor_vm_call` returns false:

```c
cnor_vm_trace_frame_t frame;
cnor_count_t count = cnor_vm_get_trace_count(&vm);
for (cnor_count_t i = 0; i < count; ++i)
{
    if (cnor_vm_get_trace_frame(&vm, i, &frame))
    {
        /* frame.function: index in the image's function table
         * frame.name, frame.file: borrowed string views
         * frame.line: source line, or 0 when absent
         * frame.offset: an address within the fault/call instruction */
    }
}
lh_bool_t shortened = cnor_vm_trace_is_truncated(&vm);
```

Frame zero is innermost. A host call starts at the requested function and
does not invent a `<main>` caller. Out-of-range indices and a null output
pointer return false. A failed load or pre-execution validation, such as an
unknown host-call target, has no trace.

The saved trace is cleared by the next run, call, reset or load, and after
successful execution. Copy any frames needed longer. Name/file views point
into the image and remain valid only until the next load or VM deinitialization.
The error dictionary available to script `catch` blocks keeps its existing
five fields; traceback frames are exposed through the host API.

## Fixed memory budget

```c
cnor_vm_config_t config = cnor_vm_config_initializer();
config.trace_capacity = 128; /* default: 64; 0 disables traceback storage */
cnor_vm_init(&vm, &config);
```

The buffer is allocated at image load, with no allocation during script
execution or error capture. The implementation uses 12 bytes per saved node
(768 bytes for the default pool), plus snapshot references in try handlers.
The capacity limits saved nodes shared by the current error and errors still
held by catch/finally handlers, rather than increasing the VM's call-depth
limit. Repeated handled errors release their saved nodes for reuse.

Snapshots share caller suffixes. This preserves the original chain through
`raise`, `finally` and nested caught errors without copying an entire stack
per handler. If a new error replaces an old error in a finally block, its own
trace is used. Ordinary captures retain the innermost frames when the call
chain exceeds capacity. If simultaneously pending errors exhaust the shared
pool, the fault origin is still available and the API marks the chain as
truncated. The CLI appends `... traceback truncated` in either case.

Setting capacity to zero removes the snapshot allocation and produces an
empty trace; ordinary diagnostics still work. The bytecode format and
function-call frame layout are unchanged.
