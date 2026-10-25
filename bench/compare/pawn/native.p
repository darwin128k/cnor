/* The cnor benchmark workload `native` in Pawn: 1M calls of a host native. */

native absval(value);

calls(n)
{
    new s = 0;
    for (new i = 0; i < n; i++)
    {
        s += absval(i - 500000);
    }
    return s;
}

main()
{
    return calls(1000000);
}
