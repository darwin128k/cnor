/* The cnor benchmark workload `fib` in Pawn: recursive fib(25). */

fib(n)
{
    if (n < 2)
    {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

main()
{
    return fib(25);
}
