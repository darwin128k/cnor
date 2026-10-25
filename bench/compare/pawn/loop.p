/* The cnor benchmark workload `loop` in Pawn: a while loop summing 0..10M. */

sum(n)
{
    new s = 0;
    new i = 0;
    while (i < n)
    {
        s += i;
        i++;
    }
    return s;
}

main()
{
    return sum(10000000);
}
