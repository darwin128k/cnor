-- The cnor benchmark workloads in Lua 5.4, for comparison. Run by compare.py:
--   lua5.4 workloads.lua NAME ITERATIONS WARMUP
-- prints "median_ns min_ns result". Locals throughout, as in the cnor sources.

local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

local function loop(n)
    local s, i = 0, 0
    while i < n do
        s = s + i
        i = i + 1
    end
    return s
end

local function nested(n)
    local s = 0
    for i = 0, n - 1 do
        for j = 0, n - 1 do
            s = s + (i * j) % 7
        end
    end
    return s
end

local function text(t, rounds)
    local hits, joined = 0, 0
    local n = #t
    local sub = string.sub
    for r = 0, rounds - 1 do
        for i = 1, n do
            if sub(t, i, i) == "o" then hits = hits + 1 end
        end
        local pair = sub(t, r % n + 1, r % n + 1) .. sub(t, (r + 1) % n + 1, (r + 1) % n + 1)
        joined = joined + #pair
    end
    return hits + joined
end

local function native(n)
    local s = 0
    local abs = math.abs -- a C function, like the cnor native
    for i = 0, n - 1 do
        s = s + abs(i - 500000)
    end
    return s
end

local function match(n)
    local s = 0
    for i = 0, n - 1 do
        local m = i % 8
        if m == 0 then s = s + 1
        elseif m == 1 then s = s + 3
        elseif m == 2 then s = s - 2
        elseif m == 3 then s = s + 5
        elseif m == 4 then s = s * 1
        elseif m == 5 then s = s + 7
        else s = s + 0 end
    end
    return s
end

local work = {
    fib = function() return fib(25) end,
    loop = function() return loop(10000000) end,
    nested = function() return nested(1000) end,
    text = function() return text("the quick brown fox jumps over the lazy dog", 20000) end,
    native = function() return native(1000000) end,
    match = function() return match(1000000) end,
}

local name, iterations, warmup = arg[1], tonumber(arg[2]), tonumber(arg[3])
local fn = work[name]
local clock = os.clock -- CPU seconds; the closest to a monotonic clock in plain Lua
local samples, result = {}, nil
for round = 1, warmup + iterations do
    local start = clock()
    result = fn()
    local elapsed = clock() - start
    if round > warmup then samples[#samples + 1] = elapsed end
end
table.sort(samples)
local median
if #samples % 2 == 1 then
    median = samples[(#samples + 1) // 2]
else
    median = (samples[#samples // 2] + samples[#samples // 2 + 1]) / 2
end
print(string.format("%d %d %d", math.floor(median * 1e9), math.floor(samples[1] * 1e9), result))
